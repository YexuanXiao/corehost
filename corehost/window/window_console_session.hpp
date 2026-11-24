#pragma once

#include "console_api.hpp"
#include "console_state.hpp"
#include "input.hpp"
#include "input_payload.hpp"
#include "io_state.hpp"
#include "process_registry.hpp"
#include "server_loop.hpp"
#include "win32_transport.hpp"
#include "screen_buffer.hpp"
#include "win32/event.hpp"
#include "win32/handle.hpp"
#include "win32/thread.hpp"

#include <atomic>
#include <cstdint>

namespace corehost::window
{

// In-process console server for window mode. It owns the ConDrv server loop
// and the console state used by the window renderer. It intentionally does
// not depend on defterm or libcorepty.
class window_console_session final
{
  public:
    window_console_session(win32::handle_view server, short width, short height);
    ~window_console_session();

    window_console_session(const window_console_session &) = delete;
    window_console_session &operator=(const window_console_session &) = delete;

    void start();
    void stop();

    void set_window_handle(HWND window) noexcept;

  private:
    struct message_handler final
    {
        window_console_session &owner;

        [[nodiscard]] bool handle(coreio::io_msg &message)
        {
            return owner.handle(message);
        }

        void flush() const noexcept
        {
        }

        [[nodiscard]] bool should_flush() const noexcept
        {
            return false;
        }
    };

    static DWORD WINAPI thread_entry(LPVOID self);
    void run();
    bool handle(coreio::io_msg &message);

    win32::handle_view server_;
    screen::screen_buffer screen_;
    screen::console_state state_;
    consoleinput::input_queue input_;
    coreio::process_registry processes_;
    coreio::io_state io_state_;
    coreio::input_payload_reader payload_;
    consoleapi::console_api api_;
    win32::event input_event_;
    win32::event stop_event_;
    win32::basic_thread thread_;
    std::atomic_bool stop_{};
};

} // namespace corehost::window
