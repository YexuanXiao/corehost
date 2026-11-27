#pragma once

#include "win32/event.hpp"

#include <Windows.h>

#include <atomic>
#include <cstdint>

namespace corehost::window
{

// A small, explicit hand-off from the UI thread to the console session.
// The UI only records the control event and signals the session; the session
// owns cancellation of pending reads and delivery to the registered processes.
class window_ctrl_state final
{
  public:
    window_ctrl_state() : ready_{win32::create_tag, false, false}
    {
    }

    void request(DWORD event, HWND hwnd) noexcept
    {
        event_.store(event, std::memory_order_relaxed);
        hwnd_.store(hwnd, std::memory_order_relaxed);
        ready_.set();
    }

    [[nodiscard]] DWORD take_event() noexcept
    {
        return event_.load(std::memory_order_relaxed);
    }

    [[nodiscard]] HWND take_hwnd() noexcept
    {
        return hwnd_.load(std::memory_order_relaxed);
    }

    [[nodiscard]] win32::event &ready() noexcept
    {
        return ready_;
    }

  private:
    win32::event ready_;
    std::atomic<DWORD> event_{CTRL_C_EVENT};
    std::atomic<HWND> hwnd_{};
};

} // namespace corehost::window
