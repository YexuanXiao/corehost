#include "window_entry.hpp"

#include "libcorewindow/window_host.hpp"
#include "window_console_session.hpp"

#include "ntapi/condrv.hpp"

#include <utility>

namespace corehost::window
{

void window_entry(std::wstring title, std::wstring client_command_line)
{
    auto server = condrv::create_server_handle();
    window_console_session console{server.view(), 120, 30};
    window_host host{std::move(title)};

    // The client process and its console handles are attached in the next
    // milestone. Starting the session before the window keeps the same order
    // as defterm: server, console session, then the visible host.
    (void)client_command_line;
    console.start();
    host.run();
    console.stop();
}

} // namespace corehost::window
