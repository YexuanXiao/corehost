#pragma once
#include "conwinuserrefs.h"
namespace console
{
void initialize_console_control();
NTSTATUS ConsoleControl(_In_ CONSOLECONTROL Command,
                        _In_reads_bytes_(ConsoleInformationLength) PVOID ConsoleInformation,
                        _In_ DWORD ConsoleInformationLength);
} // namespace console