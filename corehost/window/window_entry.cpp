#include "window_entry.hpp"
#include "config.hpp"

#include "libcorewindow/window_ui.hpp"
#include "window_client_process.hpp"
#include "window_console_session.hpp"

#include "console_ctrl.hpp"
#include "ntapi/condrv.hpp"
#include "win32/srwlock.hpp"

#include <utility>

namespace corehost::window
{

void window_entry(win32::wcstring_view title, std::wstring client_command_line,
                  const console_config &console_config, const window_config &window_config)
{
    auto server = condrv::create_server_handle();
    win32::srwlock console_mutex;
    // The console must use the same grid size as the first window frame.
    const auto initial_columns = window_config.initial_columns;
    const auto initial_rows = window_config.initial_rows;
    console shared_console{initial_columns, initial_rows, console_config.scrollback_lines};
    shared_console.set_copy_crlf(console_config.copy_ending == copy_line_ending::crlf);
    window_console_session console{server.view(), shared_console, console_mutex};
    console.start();

    window_client_process client{server.view(), title, client_command_line};
    window_ui ui{shared_console, console_mutex, client.process(), title, console_config, window_config,
                 console.output_event().view()};
    ui.start();
    ui.wait();

    coreio::ctrl_delivery delivery;
    delivery.deliver(client.process_id(), CTRL_CLOSE_EVENT);
    client.wait();
    console.stop();
}

} // namespace corehost::window
