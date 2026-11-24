#include "window_entry.hpp"

#include "libcorewindow/window_host.hpp"
#include "window_client_process.hpp"
#include "window_console_session.hpp"

#include "console_ctrl.hpp"
#include "ntapi/condrv.hpp"
#include "win32/srwlock.hpp"

#include <utility>

namespace corehost::window
{

void window_entry(std::wstring title, std::wstring client_command_line)
{
    auto server = condrv::create_server_handle();
    win32::srwlock console_mutex;
    console shared_console{120, 30};
    window_console_session console{server.view(), shared_console, console_mutex};
    console.start();

    window_client_process client{server.view(), title, client_command_line};
    window_host host{title, shared_console, console_mutex, client.process()};
    host.run();

    coreio::ctrl_delivery delivery;
    delivery.deliver(client.process_id(), CTRL_CLOSE_EVENT);
    client.wait();
    console.stop();
}

} // namespace corehost::window
