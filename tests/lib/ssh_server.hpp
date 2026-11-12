// ── tests/lib/ssh_server.hpp ──────────────────────────────
// 测试用的临时 SSH 服务器：在临时目录里生成主机密钥与客户端密钥，写一份最小
// sshd_config，以当前用户身份在 127.0.0.1 的高端口上起一个 **临时 sshd**。
//
// 为什么不使用系统 sshd 服务：启动服务需要管理员权限，而且测试不应该依赖
// （或修改）机器的服务状态。Windows OpenSSH 9.5 的 sshd.exe 允许非管理员以
// 当前用户身份运行，只做密钥认证时足够用。
//
// 生命周期：start() 后必须调用 stop()；stop() 终止进程树并删除临时目录。
// 所有等待都有截止时间——测试允许用超时判定失败，产品代码不允许。
#pragma once
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <string>
#include <thread>

namespace corehost::test
{

namespace ssh_details
{
inline std::wstring open_ssh_dir()
{
    wchar_t root[MAX_PATH]{};
    const UINT len = ::GetWindowsDirectoryW(root, ARRAYSIZE(root));
    if (len == 0)
        return {};
    return std::wstring{root} + L"\\System32\\OpenSSH";
}

inline bool run_tool(const std::wstring &exe, const std::wstring &args, DWORD timeout_ms)
{
    std::wstring command = L"\"" + exe + L"\" " + args;
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESHOWWINDOW;
    startup.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION process{};
    if (::CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &startup,
                         &process) == FALSE)
        return false;

    const DWORD wait = ::WaitForSingleObject(process.hProcess, timeout_ms);
    bool ok = false;
    if (wait == WAIT_OBJECT_0)
    {
        DWORD exit_code = 1;
        ok = ::GetExitCodeProcess(process.hProcess, &exit_code) != FALSE && exit_code == 0;
    }
    else
    {
        ::TerminateProcess(process.hProcess, 1);
        ::WaitForSingleObject(process.hProcess, INFINITE);
    }
    ::CloseHandle(process.hThread);
    ::CloseHandle(process.hProcess);
    return ok;
}

inline bool write_file(const std::filesystem::path &path, const std::string &text)
{
    FILE *file = nullptr;
    if (::_wfopen_s(&file, path.c_str(), L"wb") != 0 || file == nullptr)
        return false;
    const size_t written = std::fwrite(text.data(), 1, text.size(), file);
    std::fclose(file);
    return written == text.size();
}

inline bool port_open(int port)
{
    SOCKET socket_handle = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (socket_handle == INVALID_SOCKET)
        return false;

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = ::htons(static_cast<u_short>(port));
    address.sin_addr.s_addr = ::htonl(INADDR_LOOPBACK);

    const bool connected = ::connect(socket_handle, reinterpret_cast<sockaddr *>(&address), sizeof(address)) == 0;
    ::closesocket(socket_handle);
    return connected;
}

inline bool wait_port(int port, DWORD timeout_ms)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds{timeout_ms};
    while (std::chrono::steady_clock::now() < deadline)
    {
        if (port_open(port))
            return true;
        ::Sleep(50);
    }
    return false;
}

inline std::string to_ansi(const std::wstring &text)
{
    if (text.empty())
        return {};
    const int needed =
        ::WideCharToMultiByte(CP_ACP, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    if (needed <= 0)
        return {};
    std::string result(static_cast<size_t>(needed), '\0');
    ::WideCharToMultiByte(CP_ACP, 0, text.c_str(), static_cast<int>(text.size()), result.data(), needed, nullptr,
                          nullptr);
    return result;
}
} // namespace ssh_details

// 临时 SSH 服务器。析构时自动 stop()。
class ssh_server
{
  public:
    ssh_server() = default;
    ssh_server(const ssh_server &) = delete;
    ssh_server &operator=(const ssh_server &) = delete;
    ~ssh_server()
    {
        stop();
    }

    // 生成密钥、写配置、启动 sshd 并等到端口可连接。
    bool start(std::wstring &error)
    {
        WSADATA data{};
        if (::WSAStartup(MAKEWORD(2, 2), &data) != 0)
        {
            error = L"WSAStartup failed";
            return false;
        }
        _wsa_started = true;

        const std::wstring ssh_dir = ssh_details::open_ssh_dir();
        const std::wstring sshd = ssh_dir + L"\\sshd.exe";
        const std::wstring keygen = ssh_dir + L"\\ssh-keygen.exe";
        if (!std::filesystem::exists(sshd) || !std::filesystem::exists(keygen))
        {
            error = L"OpenSSH sshd.exe/ssh-keygen.exe not found";
            return false;
        }

        // 放在测试可执行文件同目录：构建目录一定可写（日志也写在那里），
        // 不依赖 %TEMP% 的权限（受限环境里可能被拒绝）。
        wchar_t module[MAX_PATH]{};
        if (::GetModuleFileNameW(nullptr, module, ARRAYSIZE(module)) == 0)
        {
            error = L"GetModuleFileName failed";
            return false;
        }

        _dir = std::filesystem::path{module}.parent_path() /
               (L"ssh-tmp-" + std::to_wstring(::GetCurrentProcessId()) + L"-" + std::to_wstring(::GetTickCount64()));
        std::error_code filesystem_error;
        std::filesystem::create_directories(_dir, filesystem_error);
        if (filesystem_error)
        {
            error = L"cannot create temp dir: " + _dir.wstring() + L" err=" + std::to_wstring(filesystem_error.value());
            return false;
        }

        const auto host_key = _dir / L"hostkey";
        const auto client_key = _dir / L"clientkey";
        if (!ssh_details::run_tool(keygen, L"-q -t ed25519 -N \"\" -f \"" + host_key.wstring() + L"\"", 15000) ||
            !ssh_details::run_tool(keygen, L"-q -t ed25519 -N \"\" -f \"" + client_key.wstring() + L"\"", 15000))
        {
            error = L"ssh-keygen failed";
            return false;
        }

        // authorized_keys 用客户端公钥。
        std::error_code copy_error;
        std::filesystem::copy_file(std::filesystem::path{client_key.wstring() + L".pub"}, _dir / L"authorized_keys",
                                   std::filesystem::copy_options::overwrite_existing, copy_error);
        if (copy_error)
        {
            error = L"cannot install authorized_keys";
            return false;
        }

        wchar_t user[256]{};
        DWORD user_length = ARRAYSIZE(user);
        if (::GetUserNameW(user, &user_length) == FALSE)
        {
            error = L"GetUserName failed";
            return false;
        }
        _user = user;

        // sshd 为非 PTY 远程命令创建的子进程（cmd/powershell）会新建可见的
        // 控制台窗口。把 sshd 及其子进程放到一个隐藏桌面上，窗口就不会出现在
        // 用户桌面（CreateDesktop 失败时退回默认桌面，测试仍可运行）。
        _desktop_name =
            L"corehost-ssh-" + std::to_wstring(::GetCurrentProcessId()) + L"-" + std::to_wstring(::GetTickCount64());
        _desktop = ::CreateDesktopW(_desktop_name.c_str(), nullptr, nullptr, 0, GENERIC_ALL, nullptr);

        // 端口从随机起点向上试，直到 sshd 能起来并可连接。
        const int first_port = 21200 + static_cast<int>(::GetTickCount64() % 400u);
        for (int attempt = 0; attempt < 5; ++attempt)
        {
            const int port = first_port + attempt;
            const auto config = _dir / L"sshd_config";
            std::string text;
            text += "Port " + std::to_string(port) + "\n";
            text += "ListenAddress 127.0.0.1\n";
            text += "HostKey " + _dir.string() + "\\hostkey\n";
            text += "PidFile " + _dir.string() + "\\sshd.pid\n";
            text += "AuthorizedKeysFile " + _dir.string() + "\\authorized_keys\n";
            text += "PasswordAuthentication no\n";
            text += "PubkeyAuthentication yes\n";
            text += "PermitRootLogin no\n";
            text += "StrictModes no\n";
            text += "LogLevel VERBOSE\n";
            text += "AllowUsers " + ssh_details::to_ansi(_user) + "\n";
            if (!ssh_details::write_file(config, text))
            {
                error = L"cannot write sshd_config";
                return false;
            }

            if (!start_sshd(sshd, config))
            {
                error = L"cannot start sshd";
                return false;
            }

            if (ssh_details::wait_port(port, 10000))
            {
                _port = port;
                return true;
            }
            stop_sshd();
        }

        error = L"sshd did not start listening";
        return false;
    }

    void stop()
    {
        stop_sshd();
        if (_desktop != nullptr)
        {
            ::CloseDesktop(_desktop);
            _desktop = nullptr;
            _desktop_name.clear();
        }
        if (_wsa_started)
        {
            ::WSACleanup();
            _wsa_started = false;
        }
        if (!_dir.empty())
        {
            std::error_code ignored;
            std::filesystem::remove_all(_dir, ignored);
            _dir.clear();
        }
    }

    [[nodiscard]] bool valid() const noexcept
    {
        return _port != 0;
    }

    [[nodiscard]] int port() const noexcept
    {
        return _port;
    }

    [[nodiscard]] const std::wstring &user() const noexcept
    {
        return _user;
    }

    [[nodiscard]] std::wstring client_key() const
    {
        return (_dir / L"clientkey").wstring();
    }

    // 服务器自己的主机私钥（不在 authorized_keys 中），用做"未授权客户端密钥"。
    [[nodiscard]] std::wstring host_key() const
    {
        return (_dir / L"hostkey").wstring();
    }

    // 连接参数：非交互命令与交互式会话共用。
    [[nodiscard]] std::wstring client_options() const
    {
        std::wstring options = L"-p " + std::to_wstring(_port);
        options += L" -i \"" + client_key() + L"\"";
        options += L" -o StrictHostKeyChecking=no -o UserKnownHostsFile=NUL";
        options += L" -o BatchMode=yes -o LogLevel=ERROR";
        options += L" -o PubkeyAcceptedKeyTypes=+ssh-ed25519";
        return options;
    }

  private:
    bool start_sshd(const std::wstring &sshd, const std::filesystem::path &config)
    {
        const auto log_path = _dir / L"sshd.log";
        SECURITY_ATTRIBUTES security{};
        security.nLength = sizeof(security);
        security.bInheritHandle = TRUE;
        _log = ::CreateFileW(log_path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, &security, CREATE_ALWAYS,
                             FILE_ATTRIBUTE_NORMAL, nullptr);

        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        startup.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
        startup.wShowWindow = SW_HIDE;
        startup.hStdOutput = _log;
        startup.hStdError = _log;
        startup.hStdInput = nullptr;
        if (_desktop != nullptr)
            startup.lpDesktop = _desktop_name.data();

        std::wstring command = L"\"" + sshd + L"\" -D -f \"" + config.wstring() + L"\" -e";
        PROCESS_INFORMATION process{};
        const BOOL created =
            ::CreateProcessW(nullptr, command.data(), nullptr, nullptr, TRUE,
                             CREATE_NO_WINDOW | CREATE_NEW_PROCESS_GROUP, nullptr, nullptr, &startup, &process);
        if (_log != nullptr)
        {
            ::CloseHandle(_log);
            _log = nullptr;
        }
        if (created == FALSE)
            return false;

        _process = process;
        return true;
    }

    void stop_sshd()
    {
        if (_process.hProcess != nullptr)
        {
            ::TerminateProcess(_process.hProcess, 1);
            ::WaitForSingleObject(_process.hProcess, 5000);
            ::CloseHandle(_process.hThread);
            ::CloseHandle(_process.hProcess);
            _process = PROCESS_INFORMATION{};
        }
        _port = 0;
    }

    std::filesystem::path _dir;
    std::wstring _user;
    PROCESS_INFORMATION _process{};
    HANDLE _log = nullptr;
    HDESK _desktop = nullptr;
    std::wstring _desktop_name;
    int _port = 0;
    bool _wsa_started = false;
};

} // namespace corehost::test