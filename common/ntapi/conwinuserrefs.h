#pragma once

#include <windows.h>
#include <winternl.h>

typedef enum _CONSOLECONTROL
{
    Reserved1,
    ConsoleNotifyConsoleApplication,
    Reserved2,
    ConsoleSetCaretInfo,
    Reserved3,
    ConsoleSetForeground,
    ConsoleSetWindowOwner,
    ConsoleEndTask,
} CONSOLECONTROL;

typedef struct _CONSOLEENDTASK
{
    HANDLE ProcessId;
    HWND hwnd;
    ULONG ConsoleEventCode;
    ULONG ConsoleFlags;
} CONSOLEENDTASK, *PCONSOLEENDTASK;

typedef struct _CONSOLE_PROCESS_INFO
{
    IN DWORD dwProcessID;
    IN DWORD dwFlags;
} CONSOLE_PROCESS_INFO, *PCONSOLE_PROCESS_INFO;

typedef struct _CONSOLEENDTASKDATA
{
    DWORD dwSize;
    DWORD ProcessId;
    ULONG ConsoleEventCode;
    ULONG ConsoleFlags;
} CONSOLEENDTASKDATA, *PCONSOLEENDTASKDATA;

typedef struct _CONSOLESETFOREGROUNDDATA
{
    DWORD dwSize;
    DWORD ProcessId;
    bool Foreground;
} CONSOLESETFOREGROUNDDATA, *PCONSOLESETFOREGROUNDDATA;

typedef struct _CONSOLENOTIFYAPPDATA
{
    IN DWORD dwSize;
    IN DWORD dwProcessID;
} CONSOLENOTIFYAPPDATA, *PCONSOLENOTIFYAPPDATA;

NTSTATUS ConsoleControl(
    _In_ CONSOLECONTROL Command,
    _In_reads_bytes_(ConsoleInformationLength) PVOID ConsoleInformation,
    _In_ DWORD ConsoleInformationLength
);

#define CPI_NEWPROCESSWINDOW 0x0001

