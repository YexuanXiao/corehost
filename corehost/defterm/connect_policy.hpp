#pragma once

#include <windows.h>
#include <string>
#include "os/Console/conmsgl1.h"
#include "utility/log.hpp"
#include "win32/error.hpp"
#include "win32/handle.hpp"
#include "win32/process.hpp"

namespace corehost::defterm
{

[[nodiscard]] inline bool is_interactive_user_session() noexcept
{
    // session_id == 0 通常是服务/系统会话，不应该尝试启动用户可见终端。
    // 非 0 仍需继续检查 window station 是否可见。
    DWORD session_id = 0;
    if (!::ProcessIdToSessionId(::GetCurrentProcessId(), &session_id))
    {
        LOG("serious: cannot query process session; GUI handoff is not safe err=%lu", ::GetLastError());
        return false;
    }
    if (session_id == 0)
    {
        LOG("session 0 is non-interactive; GUI handoff is not expected");
        return false;
    }

    // nullptr 表示无法取得当前进程 window station；这种环境不能保证 GUI
    // 终端可见。
    auto window_station = ::GetProcessWindowStation();
    if (!window_station)
    {
        LOG("serious: cannot query window station; GUI handoff is not safe err=%lu", ::GetLastError());
        return false;
    }

    // WSF_VISIBLE 是默认终端 handoff 的最低 GUI 条件。没有这个标志时，
    // COM 激活即使成功也可能无法显示窗口。
    USEROBJECTFLAGS flags{};
    if (!::GetUserObjectInformationW(window_station, UOI_FLAGS, &flags, sizeof(flags), nullptr))
    {
        LOG("serious: cannot query window station visibility; GUI handoff is not safe err=%lu", ::GetLastError());
        return false;
    }
    if (!(flags.dwFlags & WSF_VISIBLE))
    {
        LOG("window station is not visible; GUI handoff is not expected flags=0x%08lx", flags.dwFlags);
        return false;
    }

    LOG("interactive session confirmed: session=%lu flags=0x%08lx", session_id, flags.dwFlags);
    return true;
}

[[nodiscard]] inline bool connect_requests_terminal_window(const CONSOLE_SERVER_MSG &msg) noexcept
{
    LOG("startup visibility: consoleApp=%u visible=%u flags=0x%08lx showWindow=%u titleBytes=%u pgid=%lu",
        static_cast<unsigned>(msg.ConsoleApp), static_cast<unsigned>(msg.WindowVisible), msg.StartupFlags,
        msg.ShowWindow, msg.TitleLength, msg.ProcessGroupId);

    if (!msg.WindowVisible)
    {
        LOG("WindowVisible=false; headless conpty is expected");
        return false;
    }

    if (msg.StartupFlags & STARTF_USESHOWWINDOW)
    {
        // 这些 ShowWindow 值表示调用方明确要求隐藏或最小化；defterm
        // 不主动弹出终端窗口，后续会走 conpty/headless 路径。
        switch (msg.ShowWindow)
        {
        case SW_HIDE:
        case SW_SHOWMINIMIZED:
        case SW_MINIMIZE:
        case SW_SHOWMINNOACTIVE:
        case SW_FORCEMINIMIZE:
            LOG("hidden/minimized startup requested; headless conpty is expected showWindow=%u", msg.ShowWindow);
            return false;
        default:
            break;
        }
    }

    return true;
}

[[nodiscard]] inline bool should_start_terminal_window(const CONSOLE_SERVER_MSG &connect_info, bool already_initialized)
{
    if (already_initialized)
    {
        LOG("CONNECT after initial session; no new terminal window expected");
        return false;
    }
    if (!connect_requests_terminal_window(connect_info))
    {
        LOG("startup info does not request a visible terminal");
        return false;
    }
    if (!is_interactive_user_session())
    {
        LOG("non-interactive session; GUI handoff is not expected");
        return false;
    }
    return true;
}

[[nodiscard]] inline std::wstring query_process_command_line(DWORD pid)
{
    // pid 来自 ConDrv 的 CONNECT 描述符。查询失败时返回空字符串，
    // 通知仍可显示 UAC 阻止原因，不把进程查询失败升级为会话错误。
    win32::handle process{::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ, FALSE, pid)};
    if (!process.valid())
        return {};
    return win32::query_process_command_line(process);
}

} // namespace corehost::defterm
