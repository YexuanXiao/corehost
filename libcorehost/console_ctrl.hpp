// ── libcorehost/console_ctrl.hpp ─────────────────────────────
// Console control events (Ctrl+C, Ctrl+Break) for the processes on this
// console. Like conhost's ConsoleControl::EndTask, user32!ConsoleControl
// (ConsoleEndTask) asks CSRSS to run the event's handler routine in the
// process; the process itself decides whether it ignores the event.
#pragma once
#include <windows.h>
#include <winternl.h>
#include "ntapi/conwinuserrefs.h"

namespace corehost::conpty
{

// The CONSOLE_CTRL_*_FLAG values conhost hands along with the event.
inline constexpr ULONG console_ctrl_c_flag = 0x00000001;
inline constexpr ULONG console_ctrl_break_flag = 0x00000002;

// ReadConsole/ReadFile were ended by Ctrl+C or Ctrl+Break (conhost replies
// STATUS_ALERTED; the client sees 0 chars and ERROR_OPERATION_ABORTED).
inline constexpr LONG status_alerted = 0x00000101;

inline NTSTATUS console_end_task(DWORD pid, DWORD event, HWND console_window) noexcept
{
    using console_control_fn = NTSTATUS(WINAPI *)(CONSOLECONTROL, PVOID, DWORD);
    static const auto console_control = [] {
        auto user32 = ::GetModuleHandleW(L"user32.dll");
        if (!user32)
            user32 = ::LoadLibraryExW(L"user32.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        return user32 ? reinterpret_cast<console_control_fn>(::GetProcAddress(user32, "ConsoleControl")) : nullptr;
    }();
    if (!console_control)
        return static_cast<NTSTATUS>(0xC0000002L); // STATUS_NOT_IMPLEMENTED

    CONSOLEENDTASK task{};
    task.ProcessId = ULongToHandle(pid);
    task.hwnd = console_window;
    task.ConsoleEventCode = event;
    task.ConsoleFlags = event == CTRL_BREAK_EVENT ? console_ctrl_break_flag : console_ctrl_c_flag;
    return console_control(ConsoleEndTask, &task, sizeof(task));
}

} // namespace corehost::conpty
