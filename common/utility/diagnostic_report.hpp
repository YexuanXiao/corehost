#pragma once

#include <windows.h>
#include <string>
#include "win32/string.hpp"

namespace diagnostic_report
{

[[nodiscard]] std::wstring write_elevated_terminal_blocked(win32::wcstring_view reason, DWORD client_pid,
                                                           DWORD process_group_id, DWORD show_window,
                                                           DWORD startup_flags, bool window_visible, bool console_app,
                                                           win32::wcstring_view command_line) noexcept;

[[nodiscard]] std::wstring write_no_default_terminal(win32::wcstring_view reason, DWORD client_pid,
                                                     DWORD process_group_id, DWORD show_window, DWORD startup_flags,
                                                     bool window_visible, bool console_app,
                                                     win32::wcstring_view command_line) noexcept;

} // namespace diagnostic_report
