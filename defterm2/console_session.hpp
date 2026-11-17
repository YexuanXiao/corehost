#pragma once

#include "console_api.hpp"
#include "console_state.hpp"
#include "input.hpp"
#include "io_state.hpp"
#include "pseudo_window.hpp"
#include "process_registry.hpp"
#include "vt_parser.hpp"
#include "vtstate.hpp"
#include "apply.hpp"

#include "condrv_io.hpp"
#include "os/Console/conmsgl1.h"
#include "server_loop.hpp"
#include "win32_transport.hpp"

#include "raw_buffer.hpp"
#include "win32/event.hpp"
#include "win32/handle.hpp"
#include "win32/srwlock.hpp"
#include "win32/thread.hpp"

#include <Windows.h>

#include <atomic>
#include <cassert>
#include <cstdint>
#include <array>
#include <span>
#include "win32/error.hpp"

namespace corehost::defterm2
{

// Console-only ConDrv server. It has no vt_in, vt_out, or signal handles:
// defterm2 serves the Console API directly and uses only the ConDrv server
// plus the InputAvailableEvent.
class console_session final
{
  public:
    console_session(win32::handle_view server, win32::handle_view event, short width, short height)
        : server_{server}, event_{event}, width_{width}, height_{height},
          screen_{static_cast<std::size_t>(width), static_cast<std::size_t>(height)},
          state_{static_cast<std::size_t>(width), static_cast<std::size_t>(height)}, window_{server},
          processes_{}, payload_{server}, io_state_{server, {}, {}, processes_},
          api_{screen_, state_, &input_, 0, &processes_, &payload_}
    {
        state_.output_mode = 0x0001 | 0x0002 | 0x0004;
        state_.measurement = screen::text_measurement::graphemes;
        state_.ambiguous_is_wide = true;
        window_.start();
        api_.set_window_handle(reinterpret_cast<std::uintptr_t>(window_.handle()));
    }

    console_session(const console_session &) = delete;
    console_session &operator=(const console_session &) = delete;

    void run()
    {
        win32::event stop_event{win32::create_tag, true, false};
        std::atomic_bool stop{};
        coreio::win32_transport transport{server_, event_, stop_event.view()};
        worker_context context{transport, *this, stop};
        auto worker = win32::basic_thread{&console_session::worker_entry, &context, {.name = L"defterm2-console"}};

        const std::array waits{window_.close_requested().get(), worker.get()};
        const auto result = ::WaitForMultipleObjects(static_cast<DWORD>(waits.size()), waits.data(), FALSE, INFINITE);
        if (result == WAIT_FAILED)
        {
            win32::throw_last_error();
        }

        stop.store(true, std::memory_order_relaxed);
        (void)::SetEvent(stop_event.get());
        (void)::CancelSynchronousIo(worker.get());
        worker.join();
    }

    bool handle(coreio::io_msg &message)
    {
        switch (message.descriptor.Function)
        {
        case CONSOLE_IO_CONNECT: {
            apply_connect_info(message);
            const bool was_connected = io_state_.connected();
            io_state_.handle_connect(message);
            // defterm1 pre-registers the attached client before starting its
            // local session. Keep the same behavior here instead of relying
            // on a later process-list query racing the CONNECT handler.
            processes_.add(static_cast<std::uintptr_t>(message.descriptor.Process));
            api_.set_input_object(io_state_.input_id());
            api_.set_input_handle(reinterpret_cast<std::uintptr_t>(io_state_.input().get()));
            api_.set_output_object(io_state_.output_id());
            api_.set_output_handle(reinterpret_cast<std::uintptr_t>(io_state_.output().get()));
            api_.set_alternate_object(io_state_.alternate_output_id());
            if (!was_connected)
            {
                coreio::prepare_connection_completion(message, io_state_.input(), io_state_.output(),
                                                       static_cast<ULONG_PTR>(message.descriptor.Process));
            }
            // Complete CONNECT immediately. The client may issue its first
            // Console API call before the next READ_IO can piggyback this
            // completion; leaving it pending turns that API into a race.
            coreio::complete_io(server_, message.complete);
            return false;
        }
        case CONSOLE_IO_CREATE_OBJECT:
            io_state_.handle_create_object(message);
            api_.set_input_object(io_state_.input_id());
            api_.set_input_handle(reinterpret_cast<std::uintptr_t>(io_state_.input().get()));
            api_.set_output_object(io_state_.output_id());
            api_.set_output_handle(reinterpret_cast<std::uintptr_t>(io_state_.output().get()));
            api_.set_alternate_object(io_state_.alternate_output_id());
            return true;
        case CONSOLE_IO_CLOSE_OBJECT:
            io_state_.handle_close_object(message);
            api_.set_input_object(io_state_.input_id());
            api_.set_output_object(io_state_.output_id());
            api_.set_alternate_object(io_state_.alternate_output_id());
            return true;
        case CONSOLE_IO_DISCONNECT:
            io_state_.handle_disconnect(message);
            return true;
        case CONSOLE_IO_RAW_FLUSH:
            io_state_.handle_raw_flush(message);
            coreio::complete_io(server_, message.complete);
            return false;
        case CONSOLE_IO_RAW_WRITE: {
            const auto payload = payload_.read(message, 0);
            write_raw(std::span<const char>{reinterpret_cast<const char *>(payload.data()), payload.size()});
            coreio::prepare_completion(message, 0, payload.size());
            coreio::complete_io(server_, message.complete);
            return false;
        }
        case CONSOLE_IO_RAW_READ:
            // defterm2 has no terminal input source. Report EOF instead of
            // inventing a vt_in handle or leaving the request suspended.
            coreio::prepare_completion(message, 0, 0, nullptr, 0);
            coreio::complete_io(server_, message.complete);
            return false;
        case CONSOLE_IO_USER_DEFINED: {
            const bool handled = api_.handle(message);
            // Pending reads deliberately have no completion yet. Complete
            // synchronous API calls before the next READ_IO reuses io_msg.body.
            if (handled)
            {
                coreio::complete_io(server_, message.complete);
            }
            return false;
        }
        default:
            coreio::prepare_completion(message, coreio::error::status_illegal_function);
            return true;
        }
    }

    bool should_flush() const noexcept
    {
        return false;
    }

    void flush() noexcept
    {
    }

  private:
    struct worker_context final
    {
        coreio::win32_transport &transport;
        console_session &session;
        std::atomic_bool &stop;
    };

    static DWORD WINAPI worker_entry(LPVOID raw)
    {
        auto &context = *static_cast<worker_context *>(raw);
        coreio::run_server(context.transport, context.session, context.stop);
        return 0;
    }

    void apply_connect_info(const coreio::io_msg &message)
    {
        if (message.descriptor.InputSize < sizeof(CONSOLE_SERVER_MSG))
        {
            return;
        }

        const auto &info = *reinterpret_cast<const CONSOLE_SERVER_MSG *>(message.body);
        if (info.ScreenBufferSize.X > 0 && info.ScreenBufferSize.Y > 0)
        {
            width_ = info.ScreenBufferSize.X;
            height_ = info.ScreenBufferSize.Y;
            screen_.resize(static_cast<std::size_t>(width_), static_cast<std::size_t>(height_));
            state_.resize(static_cast<std::size_t>(width_), static_cast<std::size_t>(height_));
        }

        if (info.TitleLength <= sizeof(info.Title) - sizeof(char16_t) &&
            (info.TitleLength % sizeof(char16_t)) == 0)
        {
            const auto length = info.TitleLength / sizeof(char16_t);
            state_.title.assign(reinterpret_cast<const char16_t *>(info.Title), length);
            if (window_.handle() != nullptr)
            {
                (void)::SetWindowTextW(window_.handle(), reinterpret_cast<LPCWSTR>(state_.title.c_str()));
            }
        }
    }

    void write_raw(std::span<const char> bytes)
    {
        if (bytes.empty())
        {
            return;
        }

        text_.resize(bytes.size());
        const auto units = ::MultiByteToWideChar(state_.output_code_page, 0, bytes.data(),
                                                 static_cast<int>(bytes.size()),
                                                 reinterpret_cast<LPWSTR>(text_.data()),
                                                 static_cast<int>(text_.size()));
        assert(units > 0);
        text_.resize(static_cast<std::size_t>(units));

        std::u16string_view input{text_};
        while (!input.empty())
        {
            const auto result = parser_.parse(input);
            input.remove_prefix(result.consumed);
            if (result.id == vtparser::vt_message_id::continue_)
            {
                break;
            }
            if ((result.id == vtparser::vt_message_id::apc_sequence ||
                 result.id == vtparser::vt_message_id::dcs_sequence) &&
                applier_.apply_image(result.raw_sequence, screen_, state_))
            {
                parser_.reset();
                continue;
            }
            (void)vtstate::apply(result.id, result.message, screen_, state_, applier_);
            parser_.reset();
        }
    }

    win32::handle_view server_;
    win32::handle_view event_;
    short width_{};
    short height_{};
    screen::screen_buffer screen_;
    screen::console_state state_;
    consoleinput::input_queue input_;
    conptyio::pseudo_window window_;
    coreio::process_registry processes_;
    coreio::input_payload_reader payload_;
    coreio::io_state io_state_;
    consoleapi::console_api api_;
    vtparser::raw_u16_buffer raw_;
    vtparser::vt_parser parser_{raw_};
    vtstate::state_applier applier_{};
    vtparser::raw_u16_string text_;
};

} // namespace corehost::defterm2
