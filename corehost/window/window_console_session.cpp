#include "window_console_session.hpp"

#include <cassert>

namespace corehost::window
{

window_console_session::window_console_session(win32::handle_view server, short width, short height)
    : server_{server}, screen_{static_cast<std::size_t>(width), static_cast<std::size_t>(height)},
      state_{static_cast<std::size_t>(width), static_cast<std::size_t>(height)}, input_{}, processes_{},
      payload_{server}, api_{screen_, state_, &input_, 0, &processes_, &payload_},
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

void window_console_session::run()
{
    coreio::win32_transport transport{server_, input_event_.view(), stop_event_.view()};
    message_handler handler{api_};
    coreio::run_server(transport, handler, stop_);
}

} // namespace corehost::window
