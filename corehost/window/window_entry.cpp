#include "window_entry.hpp"

#include "libcorewindow/window_host.hpp"
#include "window_client_process.hpp"
#include "window_console_session.hpp"

#include "ntapi/condrv.hpp"

#include <utility>

namespace corehost::window
{

void window_entry(std::wstring title, std::wstring client_command_line)
{
    auto server = condrv::create_server_handle();
    console_screen screen{120, 30};
    window_console_session console{server.view(), screen};
    console.start();

    window_client_process client{server.view(), title, client_command_line};
    window_host host{title, screen};
    host.run();

    client.terminate();
    console.stop();
}

} // namespace corehost::window
