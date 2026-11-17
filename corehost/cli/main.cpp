#include "console_arguments.hpp"

#include "client/client.hpp"
#include "comserver/com_server.hpp"
#include "defterm/defterm.hpp"
#include "libcorepty/conpty.hpp"
#include "support/com_handoff_bridge.hpp"

#include "condrv_io.hpp"
#include "shell/shell.hpp"
#include "utility/crtdbg.hpp"
#include "utility/log.hpp"
#include "win32/event.hpp"
#include "win32/handle.hpp"

#include <Windows.h>

#include <string>
#include <utility>

namespace
{

int run_com_handoff()
{
    corehost::support::com_handoff_raw handoff{};
    if (!corehost::support::acquire_com_handoff(handoff))
    {
        LOG("COM handoff failed");
        return 1;
    }

    auto server = win32::handle::from_uintptr(handoff.server);
    auto event = win32::handle::from_uintptr(handoff.event);
    auto condrv_input = win32::handle::from_uintptr(handoff.condrv_input);
    auto condrv_output = win32::handle::from_uintptr(handoff.condrv_output);
    auto vt_in = win32::handle::from_uintptr(handoff.vt_in);
    auto vt_out = win32::handle::from_uintptr(handoff.vt_out);
    auto signal = win32::handle::from_uintptr(handoff.signal);

    corehost::corepty::conpty_entry(server.view(), event.view(), std::move(condrv_input), std::move(condrv_output),
                                      vt_in.view(), vt_out.view(), std::move(signal), handoff.width, handoff.height,
                                      false, corehost::screen::text_measurement::graphemes, true);
    return 0;
}

int run_headless(const corehost::cli::console_arguments &args)
{
    auto server = win32::handle::from_uintptr(args.server_handle());
    auto input_event = win32::event{win32::create_tag, true, false};
    auto event = win32::handle{input_event.release()};
    corehost::coreio::set_server_info(server.view(), event.view());

    win32::handle signal;
    if (const auto handle = args.signal_handle(); handle != 0)
    {
        signal = win32::handle::from_uintptr(handle);
    }

    corehost::corepty::conpty_entry(
        server.view(), event.view(), {}, {}, win32::handle_view{::GetStdHandle(STD_INPUT_HANDLE)},
        win32::handle_view{::GetStdHandle(STD_OUTPUT_HANDLE)}, std::move(signal), args.width(), args.height(),
        args.inherit_cursor(), args.text_measurement(), args.ambiguous_is_wide());
    return 0;
}

} // namespace

int __stdcall wWinMain(_In_ HINSTANCE, _In_opt_ HINSTANCE, _In_ PWSTR, _In_ int)
{
    utility::suppress_crt_error_dialogs();
    corehost::log::initialize_log();
    LOG("corehost process start, cmdline=%ls", ::GetCommandLineW());

    const auto args = corehost::cli::console_arguments{::GetCommandLineW()};

    if (args.com_server())
    {
        return run_com_handoff();
    }

    if (const auto handle = args.condrv_handle(); handle != 0)
    {
        corehost::defterm::defterm_entry(handle);
        return 0;
    }

    if (args.is_headless())
    {
        return run_headless(args);
    }

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
    return 0;
}
