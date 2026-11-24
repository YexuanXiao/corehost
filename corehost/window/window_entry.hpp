#pragma once

#include <string>

namespace corehost::window
{

// Window-mode entry point. It creates the ConDrv server, then the in-process
// console session, and finally starts the top-level window.
void window_entry(std::wstring title, std::wstring client_command_line);

} // namespace corehost::window
