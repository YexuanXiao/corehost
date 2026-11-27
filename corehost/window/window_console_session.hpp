#pragma once

#include "config.hpp"
#include "console_api.hpp"
#include "console.hpp"
#include "console_state.hpp"
#include "apply.hpp"
#include "input.hpp"
#include "input_payload.hpp"
#include "io_state.hpp"
#include "process_registry.hpp"
#include "server_loop.hpp"
#include "win32_transport.hpp"
#include "screen_buffer.hpp"
#include "win32/event.hpp"
#include "win32/handle.hpp"
#include "win32/srwlock.hpp"
#include "win32/thread.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace corehost::window
{

// In-process console server for window mode. It owns the ConDrv server loop
// and the console state used by the window renderer. It intentionally does
// not depend on defterm or libcorepty.
class window_console_session final
{
  public:
    window_console_session(win32::handle_view server, console &console, win32::srwlock &console_mutex,
                           coreio::process_registry &processes, const corehost_config &config);
    ~window_console_session();

    window_console_session(const window_console_session &) = delete;
    window_console_session &operator=(const window_console_session &) = delete;

    void start();
    void stop();

    [[nodiscard]] win32::event &output_event() noexcept
    {
        return output_event_;
    }



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
    static DWORD WINAPI input_thread_entry(LPVOID self);
    void run();
    void run_input();
    bool handle(coreio::io_msg &message);
    bool handle_write_console(coreio::io_msg &message);
    void apply_vt(std::u16string_view text);
    void complete_pending();
    void handle_read_termination();
    void complete_pending_raw();
    void complete_raw_locked();

    win32::handle_view server_;
    console &console_;
    win32::srwlock &console_mutex_;
    coreio::io_msg pending_message_{};
    coreio::io_msg pending_raw_message_{};
    consoleapi::console_api::pending_read_kind pending_kind_{};
    bool pending_active_{};
    bool pending_raw_active_{};
    coreio::process_registry &processes_;
    coreio::io_state io_state_;
    coreio::input_payload_reader payload_;
    consoleapi::console_api api_;
    vtparser::raw_u16buffer vt_raw_;
    vtparser::vt_parser vt_parser_{vt_raw_};
    vtstate::state_applier vt_applier_{};
    vtparser::raw_u16buffer vt_text_;
    win32::event input_event_;
    win32::event output_event_{win32::create_tag, false, false};
    win32::event stop_event_;
    win32::basic_thread thread_;
    win32::basic_thread input_thread_;
    std::atomic_bool stop_{};
};

} // namespace corehost::window
