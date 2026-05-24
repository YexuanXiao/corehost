// ── tools/echo_test.cpp ───────────────────────────────────
// 模拟 cmd.exe 的控制台测试程序
//
// 使用纯 Windows 控制台 API：
//   - WriteConsoleW 输出文本
//   - ReadConsoleW 等待用户输入（或 Ctrl+C/Break）
//   - GetConsoleTitle / SetConsoleTitle 测试
//
// 构建（独立编译，不链接 corehost）:
//   cl /EHsc /std:c++17 /Fe:build\Release\echo_test.exe tools\echo_test.cpp /link user32.lib

#include <windows.h>
#include <array>
#include <cstdio>
#include <ctime>
#include <cwchar>
#include <cstdarg>

// 与 corehost 相同的日志函数
inline void tlog(const wchar_t *fmt, ...)
{
    wchar_t buf[512];
    auto now = std::time(nullptr);
    auto tm = std::localtime(&now);
    int off = std::swprintf(buf, std::size(buf), L"[%02d:%02d:%02d] echo_test ", tm->tm_hour, tm->tm_min, tm->tm_sec);
    va_list va;
    va_start(va, fmt);
    std::vswprintf(buf + off, std::size(buf) - off, fmt, va);
    va_end(va);

    wchar_t path[MAX_PATH];
    if (::GetModuleFileNameW(nullptr, path, MAX_PATH))
    {
        wchar_t *last = nullptr;
        for (auto *p = path; *p; ++p)
            if (*p == L'\\' || *p == L'/')
                last = p;
        if (last)
            last[1] = L'\0';
        std::wcscat(path, L"echo_test.log");
    }

    auto *f = ::_wfsopen(path, L"a", _SH_DENYNO);
    if (f)
    {
        std::fputws(buf, f);
        std::fclose(f);
    }
}

int wmain()
{
    tlog(L"process start, pid=%lu", ::GetCurrentProcessId());

    HANDLE hIn = ::GetStdHandle(STD_INPUT_HANDLE);
    HANDLE hOut = ::GetStdHandle(STD_OUTPUT_HANDLE);
    tlog(L"stdin=%p stdout=%p", hIn, hOut);

    if (hIn == nullptr || hOut == nullptr || hIn == INVALID_HANDLE_VALUE || hOut == INVALID_HANDLE_VALUE)
    {
        tlog(L"ERROR: no console handles, exiting");
        return 1;
    }

    // 获取控制台标题
    wchar_t title[256];
    if (::GetConsoleTitleW(title, static_cast<DWORD>(std::size(title))))
    {
        tlog(L"console title: %ls", title);
    }
    else
    {
        tlog(L"GetConsoleTitle failed err=%lu", ::GetLastError());
    }

    // 获取控制台模式
    DWORD mode = 0;
    if (::GetConsoleMode(hIn, &mode))
    {
        tlog(L"console input mode=0x%lx", mode);
    }
    else
    {
        tlog(L"GetConsoleMode(stdin) failed err=%lu", ::GetLastError());
    }

    // 覆盖写 → 刷新 → 测试输出
    const wchar_t *msg1 = L"=== echo_test: Hello from Console API ===\r\n";
    DWORD w1 = 0;
    if (!::WriteConsoleW(hOut, msg1, static_cast<DWORD>(std::wcslen(msg1)), &w1, nullptr))
    {
        tlog(L"WriteConsole #1 failed err=%lu", ::GetLastError());
    }
    else
    {
        tlog(L"WriteConsole #1 OK wrote=%lu", w1);
    }

    const wchar_t *msg2 = L"Press Enter to continue, or Ctrl+C to exit...\r\n";
    DWORD w2 = 0;
    ::WriteConsoleW(hOut, msg2, static_cast<DWORD>(std::wcslen(msg2)), &w2, nullptr);
    tlog(L"WriteConsole #2 wrote=%lu", w2);

    // 读取一行
    wchar_t buf[256];
    DWORD read = 0;
    tlog(L"calling ReadConsoleW...");
    BOOL rc = ::ReadConsoleW(hIn, buf, static_cast<DWORD>(std::size(buf)), &read, nullptr);
    if (rc)
    {
        buf[read] = L'\0';
        tlog(L"ReadConsole OK read=%lu buf=%.*ls", read, static_cast<int>(read), buf);
    }
    else
    {
        tlog(L"ReadConsole failed err=%lu", ::GetLastError());
    }

    // 再看一遍标题
    title[0] = L'\0';
    if (::GetConsoleTitleW(title, static_cast<DWORD>(std::size(title))))
    {
        tlog(L"console title after read: %ls", title);
    }

    tlog(L"process exiting normally");
    return 0;
}
