#include "window_console_session.hpp"

#include <cassert>

namespace corehost::window
{

window_console_session::window_console_session(win32::handle_view server, short width, short height)
    : server_{server}, screen_{static_cast<std::size_t>(width), static_cast<std::size_t>(height)},
      state_{static_cast<std::size_t>(width), static_cast<std::size_t>(height)}, input_{}, processes_{},
      io_state_{server, {}, {}, processes_}, payload_{server},
      api_{screen_, state_, &input_, 0, &processes_, &payload_},
      input_event_{win32::create_tag, true, false}, stop_event_{win32::create_tag, true, false}
{
    state_.output_mode = 0x0001 | 0x0002 | 0x0004;
    state_.measurement = screen::text_measurement::graphemes;
    state_.ambiguous_is_wide = true;
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
}

void window_console_session::set_window_handle(HWND window) noexcept
{
    api_.set_window_handle(reinterpret_cast<std::uintptr_t>(window));
}

DWORD WINAPI window_console_session::thread_entry(LPVOID self)
{
    static_cast<window_console_session *>(self)->run();
    return 0;
}

bool window_console_session::handle(coreio::io_msg &message)
{
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
        return false;
    }
    default:
        coreio::prepare_completion(message, coreio::error::status_illegal_function);
        return true;
    }
}

void window_console_session::run()
{
    coreio::win32_transport transport{server_, input_event_.view(), stop_event_.view()};
    message_handler handler{*this};
    coreio::run_server(transport, handler, stop_);
}

} // namespace corehost::window
