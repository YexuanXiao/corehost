#pragma once

#include <windows.h>
#include <string>
#include "os/Console/conmsgl1.h"
#include "utility/log.hpp"
#include "win32/error.hpp"
#include "win32/handle.hpp"
#include "win32/process.hpp"

namespace deftermv2
{

[[nodiscard]] inline bool is_interactive_user_session() noexcept
{
    DWORD session_id = 0;
    if (!::ProcessIdToSessionId(::GetCurrentProcessId(), &session_id))
    {
        LOG("deftermv2::is_interactive_user_session: ProcessIdToSessionId failed err=%lu", ::GetLastError());
        return false;
    }
    if (session_id == 0)
    {
        LOG("deftermv2::is_interactive_user_session: session 0");
        return false;
    }

    auto window_station = ::GetProcessWindowStation();
    if (!window_station)
    {
        LOG("deftermv2::is_interactive_user_session: GetProcessWindowStation failed err=%lu", ::GetLastError());
        return false;
    }

    USEROBJECTFLAGS flags{};
    if (!::GetUserObjectInformationW(window_station, UOI_FLAGS, &flags, sizeof(flags), nullptr))
    {
        LOG("deftermv2::is_interactive_user_session: GetUserObjectInformationW failed err=%lu", ::GetLastError());
        return false;
    }
    if (!(flags.dwFlags & WSF_VISIBLE))
    {
        LOG("deftermv2::is_interactive_user_session: invisible window station flags=0x%08lx", flags.dwFlags);
        return false;
    }

    LOG("deftermv2::is_interactive_user_session: yes session=%lu flags=0x%08lx", session_id, flags.dwFlags);
    return true;
}

[[nodiscard]] inline bool connect_requests_terminal_window(const CONSOLE_SERVER_MSG &msg) noexcept
{
    LOG("deftermv2::connect_requests_terminal_window: consoleApp=%u visible=%u startupFlags=0x%08lx "
        "showWindow=%u titleLength=%u pgid=%lu",
        static_cast<unsigned>(msg.ConsoleApp), static_cast<unsigned>(msg.WindowVisible), msg.StartupFlags,
        msg.ShowWindow, msg.TitleLength, msg.ProcessGroupId);

    if (!msg.WindowVisible)
    {
        LOG("deftermv2::connect_requests_terminal_window: reject WindowVisible=false");
        return false;
    }

    if (msg.StartupFlags & STARTF_USESHOWWINDOW)
    {
        switch (msg.ShowWindow)
        {
        case SW_HIDE:
        case SW_SHOWMINIMIZED:
        case SW_MINIMIZE:
        case SW_SHOWMINNOACTIVE:
        case SW_FORCEMINIMIZE:
            LOG("deftermv2::connect_requests_terminal_window: reject showWindow=%u", msg.ShowWindow);
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
        LOG("deftermv2::should_start_terminal_window: reject already initialized");
        return false;
    }
    if (!connect_requests_terminal_window(connect_info))
    {
        LOG("deftermv2::should_start_terminal_window: reject startup info");
        return false;
    }
    if (!is_interactive_user_session())
    {
        LOG("deftermv2::should_start_terminal_window: reject non-interactive session");
        return false;
    }
    return true;
}

[[nodiscard]] inline std::wstring query_process_image_path(DWORD pid)
{
    win32::handle process{::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid)};
    win32::throw_last_error(!process.valid());
    return win32::query_full_process_image_name(process);
}

} // namespace deftermv2
