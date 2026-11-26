#pragma once

#include <string>
#include "libuitext/text_options.hpp"
#include "win32/string.hpp"

namespace corehost::window
{

// Window-mode entry point. It creates the ConDrv server, then the in-process
// console session, and finally starts the top-level window.
void window_entry(win32::wcstring_view title, std::wstring client_command_line,
                  uitext::text_options text_options = {});

} // namespace corehost::window
