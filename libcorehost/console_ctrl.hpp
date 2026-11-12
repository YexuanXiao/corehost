// ── libcorehost/console_ctrl.hpp ─────────────────────────────
// Console control events (Ctrl+C, Ctrl+Break) for the processes on this
// console. user32!ConsoleControl(ConsoleEndTask) asks CSRSS to run the
// event handler in the client process; the client decides whether it
// ignores the event.
#pragma once
#include <windows.h>
#include <winternl.h>
#include "ntapi/conwinuserrefs.h"
#include "ntapi/consolecontrol.hpp"

namespace corehost::conpty
{

// The CONSOLE_CTRL_*_FLAG values passed along with the event.
inline constexpr ULONG console_ctrl_c_flag = 0x00000001;
inline constexpr ULONG console_ctrl_break_flag = 0x00000002;
inline constexpr ULONG console_ctrl_close_flag = 0x00000004;

// ReadConsole/ReadFile were ended by Ctrl+C or Ctrl+Break (the host replies
// STATUS_ALERTED; the client sees 0 chars and ERROR_OPERATION_ABORTED).
inline constexpr LONG status_alerted = 0x00000101;

// 事件码到 ConsoleFlags 的映射；conhost宿主使用同一套标志
// 判断 EndTask 的语义（Ctrl+C / Ctrl+Break / Close）。
inline ULONG console_ctrl_flags(DWORD event) noexcept
{
    switch (event)
    {
    case CTRL_BREAK_EVENT:
        return console_ctrl_break_flag;
    case CTRL_CLOSE_EVENT:
        return console_ctrl_close_flag;
    default:
        return console_ctrl_c_flag;
    }
}

inline NTSTATUS console_end_task(DWORD pid, DWORD event, HWND console_window) noexcept
{
    CONSOLEENDTASK task{};
    task.ProcessId = ULongToHandle(pid);
    task.hwnd = console_window;
    task.ConsoleEventCode = event;
    task.ConsoleFlags = console_ctrl_flags(event);
    return console_control()(ConsoleEndTask, &task, sizeof(task));
}

} // namespace corehost::conpty