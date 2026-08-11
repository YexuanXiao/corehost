// === tests/test_conpty_powershell_error.cpp ===
// 复现/验证：powershell 执行不存在的命令时，错误消息应换行且为红色。
// pwsh 行为正常；powershell 5.1 出现"不换行 + 蓝色"的问题。
//
// 测试流程：
// 1. 创建 ConPTY，启动 powershell.exe
// 2. 等待提示符
// 3. 输入 hello + Enter
// 4. 捕获输出，检查错误消息是否以换行开始、是否包含红色 SGR
#define WIN32_NO_STATUS
#include <windows.h>
#undef WIN32_NO_STATUS

#include "libconpty/libconpty.hpp"
#include "win32/handle.hpp"
#include "win32/wait.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>

namespace
{

[[noreturn]] void fail(const char *message)
{
    std::fprintf(stderr, "FAIL: %s\n", message);
    std::exit(1);
}

[[noreturn]] void fail_win32(const char *message)
{
    std::fprintf(stderr, "FAIL: %s: %lu\n", message, ::GetLastError());
    std::exit(1);
}

std::wstring getenv_wide(const wchar_t *name)
{
    const DWORD required = ::GetEnvironmentVariableW(name, nullptr, 0);
    if (required == 0)
        return {};
    std::wstring value(required - 1, L'\0');
    ::GetEnvironmentVariableW(name, value.data(), required);
    return value;
}

bool file_exists(const std::wstring &path)
{
    const DWORD attributes = ::GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

std::wstring find_shell()
{
    if (auto env = getenv_wide(L"COREHOST_TEST_SHELL"); !env.empty())
        return env;

    // 优先 windows powershell 5.1（System32\WindowsPowerShell\v1.0\powershell.exe）
    wchar_t system_dir[MAX_PATH]{};
    const UINT system_len = ::GetSystemDirectoryW(system_dir, MAX_PATH);
    if (system_len != 0 && system_len < MAX_PATH)
    {
        std::wstring path{system_dir, system_len};
        path += L"\\WindowsPowerShell\\v1.0\\powershell.exe";
        if (file_exists(path))
            return path;
        // 回退到 pwsh（PATH 中查找）
        wchar_t found[MAX_PATH]{};
        if (::SearchPathW(nullptr, L"pwsh.exe", nullptr, MAX_PATH, found, nullptr) != 0)
            return found;
    }
    return {};
}

std::wstring quote(std::wstring_view value)
{
    std::wstring result;
    result.reserve(value.size() + 2);
    result.push_back(L'"');
    result.append(value);
    result.push_back(L'"');
    return result;
}

void write_all(HANDLE pipe, std::string_view bytes)
{
    while (!bytes.empty())
    {
        DWORD written = 0;
        const auto chunk = static_cast<DWORD>(std::min<size_t>(bytes.size(), 4096));
        if (!::WriteFile(pipe, bytes.data(), chunk, &written, nullptr))
            fail_win32("WriteFile(terminal input) failed");
        if (written == 0)
            fail("WriteFile(terminal input) wrote zero bytes");
        bytes.remove_prefix(written);
    }
}

// 后台读取终端输出，保存完整字节供分析
class terminal_reader
{
  public:
    explicit terminal_reader(win32::handle output_read) : _output_read{std::move(output_read)}
    {
        _thread = std::thread([this] { read_loop(); });
    }

    ~terminal_reader()
    {
        _output_read.clear();
        if (_thread.joinable())
            _thread.join();
    }

    bool wait_for(std::string_view marker, std::chrono::milliseconds timeout)
    {
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        std::unique_lock lock{_mutex};
        return _cv.wait_until(lock, deadline, [&] { return _output.find(marker) != std::string::npos || _closed; }) &&
               _output.find(marker) != std::string::npos;
    }

    std::string snapshot()
    {
        std::scoped_lock lock{_mutex};
        return _output;
    }

    size_t bytes_read() const
    {
        std::scoped_lock lock{_mutex};
        return _output.size();
    }

    void clear()
    {
        std::scoped_lock lock{_mutex};
        _output.clear();
    }

  private:
    void read_loop()
    {
        char buffer[64 * 1024];
        for (;;)
        {
            DWORD read = 0;
            if (!::ReadFile(_output_read.get(), buffer, sizeof(buffer), &read, nullptr) || read == 0)
                break;
            {
                std::scoped_lock lock{_mutex};
                _output.append(buffer, read);
            }
            _cv.notify_all();
        }
        {
            std::scoped_lock lock{_mutex};
            _closed = true;
        }
        _cv.notify_all();
    }

    win32::handle _output_read;
    std::thread _thread;
    mutable std::mutex _mutex;
    std::condition_variable _cv;
    std::string _output;
    bool _closed = false;
};

struct conpty_session
{
    HPCON hpc = nullptr;
    win32::handle input_write;
    win32::handle output_read;
    win32::handle child;
    std::unique_ptr<terminal_reader> reader;

    ~conpty_session()
    {
        if (hpc)
            ConptyClosePseudoConsole(hpc);
        if (child.valid())
            ::TerminateProcess(child.get(), 0);
    }

    void start(const std::wstring &shell_path)
    {
        SECURITY_ATTRIBUTES inheritable{sizeof(inheritable), nullptr, TRUE};
        win32::handle pty_input_read;
        win32::handle pty_output_write;
        if (!::CreatePipe(pty_input_read.put(), input_write.put(), &inheritable, 0))
            fail_win32("CreatePipe(input) failed");
        if (!::CreatePipe(output_read.put(), pty_output_write.put(), &inheritable, 0))
            fail_win32("CreatePipe(output) failed");

        constexpr COORD size{120, 30};
        const HRESULT hr = ConptyCreatePseudoConsole(size, pty_input_read.get(), pty_output_write.get(), 0, &hpc);
        if (FAILED(hr))
        {
            std::fprintf(stderr, "ConptyCreatePseudoConsole failed: 0x%08lx\n", static_cast<unsigned long>(hr));
            std::exit(1);
        }
        pty_input_read.clear();
        pty_output_write.clear();

        console::proc_thread_attribute_list attributes;
        if (FAILED(attributes.initialize(1)))
            fail("InitializeProcThreadAttributeList failed");
        if (!::UpdateProcThreadAttribute(attributes.get(), 0, PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE, hpc, sizeof(hpc),
                                         nullptr, nullptr))
            fail_win32("UpdateProcThreadAttribute(PSEUDOCONSOLE) failed");

        STARTUPINFOEXW startup{};
        startup.StartupInfo.cb = sizeof(startup);
        startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
        startup.lpAttributeList = attributes.get();

        auto command = quote(shell_path);
        PROCESS_INFORMATION pi{};
        if (!::CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE,
                              EXTENDED_STARTUPINFO_PRESENT | CREATE_UNICODE_ENVIRONMENT, nullptr, nullptr,
                              &startup.StartupInfo, &pi))
            fail_win32("CreateProcessW(shell) failed");

        child = win32::handle{pi.hProcess};
        ::CloseHandle(pi.hThread);

        if (FAILED(ConptyReleasePseudoConsole(hpc)))
            fail("ConptyReleasePseudoConsole failed");

        reader = std::make_unique<terminal_reader>(std::move(output_read));
    }
};

// 把输出转成可读的转义形式
std::string printable(std::string_view s)
{
    std::string out;
    for (unsigned char ch : s)
    {
        if (ch == 0x1b)
            out += "\\x1b";
        else if (ch == '\r')
            out += "\\r";
        else if (ch == '\n')
            out += "\\n";
        else if (ch < 0x20 || ch >= 0x7f)
        {
            char buf[8];
            std::snprintf(buf, sizeof(buf), "\\x%02x", ch);
            out += buf;
        }
        else
            out += static_cast<char>(ch);
    }
    return out;
}

} // namespace

int main()
{
    const auto shell_path = find_shell();
    if (shell_path.empty())
        fail("could not find powershell/pwsh (set COREHOST_TEST_SHELL)");
    std::wprintf(L"=== testing shell: %ls ===\n", shell_path.c_str());

    conpty_session session;
    session.start(shell_path);

    // 等待第一个提示符
    if (!session.reader->wait_for(">", std::chrono::seconds(15)))
        fail("no prompt within 15s");
    session.reader->clear();

    // 输入 hello + Enter
    write_all(session.input_write.get(), "hello\r");
    std::this_thread::sleep_for(std::chrono::seconds(3));

    const auto output = session.reader->snapshot();
    std::printf("=== captured %zu bytes ===\n", output.size());
    std::printf("%s\n", printable(output).c_str());

    // 分析断言：
    // 1. 错误消息应从新行开始（前面有 \r\n 或 \n）
    // 2. 错误消息应为红色（兼容 \x1b[31m / \x1b[31;1m / \x1b[0;91;40m /
    //    38;5;9 / 38;2;255;0;0 等红色 SGR 写法）
    const bool has_newline = output.find("\r\n") != std::string::npos || output.find("\n") != std::string::npos;
    const bool has_red = output.find("\x1b[31m") != std::string::npos || output.find("\x1b[31;") != std::string::npos ||
                         output.find("\x1b[91m") != std::string::npos || output.find("\x1b[91;") != std::string::npos ||
                         output.find("\x1b[0;31") != std::string::npos || output.find("\x1b[0;91") != std::string::npos ||
                         output.find("\x1b[38;5;9m") != std::string::npos ||
                         output.find("\x1b[38;2;255;0;0m") != std::string::npos;
    const bool has_blue = output.find("\x1b[34m") != std::string::npos || output.find("\x1b[34;") != std::string::npos ||
                          output.find("\x1b[94m") != std::string::npos || output.find("\x1b[94;") != std::string::npos ||
                          output.find("\x1b[0;34") != std::string::npos || output.find("\x1b[0;94") != std::string::npos ||
                          output.find("\x1b[38;5;4m") != std::string::npos ||
                          output.find("\x1b[38;5;12m") != std::string::npos;

    std::printf("=== analysis: newline=%d red=%d blue=%d ===\n", has_newline, has_red, has_blue);

    int failures = 0;
    if (!has_newline)
    {
        std::printf("FAIL: no newline in error output\n");
        ++failures;
    }
    if (!has_red)
    {
        std::printf("FAIL: error output is not red\n");
        ++failures;
    }
    if (has_blue && !has_red)
    {
        std::printf("FAIL: error output is blue instead of red\n");
        ++failures;
    }
    if (output.find("hello") == std::string::npos)
    {
        std::printf("FAIL: output does not contain 'hello'\n");
        ++failures;
    }

    if (failures == 0)
        std::printf("PASS\n");
    else
        std::printf("%d FAILURES\n", failures);
    return failures == 0 ? 0 : 1;
}
