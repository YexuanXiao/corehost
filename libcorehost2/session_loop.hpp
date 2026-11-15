#pragma once

#include "session.hpp"
#include "server_dispatcher.hpp"
#include "server_session.hpp"
#include "signal.hpp"
#include "signal_reader.hpp"

#include "server_loop.hpp"
#include "win32_transport.hpp"

#include "terminal_reader.hpp"
#include "terminal_writer.hpp"
#include "viewport.hpp"

#include "win32/handle.hpp"

#include <cstddef>
#include <mutex>
#include <stop_token>
#include <thread>
#include <span>

namespace corehost::corehost2
{

struct conpty_session_config final
{
    short width{};
    short height{};
    bool inherit_cursor{};
    screen::text_measurement text_measurement{screen::text_measurement::graphemes};
    bool ambiguous_is_wide{};
    unsigned long attached_process_id{};
};

// Minimal vt_in -> parser -> libscreen -> vt_out loop. ConDrv server and signal
// handling are added by later modules; this establishes the new session entry.
inline void run_conpty_session(win32::handle_view server,
                               win32::handle_view event,
                               win32::handle_view vt_in,
                               win32::handle_view vt_out,
                               win32::handle_view signal_pipe,
                               const conpty_session_config &config)
{
    const auto width = config.width > 0 ? static_cast<std::size_t>(config.width) : std::size_t{120};
    const auto height = config.height > 0 ? static_cast<std::size_t>(config.height) : std::size_t{30};

    session state{width, height};
    conptyio::terminal_reader reader{vt_in};
    conptyio::terminal_writer writer{vt_out};
    vtserializer::append_clear_screen(writer.buffer());
    vtserializer::append_cup(writer.buffer(), 0, 0);
    (void)writer.flush();
    std::mutex session_mutex;
    std::jthread server_thread;
    std::jthread signal_thread;

    if (server.valid())
    {
        server_thread = std::jthread{[&](std::stop_token stop) {
            try
            {
                coreio::win32_transport transport{server, event};
                server_session console_session{state.screen(), state.state(), state.input()};
                server_dispatcher dispatcher{server, console_session};
                auto handler = [&](coreio::io_msg &message) {
                    const std::scoped_lock lock{session_mutex};
                    dispatcher.handle(message);
                    if (!state.screen().has_damage())
                    {
                        return;
                    }
                    const auto cursor = screen::coord{
                        static_cast<std::int32_t>(state.state().cursor().x),
                        static_cast<std::int32_t>(state.state().cursor().y),
                    };
                    vtserializer::render_damage(writer.buffer(), state.screen(), cursor, &state.state());
                    state.screen().clear_damage();
                    (void)writer.flush();
                };
                coreio::run_server(transport, handler, stop);
            }
            catch (...)
            {
            }
        }};
    }

    if (signal_pipe.valid())
    {
        signal_thread = std::jthread{[&](std::stop_token stop) {
            conptyio::signal_reader signals{signal_pipe};
            while (!stop.stop_requested() && signals.read_one())
            {
                const std::scoped_lock lock{session_mutex};
                conptyio::apply_signal(signals.id(), signals.payload(), state.screen(), state.state());
            }
        }};
    }

    while (reader.read_batch())
    {
        const std::scoped_lock lock{session_mutex};
        state.feed_input(reader.codepoints());

        const auto cursor = screen::coord{
            static_cast<std::int32_t>(state.state().cursor().x),
            static_cast<std::int32_t>(state.state().cursor().y),
        };
        vtserializer::render_damage(writer.buffer(), state.screen(), cursor, &state.state());
        state.screen().clear_damage();

        if (!writer.flush())
        {
            break;
        }
    }

    if (server_thread.joinable())
    {
        server_thread.request_stop();
        ::CancelSynchronousIo(server_thread.native_handle());
        server_thread.join();
    }

    if (signal_thread.joinable())
    {
        signal_thread.request_stop();
        ::CancelSynchronousIo(signal_thread.native_handle());
        signal_thread.join();
    }
}

} // namespace corehost::corehost2