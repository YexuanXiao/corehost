#include "defterm.hpp"

#include "console_session.hpp"
#include "connect_policy.hpp"
#include "handoff_session.hpp"
#include "terminal_handoff.hpp"

#include "condrv_io.hpp"
#include "os/Console/conmsgl1.h"
#include "utility/env.hpp"
#include "utility/log.hpp"
#include "win32/event.hpp"
#include "win32/handle.hpp"
#include "win32/wait.hpp"

#include <cassert>
#include <utility>

namespace corehost::defterm

{

struct connect_decision final
{
    bool start_terminal{};
    bool connected{};
};

connect_decision handle_connect(win32::handle_view server, win32::handle_view input_event, coreio::io_msg &message)
{
    const auto client_pid = static_cast<DWORD>(message.descriptor.Process);
    const auto *info = message.descriptor.InputSize >= sizeof(CONSOLE_SERVER_MSG)
                           ? reinterpret_cast<const CONSOLE_SERVER_MSG *>(message.body)
                           : nullptr;

    if (info != nullptr && should_start_terminal_window(*info, false) && !env::is_elevated())
    {
        if (try_terminal_handoff(server, input_event, make_portable_attach_msg(message), client_pid))
        {
            LOG("defterm: terminal handoff completed");
            return {.start_terminal = true};
        }
    }

    if (info != nullptr && should_start_terminal_window(*info, false))
    {
        // GUI was requested but no terminal accepted the handoff, or the
        // process is elevated. Complete CONNECT and let the client see a
        // broken console instead of leaving it waiting forever.
        win32::handle input;
        win32::handle output;
        coreio::accept_connection(server, message, input, output);
        coreio::complete_io(server, message.complete);
        return {.start_terminal = true};
    }

    return {};
}


void defterm_entry(std::uintptr_t condrv_handle)
{
    assert(condrv_handle != 0);
    auto server = win32::handle{reinterpret_cast<HANDLE>(condrv_handle)};
    auto input_event = win32::event{win32::create_tag, true, false};
    coreio::set_server_info(server.view(), input_event.view());

    coreio::io_msg message{};
    for (;;)
    {
        const auto result = coreio::read_io_try(server.view(), nullptr, message);
        if (result == coreio::read_io_result::disconnected)
        {
            return;
        }
        if (result == coreio::read_io_result::no_message)
        {
            (void)win32::wait_one(input_event.view(), INFINITE);
            continue;
        }
        if (result != coreio::read_io_result::got_message)
        {
            return;
        }

        if (message.descriptor.Function != CONSOLE_IO_CONNECT)
        {
            coreio::prepare_completion(message);
            coreio::complete_io(server.view(), message.complete);
            continue;
        }

        const auto decision = handle_connect(server.view(), input_event.view(), message);
        if (decision.start_terminal)
        {
            return;
        }

        const auto width = message.descriptor.InputSize >= sizeof(CONSOLE_SERVER_MSG)
                               ? reinterpret_cast<const CONSOLE_SERVER_MSG *>(message.body)->ScreenBufferSize.X
                               : 120;
        const auto height = message.descriptor.InputSize >= sizeof(CONSOLE_SERVER_MSG)
                                ? reinterpret_cast<const CONSOLE_SERVER_MSG *>(message.body)->ScreenBufferSize.Y
                                : 30;

        console_session session{server.view(), input_event.view(), static_cast<short>(width > 0 ? width : 120),
                                            static_cast<short>(height > 0 ? height : 30)};
        session.handle(message);
        session.run();
        return;
    }
}

} // namespace corehost::defterm