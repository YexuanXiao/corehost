#pragma once

// ── Win32 thread RAII wrapper ──────────────────────────────────
//
// Provides a simple RAII wrapper around CreateThread/CloseHandle.
// The thread handle is closed on destruction (the thread continues
// running independently, so callers must join before destruction).
//
// Design:
// - Move-only (no copy).
// - Throws win32::error on failure.
// - Compatible with win32::handle_view.
// - Optional thread name: applied from inside the new thread before
//   the entry point runs, so the name is visible to debuggers as
//   early as possible and a naming failure cannot leak a running
//   thread (it aborts, since a failed SetThreadDescription is fatal
//   by project policy).
// - join()/join_for() never close the handle; the destructor does.
//   CancelSynchronousIo() needs the handle, so it must stay valid
//   until the thread has been joined.
//
// Usage:
//   DWORD WINAPI MyThreadProc(LPVOID param) { ... return 0; }
//
//   int data = 42;
//   auto t = win32::basic_thread{MyThreadProc, &data, {.name = L"console-input"}};
//   t.join();

#include "win32/error.hpp"
#include "win32/handle.hpp"

#include <Windows.h>

#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace win32
{

// ── Thread naming ─────────────────────────────────────────────
//
// Sets the debugger/ETW-visible description of a thread. Throws on
// failure: naming is part of diagnostics, and a silent failure would
// make thread-level debugging guesswork.
inline void set_thread_name(win32::handle_view thread, std::wstring_view name)
{
    if (name.empty())
        return;

    // SetThreadDescription requires the wide string to be null-terminated.
    const std::wstring terminated{name};
    if (::SetThreadDescription(thread.get(), terminated.c_str()) == FALSE)
        win32::throw_last_error();
}

inline void set_thread_name(win32::handle_view thread, const wchar_t *name)
{
    if (name != nullptr)
        set_thread_name(thread, std::wstring_view{name});
}

// ── Thread RAII wrapper ────────────────────────────────────────
class basic_thread
{
  public:
    // ── Creation options ──────────────────────────────────────
    struct options
    {
        // Debugger/ETW-visible name; empty means unnamed.
        std::wstring_view name{};
        DWORD creation_flags = 0;
        DWORD stack_size = 0;
        LPSECURITY_ATTRIBUTES attributes = nullptr;
        // Optional output for the thread id (zero if not requested).
        DWORD *thread_id_out = nullptr;
    };

    // ── Construction ──────────────────────────────────────────
    //
    // Creates a new thread. Throws win32::error on failure.
    basic_thread(LPTHREAD_START_ROUTINE start_address, LPVOID parameter = nullptr, options opts = {})
    {
        // The start context is owned by the new thread once CreateThread
        // succeeds; on failure it is released here.
        auto context = std::make_unique<start_context>(start_address, parameter, opts.name);

        DWORD thread_id{};
        HANDLE raw = ::CreateThread(opts.attributes, opts.stack_size, &basic_thread::trampoline, context.get(),
                                    opts.creation_flags, &thread_id);

        if (raw == nullptr)
            win32::throw_last_error();

        context.release();
        _handle = win32::handle{raw};
        _id = thread_id;
        if (opts.thread_id_out != nullptr)
            *opts.thread_id_out = thread_id;
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

    [[nodiscard]] DWORD id() const noexcept
    {
        return _id;
    }

    // ── Waiting ──────────────────────────────────────────────
    //
    // Waits for the thread to exit. join_for returns false on timeout
    // (the thread keeps running) and throws only on unexpected wait
    // failures. The handle stays valid after join so the caller may
    // still inspect the thread id.
    void join()
    {
        if (!valid())
            return;
        (void)join_for(INFINITE);
    }

    [[nodiscard]] bool join_for(DWORD timeout_ms)
    {
        if (!valid())
            return true;

        const DWORD result = ::WaitForSingleObject(_handle.get(), timeout_ms);
        if (result == WAIT_OBJECT_0)
            return true;
        if (result == WAIT_TIMEOUT)
            return false;

        win32::throw_last_error();
    }

    // ── Operations ───────────────────────────────────────────
    void *release() noexcept
    {
        return _handle.release();
    }

  private:
    // Per-thread start data. Heap allocated so a basic_thread object
    // can be moved while the thread is starting.
    struct start_context
    {
        start_context(LPTHREAD_START_ROUTINE entry, LPVOID parameter, std::wstring_view thread_name)
            : entry_point{entry}, parameter{parameter}, name{thread_name}
        {
        }

        LPTHREAD_START_ROUTINE entry_point;
        LPVOID parameter;
        std::wstring name;
    };

    static DWORD WINAPI trampoline(LPVOID raw_context)
    {
        // The context belongs to this thread; release it even if the
        // entry point exits through a fatal error path.
        std::unique_ptr<start_context> context{static_cast<start_context *>(raw_context)};

        if (!context->name.empty())
            set_thread_name(win32::handle_view{::GetCurrentThread()}, context->name);

        return context->entry_point(context->parameter);
    }

    win32::handle _handle{};
    DWORD _id = 0;
};

} // namespace win32
