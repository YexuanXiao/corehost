// tests/console_probe.cpp
// 测试用控制台探针：在被测 ConPTY 会话里以"真实客户端"身份运行。
//
//   --ctrl-probe   保持 ENABLE_PROCESSED_INPUT 后长眠，等待 Ctrl+C 由 CSRSS 结束。
//   --serve-probe  主线程阻塞在 ReadConsole，另一线程查询光标；用于验证宿主在
//                  读请求挂起期间仍服务其它 ConDrv 请求（否则控制台查询会卡住）。
#include <windows.h>

#include <cstdio>
#include <cstdarg>
#include <cstdlib>
#include <string>
#include <cwchar>
#include <cstring>
#include <cstring>

namespace
{

DWORD WINAPI query_thread(LPVOID)
{
    // 给主线程一点时间进入 ReadConsole。
    ::Sleep(200);

    CONSOLE_SCREEN_BUFFER_INFO info{};
    if (::GetConsoleScreenBufferInfo(::GetStdHandle(STD_OUTPUT_HANDLE), &info) == FALSE)
    {
        constexpr char failed[] = "QUERY-FAILED\r\n";
        DWORD written = 0;
        ::WriteFile(::GetStdHandle(STD_OUTPUT_HANDLE), failed, sizeof(failed) - 1, &written, nullptr);
        return 1;
    }

    constexpr char ok[] = "QUERY-OK\r\n";
    DWORD written = 0;
    ::WriteFile(::GetStdHandle(STD_OUTPUT_HANDLE), ok, sizeof(ok) - 1, &written, nullptr);
    return 0;
}

int ctrl_probe()
{
    // 关键：显式要求 processed input，Ctrl+C 才会被转成 CTRL_C_EVENT。
    DWORD mode = 0;
    if (::GetConsoleMode(::GetStdHandle(STD_INPUT_HANDLE), &mode) == FALSE)
        return 3;
    mode |= ENABLE_PROCESSED_INPUT | ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT;
    if (::SetConsoleMode(::GetStdHandle(STD_INPUT_HANDLE), mode) == FALSE)
        return 4;

    constexpr char ready[] = "CTRL-PROBE-READY\r\n";
    DWORD written = 0;
    ::WriteFile(::GetStdHandle(STD_OUTPUT_HANDLE), ready, sizeof(ready) - 1, &written, nullptr);

    // 不再读输入：如果 Ctrl+C 被正确投递，进程会被 CSRSS 结束。
    ::Sleep(120000);
    return 0;
}

int serve_probe()
{
    const HANDLE thread = ::CreateThread(nullptr, 0, &query_thread, nullptr, 0, nullptr);
    if (thread == nullptr)
        return 5;
    ::CloseHandle(thread);

    char buffer[64]{};
    DWORD read = 0;
    // 阻塞在这里：宿主必须仍能服务查询线程的 ConDrv 请求。
    ::ReadConsoleA(::GetStdHandle(STD_INPUT_HANDLE), buffer, sizeof(buffer), &read, nullptr);
    return 0;
}

DWORD WINAPI read_line_thread(LPVOID)
{
    char buffer[64]{};
    DWORD read = 0;
    if (::ReadConsoleA(::GetStdHandle(STD_INPUT_HANDLE), buffer, sizeof(buffer), &read, nullptr) != FALSE)
    {
        constexpr char returned[] = "READ-RETURNED\r\n";
        DWORD written = 0;
        ::WriteFile(::GetStdHandle(STD_OUTPUT_HANDLE), returned, sizeof(returned) - 1, &written, nullptr);
    }
    return 0;
}

// 主线程写一个 Enter 键记录，应结束挂起的行读（libuv 的 raw 模式切换方式）。
int wake_probe()
{
    const HANDLE thread = ::CreateThread(nullptr, 0, &read_line_thread, nullptr, 0, nullptr);
    if (thread == nullptr)
        return 6;

    ::Sleep(300);

    INPUT_RECORD record{};
    record.EventType = KEY_EVENT;
    record.Event.KeyEvent.bKeyDown = TRUE;
    record.Event.KeyEvent.wRepeatCount = 1;
    record.Event.KeyEvent.wVirtualKeyCode = VK_RETURN;
    record.Event.KeyEvent.uChar.AsciiChar = '\r';
    record.Event.KeyEvent.uChar.UnicodeChar = L'\r';

    DWORD written = 0;
    const BOOL wrote = ::WriteConsoleInputW(::GetStdHandle(STD_INPUT_HANDLE), &record, 1, &written);
    {
        char marker[64]{};
        ::sprintf_s(marker, "WAKE-WRITE ok=%d written=%lu err=%lu\r\n", static_cast<int>(wrote),
                    static_cast<unsigned long>(written), static_cast<unsigned long>(::GetLastError()));
        DWORD out_written = 0;
        ::WriteFile(::GetStdHandle(STD_OUTPUT_HANDLE), marker, static_cast<DWORD>(::strlen(marker)), &out_written,
                    nullptr);
    }
    if (wrote == FALSE || written != 1)
    {
        ::CloseHandle(thread);
        return 7;
    }

    ::WaitForSingleObject(thread, 10000);
    ::CloseHandle(thread);
    return 0;
}

} // namespace

// VT 输入程序（如 ssh.exe）应收到终端原始字符：方向键是 ESC [ A 三字符，
// Backspace 是自身字节，而不是解码后的 VK_UP/VK_BACK。
int vt_probe()
{
    const HANDLE input = ::GetStdHandle(STD_INPUT_HANDLE);
    DWORD mode = 0;
    if (::GetConsoleMode(input, &mode) == FALSE)
        return 8;
    if (::SetConsoleMode(input, ENABLE_VIRTUAL_TERMINAL_INPUT) == FALSE)
        return 9;

    constexpr char ready[] = "VT-PROBE-READY\r\n";
    DWORD written = 0;
    ::WriteFile(::GetStdHandle(STD_OUTPUT_HANDLE), ready, sizeof(ready) - 1, &written, nullptr);

    char hex[64]{};
    size_t used = 0;
    int collected = 0;
    while (collected < 4 && used + 4 < sizeof(hex))
    {
        INPUT_RECORD record{};
        DWORD got = 0;
        if (::ReadConsoleInputW(input, &record, 1, &got) == FALSE || got == 0)
            break;
        if (record.EventType != KEY_EVENT || record.Event.KeyEvent.bKeyDown == FALSE)
            continue;

        const auto ch = static_cast<unsigned char>(record.Event.KeyEvent.uChar.AsciiChar);
        const int n = ::sprintf_s(hex + used, sizeof(hex) - used, "%02x ", ch);
        if (n <= 0)
            break;
        used += static_cast<size_t>(n);
        ++collected;
    }

    char marker[128]{};
    ::sprintf_s(marker, "VT-GOT: %s\r\n", hex);
    ::WriteFile(::GetStdHandle(STD_OUTPUT_HANDLE), marker, static_cast<DWORD>(::strlen(marker)), &written, nullptr);
    return 0;
}

BOOL WINAPI ignore_ctrl_handler(DWORD event)
{
    (void)event;
    return TRUE; // 吞掉事件，进程存活，便于观察 ReadConsole 的返回
}

// 把一行结果追加到文件（用于跨控制台比较：conhost 参考实现读不到管道）。
void append_result_file(const wchar_t *path, const char *text)
{
    if (path == nullptr || path[0] == 0)
        return;
    FILE *file = nullptr;
    if (::_wfopen_s(&file, path, L"ab") != 0 || file == nullptr)
        return;
    std::fwrite(text, 1, std::strlen(text), file);
    std::fclose(file);
}

// Ctrl+C 对齐 conhost 的语义：等待中的 ReadConsole 以 STATUS_ALERTED 结束
// （客户端以 0 字节返回并设置 ERROR_OPERATION_ABORTED），而不是收到一个按键。
int ctrl_read_probe(const wchar_t *result_file)
{
    ::SetConsoleCtrlHandler(&ignore_ctrl_handler, TRUE);

    const HANDLE input = ::GetStdHandle(STD_INPUT_HANDLE);
    DWORD mode = 0;
    if (::GetConsoleMode(input, &mode) == FALSE)
        return 10;
    mode |= ENABLE_PROCESSED_INPUT | ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT;
    if (::SetConsoleMode(input, mode) == FALSE)
        return 11;

    constexpr char ready[] = "CTRL-READ-READY\r\n";
    DWORD written = 0;
    ::WriteFile(::GetStdHandle(STD_OUTPUT_HANDLE), ready, sizeof(ready) - 1, &written, nullptr);
    append_result_file(result_file, ready);

    char buffer[64]{};
    DWORD read = 0;
    ::SetLastError(0);
    const BOOL ok = ::ReadConsoleA(input, buffer, sizeof(buffer), &read, nullptr);
    const DWORD error = ::GetLastError();

    char marker[128]{};
    ::sprintf_s(marker, "CTRL-READ-RESULT ok=%d read=%lu err=%lu\r\n", static_cast<int>(ok),
                static_cast<unsigned long>(read), static_cast<unsigned long>(error));
    ::WriteFile(::GetStdHandle(STD_OUTPUT_HANDLE), marker, static_cast<DWORD>(::strlen(marker)), &written, nullptr);
    append_result_file(result_file, marker);
    return 0;
}


HANDLE g_handler_event = nullptr;
volatile ULONG g_handler_code = 0;

wchar_t g_close_marker_path[MAX_PATH]{};

BOOL WINAPI marker_event_handler(DWORD event)
{
    // 处理器在独立线程执行：只记录并置事件，文件由主线程写，避免与进程退出竞争。
    g_handler_code = event;
    if (g_handler_event != nullptr)
        ::SetEvent(g_handler_event);
    return TRUE; // 处理掉事件，进程存活以便观察
}

// 通用事件探针：把收到的控制台事件码写入文件，便于验证投递路径。
int handler_probe(const wchar_t *marker_path)
{
    if (marker_path == nullptr || marker_path[0] == 0)
        return 13;
    ::wcsncpy_s(g_close_marker_path, marker_path, _TRUNCATE);
    ::DeleteFileW(g_close_marker_path);
    g_handler_event = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (g_handler_event == nullptr)
        return 20;
    ::SetConsoleCtrlHandler(&marker_event_handler, TRUE);

    constexpr char ready[] = "HANDLER-PROBE-READY\r\n";
    DWORD written = 0;
    ::WriteFile(::GetStdHandle(STD_OUTPUT_HANDLE), ready, sizeof(ready) - 1, &written, nullptr);

    char buffer[32]{};
    DWORD read = 0;
    ::ReadConsoleA(::GetStdHandle(STD_INPUT_HANDLE), buffer, sizeof(buffer), &read, nullptr);

    const bool handled = ::WaitForSingleObject(g_handler_event, 10000) == WAIT_OBJECT_0;
    FILE *file = nullptr;
    if (::_wfopen_s(&file, g_close_marker_path, L"wb") == 0 && file != nullptr)
    {
        char line[48]{};
        if (handled)
            ::sprintf_s(line, "EVENT=%lu\r\n", static_cast<unsigned long>(g_handler_code));
        else
            ::strcpy_s(line, "EVENT=none\r\n");
        std::fwrite(line, 1, std::strlen(line), file);
        std::fclose(file);
    }
    return 0;
}

// VT 输入模式下，终端 resize 应产生 WINDOW_BUFFER_SIZE_EVENT。
int size_event_probe()
{
    const HANDLE input = ::GetStdHandle(STD_INPUT_HANDLE);
    DWORD mode = 0;
    if (::GetConsoleMode(input, &mode) == FALSE)
        return 14;
    if (::SetConsoleMode(input, mode | ENABLE_VIRTUAL_TERMINAL_INPUT) == FALSE)
        return 15;

    constexpr char ready[] = "SIZE-PROBE-READY\r\n";
    DWORD written = 0;
    ::WriteFile(::GetStdHandle(STD_OUTPUT_HANDLE), ready, sizeof(ready) - 1, &written, nullptr);

    for (;;)
    {
        INPUT_RECORD record{};
        DWORD read = 0;
        if (::ReadConsoleInputW(input, &record, 1, &read) == FALSE || read != 1)
            return 16;
        if (record.EventType != WINDOW_BUFFER_SIZE_EVENT)
            continue;

        char marker[96]{};
        ::sprintf_s(marker, "SIZE-EVENT=%dx%d\r\n", record.Event.WindowBufferSizeEvent.dwSize.X,
                    record.Event.WindowBufferSizeEvent.dwSize.Y);
        ::WriteFile(::GetStdHandle(STD_OUTPUT_HANDLE), marker, static_cast<DWORD>(::strlen(marker)), &written,
                    nullptr);
        return 0;
    }
}

// SGR 1006 鼠标在非 VT 输入模式下应转成 MOUSE_EVENT。连续读 3 条记录后退出。
int mouse_probe()
{
    const HANDLE input = ::GetStdHandle(STD_INPUT_HANDLE);
    DWORD mode = 0;
    if (::GetConsoleMode(input, &mode) == FALSE)
        return 17;
    if (::SetConsoleMode(input, mode | ENABLE_MOUSE_INPUT) == FALSE)
        return 18;

    constexpr char ready[] = "MOUSE-PROBE-READY\r\n";
    DWORD written = 0;
    ::WriteFile(::GetStdHandle(STD_OUTPUT_HANDLE), ready, sizeof(ready) - 1, &written, nullptr);

    int seen = 0;
    while (seen < 3)
    {
        INPUT_RECORD record{};
        DWORD read = 0;
        if (::ReadConsoleInputW(input, &record, 1, &read) == FALSE || read != 1)
            return 19;
        if (record.EventType != MOUSE_EVENT)
            continue;

        char marker[128]{};
        ::sprintf_s(marker, "MOUSE x=%d y=%d buttons=0x%08lx flags=0x%08lx\r\n",
                    record.Event.MouseEvent.dwMousePosition.X, record.Event.MouseEvent.dwMousePosition.Y,
                    static_cast<unsigned long>(record.Event.MouseEvent.dwButtonState),
                    static_cast<unsigned long>(record.Event.MouseEvent.dwEventFlags));
        ::WriteFile(::GetStdHandle(STD_OUTPUT_HANDLE), marker, static_cast<DWORD>(::strlen(marker)), &written,
                    nullptr);
        ++seen;
    }
    return 0;
}

// 差分测试用：调用一组稳定的控制台 API，把结果写成 JSON（单行）。
int api_probe(const wchar_t *output_path)
{
    if (output_path == nullptr || output_path[0] == 0)
        return 21;

    const HANDLE input = ::GetStdHandle(STD_INPUT_HANDLE);
    const HANDLE output = ::GetStdHandle(STD_OUTPUT_HANDLE);

    DWORD input_mode = 0;
    DWORD output_mode = 0;
    ::GetConsoleMode(input, &input_mode);
    ::GetConsoleMode(output, &output_mode);

    CONSOLE_SCREEN_BUFFER_INFO before{};
    ::GetConsoleScreenBufferInfo(output, &before);

    ::SetConsoleTextAttribute(output, 0x1E);
    CONSOLE_SCREEN_BUFFER_INFO after_attr{};
    ::GetConsoleScreenBufferInfo(output, &after_attr);

    CONSOLE_CURSOR_INFO cursor{50, TRUE};
    ::SetConsoleCursorInfo(output, &cursor);
    CONSOLE_CURSOR_INFO read_back{};
    ::GetConsoleCursorInfo(output, &read_back);

    ::SetConsoleTitleW(L"diff-title-42");
    wchar_t title[128]{};
    DWORD title_length = ::GetConsoleTitleW(title, ARRAYSIZE(title));

    const HWND window = ::GetConsoleWindow();

    FILE *file = nullptr;
    if (::_wfopen_s(&file, output_path, L"wb") != 0 || file == nullptr)
        return 22;
    std::fprintf(file,
                 "{\"in_mode\":%lu,\"out_mode\":%lu,\"size_x\":%d,\"size_y\":%d,\"cursor_x\":%d,\"cursor_y\":%d,"
                 "\"attr_before\":%u,\"attr_after\":%u,\"cursor_size\":%lu,\"cursor_visible\":%lu,"
                 "\"title\":\"%ls\",\"title_len\":%lu,\"has_window\":%d}\n",
                 static_cast<unsigned long>(input_mode), static_cast<unsigned long>(output_mode), before.dwSize.X,
                 before.dwSize.Y, before.dwCursorPosition.X, before.dwCursorPosition.Y, before.wAttributes,
                 after_attr.wAttributes, static_cast<unsigned long>(read_back.dwSize),
                 static_cast<unsigned long>(read_back.bVisible), title, static_cast<unsigned long>(title_length),
                 window != nullptr ? 1 : 0);
    std::fclose(file);
    return 0;
}

// 把一行结果写到文件（CSRSS 事件线程与主线程的竞争已在 handler_probe 处理过，
// 这些探针只在主线程写文件）。
void write_text_file(const wchar_t *path, const char *text)
{
    if (path == nullptr || path[0] == 0)
        return;
    FILE *file = nullptr;
    if (::_wfopen_s(&file, path, L"wb") != 0 || file == nullptr)
        return;
    std::fwrite(text, 1, std::strlen(text), file);
    std::fclose(file);
}

// raw ReadFile 遇 Ctrl+C：STATUS_ALERTED → 0 字节 + ERROR_OPERATION_ABORTED。
// 注意：系统 conhost 在 ConPTY 下不会让这里挂起的读因 Ctrl+C 返回（探针会
// 一直阻塞）；corehost 以 STATUS_ALERTED 结束并返回 0 字节/0 记录。
// 复现见 test_conpty_differential.cpp 的 raw_ctrl / input_ctrl 场景（G24）。
int raw_read_probe(const wchar_t *result_path)
{
    if (result_path == nullptr || result_path[0] == 0)
        return 23;
    ::SetConsoleCtrlHandler(&ignore_ctrl_handler, TRUE);
    DWORD mode = ENABLE_PROCESSED_INPUT; // 关掉 LINE/ECHO，走 RAW_READ
    ::SetConsoleMode(::GetStdHandle(STD_INPUT_HANDLE), mode);

    constexpr char ready[] = "RAW-PROBE-READY\r\n";
    DWORD written = 0;
    ::WriteFile(::GetStdHandle(STD_OUTPUT_HANDLE), ready, sizeof(ready) - 1, &written, nullptr);

    char buffer[64]{};
    DWORD read = 0;
    ::SetLastError(0);
    const BOOL ok = ::ReadFile(::GetStdHandle(STD_INPUT_HANDLE), buffer, sizeof(buffer), &read, nullptr);
    const DWORD error = ::GetLastError();
    char marker[96]{};
    ::sprintf_s(marker, "RAW ok=%d read=%lu err=%lu\r\n", static_cast<int>(ok), static_cast<unsigned long>(read),
                static_cast<unsigned long>(error));
    write_text_file(result_path, marker);
    return 0;
}

// ReadConsoleInput 遇 Ctrl+C：0 条记录 + ERROR_OPERATION_ABORTED。
// 注意：系统 conhost 在 ConPTY 下不会让这里挂起的读因 Ctrl+C 返回（探针会
// 一直阻塞）；corehost 以 STATUS_ALERTED 结束并返回 0 字节/0 记录。
// 复现见 test_conpty_differential.cpp 的 raw_ctrl / input_ctrl 场景（G24）。
int input_record_probe(const wchar_t *result_path)
{
    if (result_path == nullptr || result_path[0] == 0)
        return 24;
    ::SetConsoleCtrlHandler(&ignore_ctrl_handler, TRUE);

    constexpr char ready[] = "INPUT-PROBE-READY\r\n";
    DWORD written = 0;
    ::WriteFile(::GetStdHandle(STD_OUTPUT_HANDLE), ready, sizeof(ready) - 1, &written, nullptr);

    INPUT_RECORD record{};
    DWORD read = 0;
    ::SetLastError(0);
    const BOOL ok = ::ReadConsoleInputW(::GetStdHandle(STD_INPUT_HANDLE), &record, 1, &read);
    const DWORD error = ::GetLastError();
    char marker[96]{};
    ::sprintf_s(marker, "INPUT ok=%d read=%lu err=%lu\r\n", static_cast<int>(ok), static_cast<unsigned long>(read),
                static_cast<unsigned long>(error));
    write_text_file(result_path, marker);
    return 0;
}

// 未开 processed input 时 Ctrl+C 应作为按键到达（VK_C / uc=3），不是控制事件。
int no_processed_probe(const wchar_t *result_path)
{
    if (result_path == nullptr || result_path[0] == 0)
        return 25;
    DWORD mode = 0;
    ::GetConsoleMode(::GetStdHandle(STD_INPUT_HANDLE), &mode);
    ::SetConsoleMode(::GetStdHandle(STD_INPUT_HANDLE), mode & ~ENABLE_PROCESSED_INPUT);

    constexpr char ready[] = "NOPROC-PROBE-READY\r\n";
    DWORD written = 0;
    ::WriteFile(::GetStdHandle(STD_OUTPUT_HANDLE), ready, sizeof(ready) - 1, &written, nullptr);

    INPUT_RECORD record{};
    DWORD read = 0;
    if (::ReadConsoleInputW(::GetStdHandle(STD_INPUT_HANDLE), &record, 1, &read) == FALSE || read != 1)
        return 26;
    if (record.EventType != KEY_EVENT)
        return 27;

    char marker[96]{};
    ::sprintf_s(marker, "KEY vk=%u uc=%u\r\n", static_cast<unsigned>(record.Event.KeyEvent.wVirtualKeyCode),
                static_cast<unsigned>(record.Event.KeyEvent.uChar.UnicodeChar));
    write_text_file(result_path, marker);
    return 0;
}

// GenerateConsoleCtrlEvent(CTRL_C_EVENT, 0) 应把事件投递给控制台上的进程。
int api_ctrl_probe(const wchar_t *result_path)
{
    if (result_path == nullptr || result_path[0] == 0)
        return 28;
    const HANDLE event = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (event == nullptr)
        return 29;
    // marker_event_handler 会 SetEvent；ignore_ctrl_handler 只吞事件不通知。
    ::SetConsoleCtrlHandler(&marker_event_handler, TRUE);
    g_handler_event = event;

    constexpr char ready[] = "APICTRL-PROBE-READY\r\n";
    DWORD written = 0;
    ::WriteFile(::GetStdHandle(STD_OUTPUT_HANDLE), ready, sizeof(ready) - 1, &written, nullptr);

    ::SetLastError(0);
    const BOOL sent = ::GenerateConsoleCtrlEvent(CTRL_C_EVENT, 0);
    const DWORD error = ::GetLastError();
    const bool delivered = ::WaitForSingleObject(event, 5000) == WAIT_OBJECT_0;
    char marker[112]{};
    ::sprintf_s(marker, "APICTRL sent=%d err=%lu delivered=%d\r\n", static_cast<int>(sent),
                static_cast<unsigned long>(error), delivered ? 1 : 0);
    write_text_file(result_path, marker);
    return 0;
}

// ── 脚本化控制台客户端（W9）─────────────────────────────
// 每条指令一行，结果以 "[script] ..." 写回 stdout，供测试断言。
// 指令：write/settitle/gettitle/sbi/getmode/window/msg/expect/sleep/exit。
// 屏幕回读在脚本 expect 与 --screen-read-probe 中复用；定义在文件后部。
std::wstring read_screen_rows();

void script_print(const char *format, ...)
{
    char buffer[512]{};
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    DWORD written = 0;
    ::WriteFile(::GetStdHandle(STD_OUTPUT_HANDLE), buffer, static_cast<DWORD>(std::strlen(buffer)), &written,
                nullptr);
}

std::wstring utf8_to_wide(const std::string &text)
{
    if (text.empty())
        return {};
    const int needed = ::MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring result(static_cast<size_t>(needed), L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), result.data(), needed);
    return result;
}

bool script_msg(const std::wstring &name)
{
    const HWND window = ::GetConsoleWindow();
    if (window == nullptr)
    {
        script_print("[script] msg %ls window=0\n", name.c_str());
        return true;
    }

    DWORD_PTR result = 0;
    if (name == L"gettextlength")
        ::SendMessageTimeoutW(window, WM_GETTEXTLENGTH, 0, 0, SMTO_ABORTIFHUNG, 2000, &result);
    else if (name == L"gettext")
    {
        wchar_t text[128]{};
        ::SendMessageTimeoutW(window, WM_GETTEXT, ARRAYSIZE(text), reinterpret_cast<LPARAM>(text), SMTO_ABORTIFHUNG,
                              2000, &result);
        script_print("[script] msg gettext \"%ls\"\n", text);
        return true;
    }
    else if (name == L"showon")
        ::SendMessageTimeoutW(window, WM_SHOWWINDOW, TRUE, 0, SMTO_ABORTIFHUNG, 2000, &result);
    else if (name == L"showoff")
        ::SendMessageTimeoutW(window, WM_SHOWWINDOW, FALSE, 0, SMTO_ABORTIFHUNG, 2000, &result);
    else if (name == L"minimize")
        ::SendMessageTimeoutW(window, WM_SIZE, SIZE_MINIMIZED, 0, SMTO_ABORTIFHUNG, 2000, &result);
    else if (name == L"restore")
        ::SendMessageTimeoutW(window, WM_SIZE, SIZE_RESTORED, 0, SMTO_ABORTIFHUNG, 2000, &result);
    else
        return false;

    script_print("[script] msg %ls result=%llu\n", name.c_str(), static_cast<unsigned long long>(result));
    return true;
}

int run_script(const wchar_t *script_path)
{
    if (script_path == nullptr || script_path[0] == 0)
        return 30;
    FILE *file = nullptr;
    if (::_wfopen_s(&file, script_path, L"rb") != 0 || file == nullptr)
        return 31;

    int exit_code = 0;
    char line[512]{};
    while (std::fgets(line, sizeof(line), file) != nullptr)
    {
        std::string command{line};
        while (!command.empty() && (command.back() == '\n' || command.back() == '\r'))
            command.pop_back();
        if (command.empty() || command.front() == '#')
            continue;
        const size_t space = command.find(' ');
        const std::string name = command.substr(0, space);
        const std::string rest = space == std::string::npos ? std::string{} : command.substr(space + 1);

        if (name == "write")
        {
            const auto text = utf8_to_wide(rest);
            DWORD written = 0;
            ::WriteConsoleW(::GetStdHandle(STD_OUTPUT_HANDLE), text.c_str(), static_cast<DWORD>(text.size()), &written,
                            nullptr);
            script_print("[script] write ok\n");
        }
        else if (name == "settitle")
        {
            ::SetConsoleTitleW(utf8_to_wide(rest).c_str());
            script_print("[script] settitle ok\n");
        }
        else if (name == "gettitle")
        {
            wchar_t title[128]{};
            ::GetConsoleTitleW(title, ARRAYSIZE(title));
            script_print("[script] gettitle \"%ls\"\n", title);
        }
        else if (name == "sbi")
        {
            CONSOLE_SCREEN_BUFFER_INFO info{};
            if (::GetConsoleScreenBufferInfo(::GetStdHandle(STD_OUTPUT_HANDLE), &info) != FALSE)
                script_print("[script] sbi size=%dx%d cursor=%d,%d attr=0x%04X\n", info.dwSize.X, info.dwSize.Y,
                             info.dwCursorPosition.X, info.dwCursorPosition.Y, info.wAttributes);
            else
                script_print("[script] sbi failed\n");
        }
        else if (name == "getmode")
        {
            DWORD in_mode = 0;
            DWORD out_mode = 0;
            ::GetConsoleMode(::GetStdHandle(STD_INPUT_HANDLE), &in_mode);
            ::GetConsoleMode(::GetStdHandle(STD_OUTPUT_HANDLE), &out_mode);
            script_print("[script] getmode in=0x%08lX out=0x%08lX\n", static_cast<unsigned long>(in_mode),
                         static_cast<unsigned long>(out_mode));
        }
        else if (name == "window")
        {
            const HWND window = ::GetConsoleWindow();
            script_print("[script] window valid=%d visible=%d\n", window != nullptr ? 1 : 0,
                         window != nullptr && ::IsWindowVisible(window) ? 1 : 0);
        }
        else if (name == "msg")
        {
            if (!script_msg(utf8_to_wide(rest)))
                script_print("[script] msg %s unsupported\n", rest.c_str());
        }
        else if (name == "expect")
        {
            const auto screen_text = read_screen_rows();
            const auto needle = utf8_to_wide(rest);
            const bool found = screen_text.find(needle) != std::wstring::npos;
            script_print("[script] expect \"%s\" ok=%d\n", rest.c_str(), found ? 1 : 0);
        }
        else if (name == "sleep")
            ::Sleep(static_cast<DWORD>(std::strtoul(rest.c_str(), nullptr, 10)));
        else if (name == "exit")
        {
            exit_code = static_cast<int>(std::strtol(rest.c_str(), nullptr, 10));
            break;
        }
        else
            script_print("[script] unknown %s\n", name.c_str());
    }
    std::fclose(file);
    return exit_code;
}

// 用 ReadConsoleOutputCharacterW 逐行回读可见屏幕。容量按 SBI 的可见窗口计算，
// 不假设固定宽度；这是 G28 崩溃的原始调用形态，现在应安全返回。
std::wstring read_screen_rows()
{
    const HANDLE output = ::GetStdHandle(STD_OUTPUT_HANDLE);
    CONSOLE_SCREEN_BUFFER_INFO info{};
    if (::GetConsoleScreenBufferInfo(output, &info) == FALSE)
        return {};
    const SHORT width = static_cast<SHORT>(info.srWindow.Right - info.srWindow.Left + 1);
    if (width <= 0)
        return {};
    std::wstring text;
    std::wstring row(static_cast<size_t>(width), L' ');
    for (SHORT y = info.srWindow.Top; y <= info.srWindow.Bottom; ++y)
    {
        DWORD chars = 0;
        if (::ReadConsoleOutputCharacterW(output, row.data(), width, COORD{info.srWindow.Left, y}, &chars) == FALSE)
            break;
        text.append(row, 0, chars);
        text.push_back(L'\n');
    }
    return text;
}

// G28 回归探针：逐行 ReadConsoleOutputCharacter + ReadConsoleOutput 矩形。
int screen_read_probe(const wchar_t *result_path)
{
    if (result_path == nullptr || result_path[0] == 0)
        return 32;
    const HANDLE output = ::GetStdHandle(STD_OUTPUT_HANDLE);
    constexpr wchar_t banner[] = L"SCREEN-READBACK\r\n";
    DWORD written = 0;
    ::WriteConsoleW(output, banner, ARRAYSIZE(banner) - 1, &written, nullptr);

    const auto screen = read_screen_rows();
    SMALL_RECT region{0, 0, 7, 2};
    CHAR_INFO cells[8 * 3]{};
    const BOOL region_ok = ::ReadConsoleOutputW(output, cells, COORD{8, 3}, COORD{0, 0}, &region);
    char marker[160]{};
    ::sprintf_s(marker, "SCREEN chars=%zu region=%d\r\n", screen.size(), region_ok ? 1 : 0);
    write_text_file(result_path, marker);
    return screen.empty() || region_ok == FALSE ? 33 : 0;
}

// CTRL_CLOSE_EVENT 处理器：必须自己写文件，因为 close 语义下 CSRSS 在处理器
// 返回后会终止进程，主线程可能来不及写。
int wmain(int argc, wchar_t **argv)
{
    if (argc < 2 || argc > 3)
    {
        ::fwprintf(stderr, L"usage: console_probe --ctrl-probe | --ctrl-read-probe | --serve-probe | --wake-probe | --vt-probe\n");
        return 2;
    }


    if (::wcscmp(argv[1], L"--ctrl-probe") == 0)
        return ctrl_probe();
    if (::wcscmp(argv[1], L"--serve-probe") == 0)
        return serve_probe();
    if (::wcscmp(argv[1], L"--wake-probe") == 0)
        return wake_probe();
    if (::wcscmp(argv[1], L"--raw-read-probe") == 0)
        return raw_read_probe(argc >= 3 ? argv[2] : nullptr);
    if (::wcscmp(argv[1], L"--input-record-probe") == 0)
        return input_record_probe(argc >= 3 ? argv[2] : nullptr);
    if (::wcscmp(argv[1], L"--no-processed-probe") == 0)
        return no_processed_probe(argc >= 3 ? argv[2] : nullptr);
    if (::wcscmp(argv[1], L"--api-ctrl-probe") == 0)
        return api_ctrl_probe(argc >= 3 ? argv[2] : nullptr);
    if (::wcscmp(argv[1], L"--screen-read-probe") == 0)
        return screen_read_probe(argc >= 3 ? argv[2] : nullptr);
    if (::wcscmp(argv[1], L"--script") == 0)
        return run_script(argc >= 3 ? argv[2] : nullptr);
    if (::wcscmp(argv[1], L"--api-probe") == 0)
        return api_probe(argc >= 3 ? argv[2] : nullptr);
    if (::wcscmp(argv[1], L"--mouse-probe") == 0)
        return mouse_probe();
    if (::wcscmp(argv[1], L"--size-event-probe") == 0)
        return size_event_probe();
    if (::wcscmp(argv[1], L"--handler-probe") == 0)
        return handler_probe(argc >= 3 ? argv[2] : nullptr);
    if (::wcscmp(argv[1], L"--ctrl-read-probe") == 0)
        return ctrl_read_probe(argc >= 3 ? argv[2] : nullptr);
    if (::wcscmp(argv[1], L"--vt-probe") == 0)
        return vt_probe();

    ::fwprintf(stderr, L"unknown mode\n");
    return 2;
}