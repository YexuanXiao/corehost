#include "consolecontrol.hpp"
#include "conwinuserrefs.h"
#include <cassert>

// 函数指针类型定义
typedef NTSTATUS(WINAPI *PFN_ConsoleControl)(CONSOLECONTROL Command, PVOID ConsoleInformation,
                                             DWORD ConsoleInformationLength);

namespace console
{
PFN_ConsoleControl g_pfnConsoleControl = nullptr;
void initialize_console_control()
{
    assert(g_pfnConsoleControl != nullptr);
    HMODULE hUser32 = nullptr;
    auto res = GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, L"user32.dll", &hUser32);

    assert(hUser32 != nullptr);
    assert(res != 0);
    g_pfnConsoleControl = (PFN_ConsoleControl)GetProcAddress(hUser32, "ConsoleControl");
    assert(g_pfnConsoleControl != nullptr);
}
NTSTATUS ConsoleControl(_In_ CONSOLECONTROL Command,
                        _In_reads_bytes_(ConsoleInformationLength) PVOID ConsoleInformation,
                        _In_ DWORD ConsoleInformationLength)
{
    assert(console::g_pfnConsoleControl != nullptr);

    return console::g_pfnConsoleControl(Command, ConsoleInformation, ConsoleInformationLength);
}
} // namespace console
