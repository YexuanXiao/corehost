#pragma once

#include "session.hpp"
#include "server_dispatcher.hpp"
#include "server_session.hpp"
#include "signal.hpp"
#include "signal_thread.hpp"
#include "vt_input_thread.hpp"

#include "console_ctrl.hpp"
#include "server_loop.hpp"
#include "win32_transport.hpp"

#include "terminal_reader.hpp"
#include "terminal_writer.hpp"
#include "viewport.hpp"

#include "utf8.hpp"
#include "win32/event.hpp"
#include "win32/handle.hpp"
#include "win32/srwlock.hpp"
#include "win32/thread.hpp"

#include <cstddef>
#include <mutex>
#include <atomic>
#include <cstdlib>
#include <string>
#include <iterator>
#include <libunicode/convert.h>
#include <thread>
#include <span>

namespace corehost::corehost2
{

struct server_worker_context final
{
    win32::handle_view server;
    win32::handle_view event;
    win32::handle_view stop_event;
    server_dispatcher *dispatcher{};
    session *state{};
    conptyio::terminal_writer *writer{};
    win32::srwlock *mutex{};
    const std::atomic_bool *stop{};
    coreio::ctrl_delivery *delivery{};
};

inline DWORD WINAPI server_worker_entry(LPVOID raw)
{
    auto &context = *static_cast<server_worker_context *>(raw);
    coreio::win32_transport transport{context.server, context.event, context.stop_event};
    struct message_handler final
    {
        server_worker_context &context;

        bool handle(coreio::io_msg &message) const
        {
            std::vector<std::pair<DWORD, DWORD>> ctrl_events;
            {
                std::unique_lock guard{*context.mutex};
                if (!context.dispatcher->handle(message))
                {
                    return false;
                }
                context.dispatcher->take_ctrl_events(ctrl_events);
                const auto cursor = screen::coord{
                    static_cast<std::int32_t>(context.state->state().cursor().x),
                    static_cast<std::int32_t>(context.state->state().cursor().y),
                };
                bool rendered = false;
                if (context.state->take_alternate_changed() || context.state->take_viewport_changed())
                {
                    vtserializer::render_viewport(context.writer->buffer(), context.state->screen(),
                                                  cursor, &context.state->state());
                    context.state->screen().clear_damage();
                    rendered = true;
                }
                else if (context.state->screen().has_damage() ||
                         context.state->state().take_cursor_changed())
                {
                    vtserializer::render_damage(context.writer->buffer(), context.state->screen(),
                                                cursor, &context.state->state());
                    context.state->screen().clear_damage();
                    rendered = true;
                }
                if (context.state->take_echo_line_feed())
                {
                    context.writer->buffer().append_text(u8"\r\n");
                    rendered = true;
                }
                if (context.state->take_title_changed())
                {
                    vtserializer::append_window_title(context.writer->buffer(),
                                                      context.state->state().title);
                    rendered = true;
                }
                if (context.state->take_scrollback_cleared())
                {
                    vtserializer::append_erase_display(context.writer->buffer(), 2);
                    vtserializer::append_erase_display(context.writer->buffer(), 3);
                    rendered = true;
                }
                bool win32_input = true;
                if (context.state->take_win32_input_change(win32_input))
                {
                    vtserializer::append_private_mode(context.writer->buffer(), 9001, win32_input);
                    rendered = true;
                }
                if (rendered)
                {
                    context.writer->flush();
                }
            }
            for (const auto [event, pid] : ctrl_events)
            {
                context.delivery->deliver(pid, event);
            }
            return true;
        }
    };
    message_handler handler{context};
    coreio::run_server(transport, handler, *context.stop);
    return 0;
}

class session_input_sink final
{
  public:
    session_input_sink(session &state, conptyio::terminal_writer &writer,
                       win32::srwlock &mutex, server_session &console)
        : state_{state}, writer_{writer}, mutex_{mutex}, console_{console}
    {
    }

    void on_input_bytes(std::span<const std::byte> bytes)
    {
        std::unique_lock guard{mutex_};
        vtparser::decode_utf8(
            std::span<const char8_t>{reinterpret_cast<const char8_t *>(bytes.data()), bytes.size()},
            text_, pending_);
        if (console_.raw_pending())
        {
            console_.push_raw(std::span<const char>{reinterpret_cast<const char *>(bytes.data()),
                                                   bytes.size()});
        }
        state_.feed_input(text_, (state_.state().input_mode & 0x0200) != 0 &&
                                  !console_.raw_pending() && !console_.console_read_pending());
        (void)console_.complete_pending();
        for (const auto [mode, enabled] : state_.state().take_private_modes())
        {
            vtserializer::append_private_mode(writer_.buffer(), mode, enabled);
        }
        if (state_.state().take_keypad_changed())
        {
            writer_.buffer().append_text(state_.state().keypad_application ? u8"\x1b=" : u8"\x1b>");
        }
        const auto cursor = screen::coord{
            static_cast<std::int32_t>(state_.state().cursor().x),
            static_cast<std::int32_t>(state_.state().cursor().y),
        };
        if (state_.take_alternate_changed() || state_.take_viewport_changed())
        {
            vtserializer::render_viewport(writer_.buffer(), state_.screen(), cursor, &state_.state());
        }
        else
        {
            vtserializer::render_damage(writer_.buffer(), state_.screen(), cursor, &state_.state());
        }
        state_.screen().clear_damage();
        if (state_.take_echo_line_feed())
        {
            writer_.buffer().append_text(u8"\r\n");
        }
        if (state_.take_title_changed())
        {
            vtserializer::append_window_title(writer_.buffer(), state_.state().title);
        }
        if (state_.take_scrollback_cleared())
        {
            vtserializer::append_erase_display(writer_.buffer(), 2);
            vtserializer::append_erase_display(writer_.buffer(), 3);
        }
        bool win32_input = true;
        if (state_.take_win32_input_change(win32_input))
        {
            vtserializer::append_private_mode(writer_.buffer(), 9001, win32_input);
        }
        writer_.flush();
    }

    void on_input_eof()
    {
        std::unique_lock guard{mutex_};
        console_.on_input_eof();
    }

  private:
    session &state_;
    conptyio::terminal_writer &writer_;
    win32::srwlock &mutex_;
    server_session &console_;
    std::string pending_;
    std::u16string text_;
};

class session_signal_sink final
{
  public:
    session_signal_sink(session &state, conptyio::terminal_writer &writer,
                        conptyio::pseudo_window &window, win32::srwlock &mutex,
                        server_session &console)
        : state_{state}, writer_{writer}, window_{window}, mutex_{mutex}, console_{console}
    {
    }

    void on_signal(std::uint16_t id, std::span<const std::byte> payload)
    {
        std::unique_lock guard{mutex_};
        conptyio::apply_signal(id, payload, state_.screen(), state_.state(), window_);
        state_.queue_window_size(static_cast<std::int16_t>(state_.state().width()),
                                 static_cast<std::int16_t>(state_.state().height()));
        (void)console_.complete_pending();
        if (!state_.screen().has_damage())
        {
            return;
        }
        const auto cursor = screen::coord{
            static_cast<std::int32_t>(state_.state().cursor().x),
            static_cast<std::int32_t>(state_.state().cursor().y),
        };
        if (state_.take_alternate_changed() || state_.take_viewport_changed())
        {
            vtserializer::render_viewport(writer_.buffer(), state_.screen(), cursor, &state_.state());
        }
        else
        {
            vtserializer::render_damage(writer_.buffer(), state_.screen(), cursor, &state_.state());
        }
        state_.screen().clear_damage();
        if (state_.take_echo_line_feed())
        {
            writer_.buffer().append_text(u8"\r\n");
        }
        if (state_.take_title_changed())
        {
            vtserializer::append_window_title(writer_.buffer(), state_.state().title);
        }
        if (state_.take_scrollback_cleared())
        {
            vtserializer::append_erase_display(writer_.buffer(), 2);
            vtserializer::append_erase_display(writer_.buffer(), 3);
        }
        bool win32_input = true;
        if (state_.take_win32_input_change(win32_input))
        {
            vtserializer::append_private_mode(writer_.buffer(), 9001, win32_input);
        }
        writer_.flush();
    }

    void on_signal_eof()
    {
        std::unique_lock guard{mutex_};
        console_.on_input_eof();
    }

  private:
    session &state_;
    conptyio::terminal_writer &writer_;
    conptyio::pseudo_window &window_;
    win32::srwlock &mutex_;
    server_session &console_;
};
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
    state.state().output_mode = 0x0001 | 0x0002 | 0x0004;
    conptyio::terminal_writer writer{vt_out};
    conptyio::pseudo_window window{server};
    window.start();
    vtserializer::append_clear_screen(writer.buffer());
    vtserializer::append_cup(writer.buffer(), 0, 0);
    writer.buffer().append_text(u8"\x1b[?9001h");
    if (config.inherit_cursor)
    {
        vtserializer::append_request_cursor_position(writer.buffer());
    }
    writer.flush();
    win32::srwlock session_mutex;
    win32::basic_thread server_thread;
    std::atomic_bool server_stop{};

    server_session console_session{state.screen(), state.state(), state.input(), 0, server};
    console_session.set_window_handle(reinterpret_cast<std::uintptr_t>(window.handle()));
    if (config.attached_process_id != 0)
    {
        console_session.processes().add(config.attached_process_id);
    }
    server_dispatcher dispatcher{server, condrv_input, condrv_output, console_session};

    win32::event server_stop_event{win32::create_tag, true, false};
    coreio::ctrl_delivery ctrl_delivery;
    server_worker_context server_context{server, event, server_stop_event.view(), &dispatcher, &state, &writer,
                                         &session_mutex, &server_stop, &ctrl_delivery};
    if (server.valid())
    {
        server_thread = win32::basic_thread{&server_worker_entry, &server_context,
                                            {.name = L"corehost2-server"}};
    }

    session_input_sink input_sink{state, writer, session_mutex, console_session};
    conptyio::vt_input_thread input_thread{vt_in, input_sink};
    if (vt_in.valid())
    {
        input_thread.start();
    }

    session_signal_sink signal_sink{state, writer, window, session_mutex, console_session};
    conptyio::pty_signal_thread pty_signal_thread{signal_pipe, signal_sink};
    if (signal_pipe.valid())
    {
        pty_signal_thread.start();
    }

    std::array<HANDLE, 4> waits{};
    DWORD wait_count = 0;
    if (input_thread.view().valid())
    {
        waits[wait_count++] = input_thread.view().get();
    }
    if (server_thread.valid())
    {
        waits[wait_count++] = server_thread.get();
    }
    if (pty_signal_thread.view().valid())
    {
        waits[wait_count++] = pty_signal_thread.view().get();
    }
    if (window.close_requested().valid())
    {
        waits[wait_count++] = window.close_requested().get();
    }
    if (wait_count == 0)
    {
        std::abort();
    }
    const auto wait = ::WaitForMultipleObjects(wait_count, waits.data(), FALSE, INFINITE);
    if (wait == WAIT_FAILED)
    {
        win32::throw_last_error();
    }

    input_thread.stop_and_join();
    pty_signal_thread.stop_and_join();
    if (server_thread.valid())
    {
        server_stop.store(true, std::memory_order_relaxed);
        (void)::SetEvent(server_stop_event.get());
        (void)::CancelSynchronousIo(server_thread.get());
        server_thread.join();
    }
}

} // namespace corehost::corehost2