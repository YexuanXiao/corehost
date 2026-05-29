#pragma once
#include <windows.h>

namespace console
{
void initialize_console_nls();

BOOL GetConsoleNlsMode(_In_ HANDLE hConsole, _Out_ PDWORD lpdwNlsMode);
BOOL SetConsoleNlsMode(_In_ HANDLE hConsole, _In_ DWORD dwNlsMode);

} // namespace console