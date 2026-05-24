// manual_common.hpp — 手动测试公共工具
// 每个测试程序都是独立的 .exe, 输出解释文字 + 3 秒延迟观察
#pragma once
#include <windows.h>
#include <cstdio>
#include <cwchar>

// ── 输出宽字符串 ──
inline void wprint(const wchar_t *fmt, ...)
{
    wchar_t buf[4096];
    va_list args;
    va_start(args, fmt);
    int n = _vsnwprintf_s(buf, _TRUNCATE, fmt, args);
    va_end(args);
    if (n > 0)
    {
        DWORD w;
        WriteConsoleW(GetStdHandle(STD_OUTPUT_HANDLE), buf, static_cast<DWORD>(n), &w, nullptr);
    }
}

// ── VT 序列 ──
inline void vt(const wchar_t *seq)
{
    wprint(L"%s", seq);
}

// ── 分隔线 ──
inline void sep()
{
    wprint(L"\n\x1b[1;37m──────────────────────────────────────────────────\x1b[0m\n");
}

// ── 测试标题 ──
inline void title(const wchar_t *name, const wchar_t *desc)
{
    vt(L"\x1b[2J\x1b[H"); // 清屏
    wprint(L"\x1b[1;36m══════════════════════════════════════════\x1b[0m\n");
    wprint(L"\x1b[1;33m  %s\x1b[0m\n", name);
    wprint(L"\x1b[1;36m══════════════════════════════════════════\x1b[0m\n\n");
    wprint(L"  \x1b[1;37m说明:\x1b[0m %s\n\n", desc);
    sep();
}

// ── 3 秒观察 ──
inline void wait3s(const wchar_t *hint)
{
    wprint(L"\n  \x1b[1;33m>>> %s — 3 秒后自动退出...\x1b[0m\n", hint);
    Sleep(3000);
}

// ── 获取输出句柄 + 启用 VT ──
inline HANDLE init_console()
{
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode;
    GetConsoleMode(hOut, &mode);
    SetConsoleMode(hOut, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING | DISABLE_NEWLINE_AUTO_RETURN);
    return hOut;
}
