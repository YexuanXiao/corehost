#pragma once

#include <string>
#include "console_config.hpp"
#include "window_config.hpp"
#include "win32/string.hpp"

namespace corehost::window
{

// Window-mode entry point. It creates the ConDrv server, then the in-process
// console session, and finally starts the top-level window.
void window_entry(win32::wcstring_view title, std::wstring client_command_line,
                  const console_config &console_config, const window_config &window_config);

} // namespace corehost::window
