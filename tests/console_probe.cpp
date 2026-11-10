// tests/console_probe.cpp
// 测试用控制台探针：在被测 ConPTY 会话里以"真实客户端"身份运行。
//
//   --ctrl-probe   保持 ENABLE_PROCESSED_INPUT 后长眠，等待 Ctrl+C 由 CSRSS 结束。
//   --serve-probe  主线程阻塞在 ReadConsole，另一线程查询光标；用于验证宿主在
//                  读请求挂起期间仍服务其它 ConDrv 请求（否则控制台查询会卡住）。
#include <windows.h>

#include <cstdio>
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

wchar_t g_close_marker_path[MAX_PATH]{};

BOOL WINAPI marker_event_handler(DWORD event)
{
    FILE *file = nullptr;
    if (::_wfopen_s(&file, g_close_marker_path, L"ab") == 0 && file != nullptr)
    {
        char line[32]{};
        ::sprintf_s(line, "EVENT=%lu\r\n", static_cast<unsigned long>(event));
        std::fwrite(line, 1, std::strlen(line), file);
        std::fclose(file);
    }
    return TRUE; // 处理掉事件，进程存活以便观察
}

// 通用事件探针：把收到的控制台事件码写入文件，便于验证投递路径。
int handler_probe(const wchar_t *marker_path)
{
    if (marker_path == nullptr || marker_path[0] == 0)
        return 13;
    ::wcsncpy_s(g_close_marker_path, marker_path, _TRUNCATE);
    ::DeleteFileW(g_close_marker_path);
    ::SetConsoleCtrlHandler(&marker_event_handler, TRUE);

    constexpr char ready[] = "HANDLER-PROBE-READY\r\n";
    DWORD written = 0;
    ::WriteFile(::GetStdHandle(STD_OUTPUT_HANDLE), ready, sizeof(ready) - 1, &written, nullptr);

    char buffer[32]{};
    DWORD read = 0;
    ::ReadConsoleA(::GetStdHandle(STD_INPUT_HANDLE), buffer, sizeof(buffer), &read, nullptr);
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
