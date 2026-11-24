#include "window_entry.hpp"

#include "libcorewindow/window_host.hpp"
#include "window_client_process.hpp"
#include "window_console_session.hpp"

#include "ntapi/condrv.hpp"

#include <utility>

namespace corehost::window
{
namespace
{
bool take_console_updates(void *context, std::vector<screen_row_update> &updates)
{
    return static_cast<window_console_session *>(context)->take_screen_updates(updates);
}

void attach_console_window(void *context, HWND window)
{
    static_cast<window_console_session *>(context)->set_window_handle(window);
}
} // namespace

void window_entry(std::wstring title, std::wstring client_command_line)
{
    auto server = condrv::create_server_handle();
    window_console_session console{server.view(), 120, 30};
    console.start();

    window_client_process client{server.view(), title, client_command_line};
    window_host host{title, {&console, &take_console_updates, &attach_console_window}};
    host.run();

    client.terminate();
    console.stop();
}

} // namespace corehost::window
