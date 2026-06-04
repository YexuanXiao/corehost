#pragma once
#include "conwinuserrefs.h"
namespace console
{
void initialize_console_control() noexcept;
NTSTATUS ConsoleControl(_In_ CONSOLECONTROL Command,
                        _In_reads_bytes_(ConsoleInformationLength) PVOID ConsoleInformation,
                        _In_ DWORD ConsoleInformationLength) noexcept;
} // namespace console
