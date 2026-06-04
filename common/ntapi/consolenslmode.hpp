#pragma once
#include <windows.h>

namespace console
{
void initialize_console_nls() noexcept;

BOOL GetConsoleNlsMode(_In_ HANDLE hConsole, _Out_ PDWORD lpdwNlsMode) noexcept;
BOOL SetConsoleNlsMode(_In_ HANDLE hConsole, _In_ DWORD dwNlsMode) noexcept;

} // namespace console
