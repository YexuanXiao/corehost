#include "console_arguments.hpp"

#include "client/client.hpp"
#include "comserver/com_server.hpp"
#include "defterm/defterm.hpp"
#include "libcorepty/conpty.hpp"

#include "condrv_io.hpp"
#include "shell/shell.hpp"
#ifdef COREHOST_WINDOW
#include "window/window_entry.hpp"
#endif
#include "utility/crtdbg.hpp"
#include "utility/log.hpp"
#include "win32/event.hpp"
#include "win32/handle.hpp"

#include <Windows.h>

#include <string>
#include <utility>

int __stdcall wWinMain(_In_ HINSTANCE, _In_opt_ HINSTANCE, _In_ PWSTR, _In_ int)
{
    utility::suppress_crt_error_dialogs();
    corehost::log::initialize_log();
    LOG("corehost process start, cmdline=%ls", ::GetCommandLineW());

    const auto args = corehost::cli::console_arguments{::GetCommandLineW()};

    if (args.com_server())
    {
        auto handoff = corehost::comserver::com_server_entry();
        if (!handoff.server.valid())
        {
            LOG("COM handoff failed");
            return 1;
        }

        corehost::corepty::conpty_entry(handoff.server.view(), handoff.event.view(),
                                        std::move(handoff.condrv_input), std::move(handoff.condrv_output),
                                        handoff.vt_in.view(), handoff.vt_out.view(), std::move(handoff.signal),
                                        handoff.width, handoff.height, false,
                                        corehost::screen::text_measurement::graphemes, true);
        return 0;
    }

    if (const auto handle = args.condrv_handle(); handle != 0)
    {
        corehost::defterm::defterm_entry(handle);
        return 0;
    }

    if (args.is_headless())
    {
        auto server = win32::handle::from_uintptr(args.server_handle());
        auto input_event = win32::event{win32::create_tag, true, false};
        auto event = win32::handle{input_event.release()};
        corehost::coreio::set_server_info(server.view(), event.view());

        win32::handle signal;
        if (const auto signal_handle = args.signal_handle(); signal_handle != 0)
        {
            signal = win32::handle::from_uintptr(signal_handle);
        }

        corehost::corepty::conpty_entry(
            server.view(), event.view(), {}, {}, win32::handle_view{::GetStdHandle(STD_INPUT_HANDLE)},
            win32::handle_view{::GetStdHandle(STD_OUTPUT_HANDLE)}, std::move(signal), args.width(), args.height(),
            args.inherit_cursor(), args.text_measurement(), args.ambiguous_is_wide());
        return 0;
    }

#ifdef COREHOST_WINDOW
    auto cmdline = args.client_command_line();
    if (cmdline.empty())
    {
        auto shell_info = shell::get_shell();
        corehost::window::window_entry({shell_info.name.data(), shell_info.name.size()},
                                              std::move(shell_info.path));
    }
    else
    {
        corehost::window::window_entry({}, std::wstring(cmdline.data(), cmdline.size()));
    }
#else
    auto cmdline = args.client_command_line();
    if (cmdline.empty())
    {
        auto shell_info = shell::get_shell();
        corehost::client::client_entry({shell_info.name.data(), shell_info.name.size()}, std::move(shell_info.path));
    }
    else
    {
        corehost::client::client_entry({}, std::wstring(cmdline.data(), cmdline.size()));
    }
#endif
    return 0;
}