#pragma once

// ── Win32 event (synchronization object) RAII wrapper ──────────
//
// Provides a standard-library-style RAII wrapper around Win32
// manual-reset and auto-reset events created via CreateEventW.
//
// Design:
// - Move-only (no copy).
// - Throws win32::error on failure.
// - Compatible with win32::handle_view for wait APIs.
//
// Replaces wil::unique_event_nothrow. Unlike the WIL version,
// this wrapper throws on failure instead of silently returning
// an invalid handle.

#include "win32/handle.hpp"
#include "win32/error.hpp"
#include "win32/string.hpp"
#include "win32/tags.hpp"

#include <Windows.h>

namespace win32
{

// ── Event RAII wrapper ─────────────────────────────────────────
//
// Usage:
//   // Create a new event:
//   win32::event ev{win32::create_tag, true, false}; // manual-reset, unsignaled
//
//   // Open an existing named event:
//   win32::event ev{win32::open_tag, L"Global\\MyEvent", EVENT_ALL_ACCESS};
//
//   ev.wait();
//   ev.set();
//   ev.reset();
class event
{
  public:
    // ── Create new event ────────────────────────────────────
    event(create_tag_t /*tag*/, bool manual_reset, bool initial_state, win32::wcstring_view name = {})
    {
        HANDLE raw = ::CreateEventW(nullptr, manual_reset ? TRUE : FALSE, initial_state ? TRUE : FALSE, name.c_str());
        if (raw == nullptr)
            win32::throw_last_error();
        _handle = win32::handle{raw};
    }

    // ── Open existing named event ───────────────────────────
    event(open_tag_t /*tag*/, win32::wcstring_view name, DWORD desired_access = EVENT_ALL_ACCESS,
          bool inherit_handle = false)
    {
        HANDLE raw = ::OpenEventW(desired_access, inherit_handle ? TRUE : FALSE, name.c_str());
        if (raw == nullptr)
        {
            win32::throw_last_error();
        }
        _handle = win32::handle{raw};
    }

    // ── Default (no-event) ───────────────────────────────────
    event() noexcept = default;

    ~event() noexcept = default;

    event(const event &) = delete;
    event &operator=(const event &) = delete;

    event(event &&) noexcept = default;
    event &operator=(event &&) noexcept = default;

    // ── Access ───────────────────────────────────────────────
    [[nodiscard]] bool valid() const noexcept
    {
        return _handle.valid();
    }

    [[nodiscard]] win32::handle_view view() const noexcept
    {
        return _handle.view();
    }

    [[nodiscard]] HANDLE get() const noexcept
    {
        return _handle.get();
    }

    [[nodiscard]] HANDLE *put() noexcept
    {
        return _handle.put();
    }

    // ── Operations ───────────────────────────────────────────

    // Signal the event (SetEvent).
    void set()
    {
        if (::SetEvent(_handle.get()) == FALSE)
        {
            win32::throw_last_error();
        }
    }

    // Unsignal the event (ResetEvent).
    void reset()
    {
        if (::ResetEvent(_handle.get()) == FALSE)
        {
            win32::throw_last_error();
        }
    }

    // Wait for the event to be signaled (WaitForSingleObject, infinite).
    void wait() const
    {
        const auto result = ::WaitForSingleObject(_handle.get(), INFINITE);
        if (result == WAIT_FAILED)
        {
            win32::throw_last_error();
        }
    }

    void lock()
    {
        wait();
    }

    void unlock()
    {
        set();
    }

    // Close and destroy the event handle.
    void clear() noexcept
    {
        _handle = win32::handle{};
    }

    // Release ownership of the handle (caller must close it).
    void *release() noexcept
    {
        return _handle.release();
    }

  private:
    win32::handle _handle{};
};

} // namespace win32
