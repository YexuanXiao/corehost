#pragma once

#include "config.hpp"
#include "win32/handle.hpp"
#include "window_host.hpp"

#include "win32/com_apartment.hpp"
#include "win32/event.hpp"
#include "win32/srwlock.hpp"
#include "win32/thread.hpp"

#include <Windows.h>

#include <cassert>

namespace corehost::window
{

// Owns the visible window and its message loop on a dedicated UI thread.
// This is the window-mode analogue of conptyio::pseudo_window: the caller
// keeps the console/client lifetime and only starts/joins the UI.
class window_ui final
{
  public:
    window_ui(console &console, win32::srwlock &console_mutex, win32::handle_view client_process,
              win32::wcstring_view title, const corehost_config &config, win32::handle_view damage_event)
        : console_{console}, console_mutex_{console_mutex}, client_process_{client_process}, title_{title},
          config_{config}, damage_event_{damage_event}
    {
        assert(!title_.empty());
        assert(client_process_.valid());
    }

    ~window_ui()
    {
        stop_and_join();
    }

    window_ui(const window_ui &) = delete;
    window_ui &operator=(const window_ui &) = delete;

    void start()
    {
        assert(!thread_.valid());
        thread_ = win32::basic_thread{&window_ui::thread_entry, this, {.name = L"corehost-window-ui"}};
        ready_.wait();
    }

    // Waits for the UI event channel. Fatal errors also exit normally;
    // the caller only needs to know that the UI has finished.
    void wait()
    {
        assert(thread_.valid());
        HANDLE events[] = {close_requested_.get(), fatal_.get()};
        const auto result = ::WaitForMultipleObjects(2, events, FALSE, INFINITE);
        if (result == WAIT_FAILED)
        {
            win32::throw_last_error();
        }
        thread_.join();
        thread_ = {};
    }

    void stop_and_join()
    {
        if (!thread_.valid())
        {
            return;
        }
        if (hwnd_ != nullptr)
        {
            (void)::PostMessageW(hwnd_, WM_CLOSE, 0, 0);
        }
        thread_.join();
        thread_ = {};
    }

  private:
    static DWORD WINAPI thread_entry(LPVOID self)
    {
        static_cast<window_ui *>(self)->run_ui();
        return 0;
    }

    void run_ui() noexcept
    {
        try
        {
            win32::com_apartment apartment{COINIT_APARTMENTTHREADED};
            window_host host{title_, console_, console_mutex_, client_process_, config_, damage_event_};
            hwnd_ = host.window();
            ready_.set();
            host.run();
            close_requested_.set();
        }
        catch (...)
        {
            fatal_.set();
            ready_.set();
        }
    }

    console &console_;
    win32::srwlock &console_mutex_;
    win32::handle_view client_process_;
    win32::wcstring_view title_;
    const corehost_config &config_;
    win32::handle_view damage_event_{};

    win32::event ready_{win32::create_tag, true, false};
    win32::event close_requested_{win32::create_tag, true, false};
    win32::event fatal_{win32::create_tag, true, false};
    HWND hwnd_{};
    win32::basic_thread thread_;
};

} // namespace corehost::window
