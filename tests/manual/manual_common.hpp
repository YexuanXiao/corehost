// manual_common.hpp — shared helpers for small manual console checks.
#pragma once
#include <windows.h>
#include <cstdio>
#include <cwchar>

inline HANDLE out_handle()
{
    return ::GetStdHandle(STD_OUTPUT_HANDLE);
}

inline HANDLE in_handle()
{
    return ::GetStdHandle(STD_INPUT_HANDLE);
}

inline void wprint(const wchar_t *fmt, ...)
{
    wchar_t buffer[4096]{};
    va_list args;
    va_start(args, fmt);
    const int count = _vsnwprintf_s(buffer, _TRUNCATE, fmt, args);
    va_end(args);

    if (count > 0)
    {
        DWORD written = 0;
        ::WriteConsoleW(out_handle(), buffer, static_cast<DWORD>(count), &written, nullptr);
    }
}

inline void vt(const wchar_t *seq)
{
    wprint(L"%s", seq);
}

inline void enable_vt()
{
    DWORD mode = 0;
    if (::GetConsoleMode(out_handle(), &mode))
    {
        mode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
        mode |= DISABLE_NEWLINE_AUTO_RETURN;
        ::SetConsoleMode(out_handle(), mode);
    }
}

inline void clear_screen()
{
    vt(L"\x1b[2J\x1b[H");
}

inline void section(const wchar_t *name, const wchar_t *what)
{
    clear_screen();
    wprint(L"\x1b[1;36m%s\x1b[0m\n", name);
    wprint(L"%s\n", what);
    wprint(L"\x1b[37m------------------------------------------------------------\x1b[0m\n");
}

inline void check_line(const wchar_t *label, const wchar_t *expected)
{
    wprint(L"\n\x1b[1;33m观察:\x1b[0m %s\n", label);
    wprint(L"\x1b[1;33m期望:\x1b[0m %s\n", expected);
}

inline void pause_briefly()
{
    wprint(L"\n3 秒后退出。\n");
    ::Sleep(3000);
}

inline void init_manual_console()
{
    enable_vt();
    ::SetConsoleOutputCP(CP_UTF8);
    ::SetConsoleCP(CP_UTF8);
}
