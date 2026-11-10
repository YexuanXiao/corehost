// tests/console_probe.cpp
// 测试用控制台探针：在被测 ConPTY 会话里以"真实客户端"身份运行。
//
//   --ctrl-probe   保持 ENABLE_PROCESSED_INPUT 后长眠，等待 Ctrl+C 由 CSRSS 结束。
//   --serve-probe  主线程阻塞在 ReadConsole，另一线程查询光标；用于验证宿主在
//                  读请求挂起期间仍服务其它 ConDrv 请求（否则控制台查询会卡住）。
#include <windows.h>

#include <cstdio>
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

} // namespace

int wmain(int argc, wchar_t **argv)
{
    if (argc != 2)
    {
        ::fwprintf(stderr, L"usage: console_probe --ctrl-probe | --serve-probe\n");
        return 2;
    }

    if (::wcscmp(argv[1], L"--ctrl-probe") == 0)
        return ctrl_probe();
    if (::wcscmp(argv[1], L"--serve-probe") == 0)
        return serve_probe();

    ::fwprintf(stderr, L"unknown mode\n");
    return 2;
}
