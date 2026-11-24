#include "window_console_session.hpp"

#include "win32/error.hpp"

#include <cassert>
#include <utility>

namespace corehost::window
{

window_console_session::window_console_session(win32::handle_view server, console_screen &screen)
    : server_{server}, screen_source_{screen}, processes_{}, io_state_{server, {}, {}, processes_},
      payload_{server}, api_{screen_source_.buffer(), screen_source_.state(), &screen_source_.input(), 0, &processes_,
                        &payload_},
      input_event_{win32::create_tag, true, false}, stop_event_{win32::create_tag, true, false}
{
    auto &state = screen_source_.state();
    state.output_mode = 0x0001 | 0x0002 | 0x0004;
    state.measurement = screen::text_measurement::graphemes;
    state.ambiguous_is_wide = true;
}

window_console_session::~window_console_session()
{
    stop();
}

void window_console_session::start()
{
    assert(server_.valid());
    assert(!thread_.valid());

    coreio::set_server_info(server_, input_event_.view());
    thread_ = win32::basic_thread{&window_console_session::thread_entry, this, {.name = L"corehost-window-console"}};
    screen_source_.set_worker(thread_.get());
}

void window_console_session::stop()
{
    if (!thread_.valid())
    {
        return;
    }

    stop_.store(true, std::memory_order_relaxed);
    stop_event_.set();
    (void)::CancelSynchronousIo(thread_.get());
    thread_.join();
    thread_ = {};
    screen_source_.set_worker(nullptr);
}

DWORD WINAPI window_console_session::thread_entry(LPVOID self)
{
    static_cast<window_console_session *>(self)->run();
    return 0;
}

bool window_console_session::handle(coreio::io_msg &message)
{
    console_screen::write_lock lock{screen_source_};

    switch (message.descriptor.Function)
    {
    case CONSOLE_IO_CONNECT:
        io_state_.handle_connect(message);
        api_.set_input_object(io_state_.input_id());
        api_.set_input_handle(reinterpret_cast<std::uintptr_t>(io_state_.input().get()));
        api_.set_output_object(io_state_.output_id());
        api_.set_output_handle(reinterpret_cast<std::uintptr_t>(io_state_.output().get()));
        api_.set_alternate_object(io_state_.alternate_output_id());
        coreio::complete_io(server_, message.complete);
        return false;
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
        coreio::prepare_completion(message, 0, payload.size());
        coreio::complete_io(server_, message.complete);
        return false;
    }
    case CONSOLE_IO_RAW_READ:
        return false;
    case CONSOLE_IO_USER_DEFINED: {
        const bool handled = api_.handle(message);
        if (handled)
        {
            coreio::complete_io(server_, message.complete);
        }
        else
        {
            const auto kind = api_.take_pending_read_kind();
            if (kind != consoleapi::console_api::pending_read_kind::none)
            {
                pending_message_ = message;
                pending_kind_ = kind;
                pending_active_ = true;
            }
        }
        return false;
    }
    default:
        coreio::prepare_completion(message, coreio::error::status_illegal_function);
        return true;
    }
}

void window_console_session::complete_pending()
{
    if (!pending_active_)
    {
        return;
    }

    console_screen::write_lock lock{screen_source_};
    bool completed = false;
    if (pending_kind_ == consoleapi::console_api::pending_read_kind::console_input)
    {
        completed = api_.read_console_input(pending_message_, false);
    }
    else
    {
        completed = api_.resume_read(pending_message_);
    }

    if (completed)
    {
        coreio::complete_io(server_, pending_message_.complete);
        pending_active_ = false;
        pending_kind_ = consoleapi::console_api::pending_read_kind::none;
    }
}

void window_console_session::run()
{
    coreio::io_msg message{};
    CD_IO_COMPLETE *previous = nullptr;

    for (;;)
    {
        if (stop_.load(std::memory_order_relaxed))
        {
            return;
        }

        if (pending_active_)
        {
            complete_pending();
        }

        const auto result = coreio::read_io_try(server_, previous, message);
        if (result == coreio::read_io_result::got_message)
        {
            previous = handle(message) ? &message.complete : nullptr;
            continue;
        }
        if (result == coreio::read_io_result::disconnected)
        {
            return;
        }

        previous = nullptr;
        HANDLE waits[] = {input_event_.get(), screen_source_.input_event().get(), stop_event_.get()};
        const auto wait = ::WaitForMultipleObjects(3, waits, FALSE, INFINITE);
        if (wait == WAIT_FAILED)
        {
            win32::throw_last_error();
        }
        if (wait == WAIT_OBJECT_0 + 2)
        {
            return;
        }
        if (wait == WAIT_OBJECT_0 + 1)
        {
            screen_source_.input_event().reset();
        }
    }
}

} // namespace corehost::window
