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
                  const corehost_config &config)
{
    auto server = condrv::create_server_handle();
    win32::srwlock console_mutex;
    // The console must use the same grid size as the first window frame.
    const auto &console_config = config.console;
    const auto &window_config = config.window;
    const auto initial_columns = window_config.initial_columns;
    const auto initial_rows = window_config.initial_rows;
    console shared_console{initial_columns, initial_rows, window_config.scrollback_lines};
    shared_console.set_copy_crlf(window_config.copy_ending == copy_line_ending::crlf);
    shared_console.set_trim_block(config.mouse.selection_trim_block);
    window_console_session console{server.view(), shared_console, console_mutex, config};
    console.start();

    window_client_process client{server.view(), title, client_command_line, config};
    window_ui ui{shared_console, console_mutex, client.process(), title, config, console.output_event().view()};
    ui.start();
    ui.wait();

    coreio::ctrl_delivery delivery;
    delivery.deliver(client.process_id(), CTRL_CLOSE_EVENT);
    client.wait();
    console.stop();
}

} // namespace corehost::window
