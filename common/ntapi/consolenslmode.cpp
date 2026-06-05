// ── ntapi/consolenslmode.cpp ──────────────────────────
// GetConsoleNlsMode / SetConsoleNlsMode 动态加载
// 对标 ConsoleControl 的实现方式: GetProcAddress 从 kernel32.dll
// 加载内部 API，避免直接链接。

#include "consolenslmode.hpp"
#include <cassert>

// 函数指针类型定义
typedef BOOL (*PFN_GetConsoleNlsMode)(HANDLE hConsole, PDWORD lpdwNlsMode);
typedef BOOL (*PFN_SetConsoleNlsMode)(HANDLE hConsole, DWORD dwNlsMode);

namespace console
{
PFN_GetConsoleNlsMode g_pfnGetConsoleNlsMode = nullptr;
PFN_SetConsoleNlsMode g_pfnSetConsoleNlsMode = nullptr;

void initialize_console_nls() noexcept
{
    HMODULE hKernel32 = nullptr;
    auto res = GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, L"kernel32.dll", &hKernel32);
    assert(hKernel32 != nullptr);
    assert(res != 0);

    g_pfnGetConsoleNlsMode = reinterpret_cast<PFN_GetConsoleNlsMode>(GetProcAddress(hKernel32, "GetConsoleNlsMode"));
    g_pfnSetConsoleNlsMode = reinterpret_cast<PFN_SetConsoleNlsMode>(GetProcAddress(hKernel32, "SetConsoleNlsMode"));
}

BOOL GetConsoleNlsMode(_In_ HANDLE hConsole, _Out_ PDWORD lpdwNlsMode) noexcept
{
    return console::g_pfnGetConsoleNlsMode(hConsole, lpdwNlsMode);
}

BOOL SetConsoleNlsMode(_In_ HANDLE hConsole, _In_ DWORD dwNlsMode) noexcept
{
    return console::g_pfnSetConsoleNlsMode(hConsole, dwNlsMode);
}

} // namespace console
