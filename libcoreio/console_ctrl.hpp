#pragma once

#include "ntapi/consolecontrol.hpp"
#include "ntapi/conwinuserrefs.h"

#include <Windows.h>

#include <cassert>
#include <cstdlib>
#include <cstdint>

namespace corehost::coreio
{

inline constexpr ULONG console_ctrl_c_flag = 0x00000001;
inline constexpr ULONG console_ctrl_break_flag = 0x00000002;
inline constexpr ULONG console_ctrl_close_flag = 0x00000004;

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

// Delivers a console control event to one client process through
// user32!ConsoleControl(ConsoleEndTask), matching libcorehost.
class ctrl_delivery final
{
  public:
    ctrl_delivery() : control_{resolve_console_control()}
    {
        assert(control_ != nullptr);
        if (control_ == nullptr)
        {
            std::abort();
        }
    }

    void deliver(DWORD pid, DWORD event) const noexcept
    {
        CONSOLEENDTASK task{};
        task.ProcessId = ULongToHandle(pid);
        task.hwnd = nullptr;
        task.ConsoleEventCode = event;
        task.ConsoleFlags = console_ctrl_flags(event);
        (void)control_(ConsoleEndTask, &task, sizeof(task));
    }

  private:
    PFN_ConsoleControl control_{};
};

} // namespace corehost::coreio
