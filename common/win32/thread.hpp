#pragma once

// ── Win32 thread RAII wrapper ──────────────────────────────────
//
// Provides a simple RAII wrapper around CreateThread/CloseHandle.
// The thread handle is closed on destruction (the thread continues
// running independently).
//
// Design:
// - Move-only (no copy).
// - Throws win32::error on failure.
// - Compatible with win32::handle_view.

#include "win32/handle.hpp"
#include "win32/error.hpp"

#include <Windows.h>

namespace win32
{

// ── Thread RAII wrapper ────────────────────────────────────────
//
// Wraps a thread handle created via CreateThread.
// On destruction, closes the handle (does NOT terminate the thread).
//
// Usage:
//   DWORD WINAPI MyThreadProc(LPVOID param) { ... return 0; }
//
//   int data = 42;
//   auto t = win32::basic_thread{MyThreadProc, &data};
//   t.release();  // or let destructor close the handle
class basic_thread
{
  public:
    // ── Construction ──────────────────────────────────────────
    //
    // Creates a new thread. Throws win32::error on failure.
    // @param thread_id_out  Optional output parameter for the thread ID.
    basic_thread(LPTHREAD_START_ROUTINE start_address, LPVOID parameter = nullptr, DWORD *thread_id_out = nullptr,
                 DWORD creation_flags = 0, DWORD stack_size = 0, LPSECURITY_ATTRIBUTES thread_attributes = nullptr)
    {
        DWORD thread_id{};
        HANDLE raw =
            ::CreateThread(thread_attributes, stack_size, start_address, parameter, creation_flags, &thread_id);

        if (raw == nullptr)
        {
            win32::throw_last_error();
        }

        _handle = win32::handle{raw};
        if (thread_id_out != nullptr)
        {
            *thread_id_out = thread_id;
        }
    }

    // ── Default (no thread) ──────────────────────────────────
    basic_thread() noexcept = default;

    ~basic_thread() noexcept = default;

    basic_thread(const basic_thread &) = delete;
    basic_thread &operator=(const basic_thread &) = delete;

    basic_thread(basic_thread &&) noexcept = default;
    basic_thread &operator=(basic_thread &&) noexcept = default;

    // ── Accessors ────────────────────────────────────────────
    [[nodiscard]] bool valid() const noexcept
    {
        return _handle.valid();
    }

    [[nodiscard]] HANDLE get() const noexcept
    {
        return _handle.get();
    }

    [[nodiscard]] win32::handle_view view() const noexcept
    {
        return _handle.view();
    }

    // ── Operations ───────────────────────────────────────────

    void *release() noexcept
    {
        return _handle.release();
    }

  private:
    win32::handle _handle{};
};

} // namespace win32
