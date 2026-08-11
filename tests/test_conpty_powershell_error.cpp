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

// 后台读取终端输出，保存完整字节供分析。
// 同时模拟真实终端：收到 DSR CPR 查询（\x1b[6n）时用输入管道响应
// \x1b[1;1R（初始光标 1-based (1,1)）。没有这个响应，corehost 无法继承
// 终端光标（cursor_valid=false），输入 echo / Enter 换行处理会走降级路径，
// 无法复现 Windows Terminal 下的真实行为。
class terminal_reader
{
  public:
    terminal_reader(win32::handle output_read, HANDLE input_write)
        : _output_read{std::move(output_read)}, _input_write{input_write}
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
        _cpr_scan_offset = 0;
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
            bool cpr_found = false;
            {
                std::scoped_lock lock{_mutex};
                _output.append(buffer, read);
                // 扫描 CPR 查询；只响应扫描窗口内新出现的 \x1b[6n
                for (;;)
                {
                    const auto pos = _output.find("\x1b[6n", _cpr_scan_offset);
                    if (pos == std::string::npos)
                        break;
                    _cpr_scan_offset = pos + 4;
                    cpr_found = true;
                }
            }
            if (cpr_found)
            {
                DWORD written = 0;
                ::WriteFile(_input_write, "\x1b[1;1R", 6, &written, nullptr);
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
    HANDLE _input_write = nullptr;
    std::thread _thread;
    mutable std::mutex _mutex;
    std::condition_variable _cv;
    std::string _output;
    size_t _cpr_scan_offset = 0;
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
        // 使用 PSEUDOCONSOLE_INHERIT_CURSOR：与 Windows Terminal 一致，corehost
        // 会发送 DSR CPR 查询并继承终端光标（cursor_valid=true），输入 echo 与
        // Enter 换行处理才走完整路径。
        const HRESULT hr = ConptyCreatePseudoConsole(size, pty_input_read.get(), pty_output_write.get(),
                                                     PSEUDOCONSOLE_INHERIT_CURSOR, &hpc);
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

        reader = std::make_unique<terminal_reader>(std::move(output_read), input_write.get());
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

// ── VT 终端模拟器（仅跟踪光标）──
// 模拟 Windows Terminal 的 VT 渲染语义：
//   - LF (\n) 只下移一行，X 不变（关键差异点！conhost 输出 CRLF，裸 LF 不回车）
//   - CR 归零 X；CRLF 到下一行行首
//   - CUP \x1b[r;cH 绝对定位（1-based）
//   - 可打印字符推进 X，到列尾自动 wrap
struct vt_terminal_tracker
{
    int row = 0; // 0-based
    int col = 0;
    int width = 120;

    void feed(char c)
    {
        if (_in_csi)
        {
            if (c >= 0x40 && c <= 0x7e)
            {
                // final byte：只处理 CUP（H/f）
                _in_csi = false;
                if (c == 'H' || c == 'f')
                {
                    int r = 1, cc = 1;
                    if (!_csi_params.empty())
                    {
                        r = std::atoi(_csi_params.c_str());
                        auto semi = _csi_params.find(';');
                        if (semi != std::string::npos)
                            cc = std::atoi(_csi_params.c_str() + semi + 1);
                    }
                    row = (r > 0 ? r - 1 : 0);
                    col = (cc > 0 ? cc - 1 : 0);
                }
                _csi_params.clear();
            }
            else if (c == 0x1b)
            {
                // 嵌套 ESC：重新开始
                _in_csi = false;
                _csi_params.clear();
                _in_esc = true;
            }
            else
            {
                _csi_params.push_back(c);
            }
            return;
        }
        if (_in_esc)
        {
            _in_esc = false;
            if (c == '[')
            {
                _in_csi = true;
                _csi_params.clear();
            }
            // 其他单字符 ESC 序列（如 ESC M）与 OSC 前缀：忽略
            return;
        }
        if (c == 0x1b)
        {
            _in_esc = true;
        }
        else if (c == '\r')
        {
            col = 0;
        }
        else if (c == '\n')
        {
            // LF 只下移；Windows Terminal 不自动 CR
            ++row;
        }
        else if (c == 0x08 || c == 0x7f)
        {
            if (col > 0)
                --col;
        }
        else if (c >= 0x20)
        {
            ++col;
            if (col >= width)
            {
                col = 0;
                ++row;
            }
        }
    }

    void feed(std::string_view s)
    {
        for (char c : s)
            feed(c);
    }

    // 解析到指定字节偏移处，返回该处的光标位置
    void advance_to(std::string_view s, size_t offset)
    {
        feed(s.substr(0, offset));
    }

  private:
    bool _in_esc = false;
    bool _in_csi = false;
    std::string _csi_params;
};

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

    // ── VT 终端模拟：跟踪光标，验证错误消息确实在新行 ──
    // 关键：Windows Terminal 中裸 LF 只下移不回车。若 corehost 把 Enter 的
    // 换行 echo 透传为裸 \n 且之后错误消息没有 CUP，错误消息会显示在
    // hello 的同一行（列 56 处），而不是新行。
    // 错误消息格式差异：powershell 5.1 是 "hello : The term"，pwsh 7 是
    // "hello: \x1b[31;1mThe term"（SGR 分隔），统一匹配 "The term"。
    const auto echo_pos = output.find("hello");
    const auto err_pos = output.find("The term");
    bool found_echo = echo_pos != std::string::npos;
    bool found_err = err_pos != std::string::npos;

    vt_terminal_tracker tracker;
    int echo_row = -1, echo_col = -1, err_row = -1, err_col = -1;
    if (found_echo && found_err && echo_pos < err_pos)
    {
        tracker.advance_to(output, echo_pos);
        echo_row = tracker.row;
        echo_col = tracker.col;
        // 继续到错误消息文本开头
        tracker.feed(output.substr(echo_pos, err_pos - echo_pos));
        err_row = tracker.row;
        err_col = tracker.col;
    }
    std::printf("=== vt-trace: echo@(r=%d,c=%d) err@(r=%d,c=%d) ===\n", echo_row, echo_col, err_row, err_col);

    // 分析断言：
    // 1. 错误消息应从新行开始：错误消息起始行必须大于 hello 回显所在行
    // 2. 错误消息应为红色（兼容 \x1b[31m / \x1b[31;1m / \x1b[0;91;40m /
    //    38;5;9 / 38;2;255;0;0 等红色 SGR 写法）
    const bool has_newline = output.find("\r\n") != std::string::npos || output.find("\n") != std::string::npos;
    const bool on_new_line = found_echo && found_err && echo_pos < err_pos && err_row > echo_row;
    const bool has_red =
        output.find("\x1b[31m") != std::string::npos || output.find("\x1b[31;") != std::string::npos ||
        output.find("\x1b[91m") != std::string::npos || output.find("\x1b[91;") != std::string::npos ||
        output.find("\x1b[0;31") != std::string::npos || output.find("\x1b[0;91") != std::string::npos ||
        output.find("\x1b[38;5;9m") != std::string::npos || output.find("\x1b[38;2;255;0;0m") != std::string::npos;
    const bool has_blue =
        output.find("\x1b[34m") != std::string::npos || output.find("\x1b[34;") != std::string::npos ||
        output.find("\x1b[94m") != std::string::npos || output.find("\x1b[94;") != std::string::npos ||
        output.find("\x1b[0;34") != std::string::npos || output.find("\x1b[0;94") != std::string::npos ||
        output.find("\x1b[38;5;4m") != std::string::npos || output.find("\x1b[38;5;12m") != std::string::npos;

    std::printf("=== analysis: newline=%d on_new_line=%d red=%d blue=%d ===\n", has_newline, on_new_line, has_red,
                has_blue);

    int failures = 0;
    if (!has_newline)
    {
        std::printf("FAIL: no newline in error output\n");
        ++failures;
    }
    if (!on_new_line)
    {
        std::printf("FAIL: error output starts on same row as 'hello' echo (not on a new line)\n");
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
