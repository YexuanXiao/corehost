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
#include "win32/srwlock.hpp"
#include "win32/thread.hpp"

#include <cstddef>
#include <mutex>
#include <atomic>
#include <thread>
#include <span>

namespace corehost::corehost2
{

struct server_worker_context final
{
    win32::handle_view server;
    win32::handle_view event;
    server_dispatcher *dispatcher{};
    session *state{};
    conptyio::terminal_writer *writer{};
    win32::srwlock *mutex{};
    const std::atomic_bool *stop{};
};

inline DWORD WINAPI server_worker_entry(LPVOID raw)
{
    auto &context = *static_cast<server_worker_context *>(raw);
    coreio::win32_transport transport{context.server, context.event};
    auto handler = [&](coreio::io_msg &message) -> bool {
        std::unique_lock guard{*context.mutex};
        if (!context.dispatcher->handle(message))
        {
            return false;
        }
        if (!context.state->screen().has_damage())
        {
            return true;
        }
        const auto cursor = screen::coord{
            static_cast<std::int32_t>(context.state->state().cursor().x),
            static_cast<std::int32_t>(context.state->state().cursor().y),
        };
        vtserializer::render_damage(context.writer->buffer(), context.state->screen(), cursor,
                                    &context.state->state());
        context.state->screen().clear_damage();
        (void)context.writer->flush();
        return true;
    };
    coreio::run_server(transport, handler, *context.stop);
    return 0;
}

struct signal_worker_context final
{
    win32::handle_view signal_pipe;
    session *state{};
    conptyio::terminal_writer *writer{};
    conptyio::pseudo_window *window{};
    win32::srwlock *mutex{};
    const std::atomic_bool *stop{};
};

inline DWORD WINAPI signal_worker_entry(LPVOID raw)
{
    auto &context = *static_cast<signal_worker_context *>(raw);
    conptyio::signal_reader signals{context.signal_pipe};
    while (!context.stop->load(std::memory_order_relaxed) && signals.read_one())
    {
        std::unique_lock guard{*context.mutex};
        conptyio::apply_signal(signals.id(), signals.payload(), context.state->screen(),
                               context.state->state(), context.window);
        if (!context.state->screen().has_damage())
        {
            continue;
        }
        const auto cursor = screen::coord{
            static_cast<std::int32_t>(context.state->state().cursor().x),
            static_cast<std::int32_t>(context.state->state().cursor().y),
        };
        vtserializer::render_damage(context.writer->buffer(), context.state->screen(), cursor,
                                    &context.state->state());
        context.state->screen().clear_damage();
        (void)context.writer->flush();
    }
    return 0;
}
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
                               win32::handle_view condrv_input,
                               win32::handle_view condrv_output,
                               win32::handle_view vt_in,
                               win32::handle_view vt_out,
                               win32::handle_view signal_pipe,
                               const conpty_session_config &config)
{
    const auto width = config.width > 0 ? static_cast<std::size_t>(config.width) : std::size_t{120};
    const auto height = config.height > 0 ? static_cast<std::size_t>(config.height) : std::size_t{30};

    session state{width, height, config.text_measurement, config.ambiguous_is_wide};
    conptyio::terminal_reader reader{vt_in};
    conptyio::terminal_writer writer{vt_out};
    conptyio::pseudo_window window;
    (void)window.create();
    vtserializer::append_clear_screen(writer.buffer());
    vtserializer::append_cup(writer.buffer(), 0, 0);
    if (config.inherit_cursor)
    {
        vtserializer::append_request_cursor_position(writer.buffer());
    }
    (void)writer.flush();
    win32::srwlock session_mutex;
    win32::basic_thread server_thread;
    win32::basic_thread signal_thread;
    std::atomic_bool server_stop{};
    std::atomic_bool signal_stop{};

    server_session console_session{state.screen(), state.state(), state.input(), 0, server};
    console_session.set_window_handle(reinterpret_cast<std::uintptr_t>(window.handle()));
    if (config.attached_process_id != 0)
    {
        console_session.processes().add(config.attached_process_id);
    }
    server_dispatcher dispatcher{server, condrv_input, condrv_output, console_session};

    server_worker_context server_context{server, event, &dispatcher, &state, &writer,
                                         &session_mutex, &server_stop};
    if (server.valid())
    {
        server_thread = win32::basic_thread{&server_worker_entry, &server_context,
                                            {.name = L"corehost2-server"}};
    }

    signal_worker_context signal_context{signal_pipe, &state, &writer, &window, &session_mutex,
                                         &signal_stop};
    if (signal_pipe.valid())
    {
        signal_thread = win32::basic_thread{&signal_worker_entry, &signal_context,
                                            {.name = L"corehost2-signal"}};
    }

    while (reader.read_batch())
    {
        std::unique_lock guard{session_mutex};
        state.feed_input(reader.codepoints());
        (void)console_session.complete_pending();
        for (const auto [mode, enabled] : state.state().take_private_modes())
        {
            vtserializer::append_private_mode(writer.buffer(), mode, enabled);
        }
        if (state.state().take_keypad_changed())
        {
            writer.buffer().append_text(state.state().keypad_application ? u8"\x1b=" : u8"\x1b>");
        }

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

    if (server_thread.valid())
    {
        server_stop.store(true, std::memory_order_relaxed);
        ::CancelSynchronousIo(server_thread.get());
        server_thread.join();
    }

    if (signal_thread.valid())
    {
        signal_stop.store(true, std::memory_order_relaxed);
        ::CancelSynchronousIo(signal_thread.get());
        signal_thread.join();
    }
}

} // namespace corehost::corehost2