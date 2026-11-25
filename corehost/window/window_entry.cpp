#include "window_entry.hpp"

#include "libcorewindow/window_ui.hpp"
#include "window_client_process.hpp"
#include "window_console_session.hpp"

#include "console_ctrl.hpp"
#include "ntapi/condrv.hpp"
#include "win32/srwlock.hpp"

#include <cstdlib>
#include <utility>

namespace corehost::window
{

void window_entry(win32::wcstring_view title, std::wstring client_command_line)
{
    auto server = condrv::create_server_handle();
    win32::srwlock console_mutex;
    const auto [initial_columns, initial_rows] = window_host::initial_cell_size();
    console shared_console{initial_columns, initial_rows};
    window_console_session console{server.view(), shared_console, console_mutex};
    console.start();

    window_client_process client{server.view(), title, client_command_line};
    window_ui ui{shared_console, console_mutex, client.process(), title, client_exit_policy::keep_open,
                 console_palette::campbell()};
    ui.start();
    const bool ui_fatal = ui.wait();

    coreio::ctrl_delivery delivery;
    delivery.deliver(client.process_id(), CTRL_CLOSE_EVENT);
    client.wait();
    console.stop();

    if (ui_fatal)
    {
        std::exit(EXIT_FAILURE);
    }
}

} // namespace corehost::window
