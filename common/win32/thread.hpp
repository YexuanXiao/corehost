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
// - A thread name is required. It is applied by the creator while the
//   thread is still suspended, then the thread is resumed. This makes
//   the name visible before the entry point runs and keeps naming a
//   creation-time invariant instead of a later mutation.
// - join() never closes the handle; the destructor does.
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
#include "win32/string.hpp"

#include <Windows.h>

#include <cassert>
#include <memory>
#include <utility>

namespace win32
{

// ── Thread RAII wrapper ────────────────────────────────────────
class basic_thread
{
  public:
    // ── Creation options ──────────────────────────────────────
    struct options
    {
        // Debugger/ETW-visible name. Must be null-terminated; required.
        win32::wcstring_view name{};
        DWORD creation_flags = 0;
        DWORD stack_size = 0;
        LPSECURITY_ATTRIBUTES attributes = nullptr;
    };

    // ── Construction ──────────────────────────────────────────
    //
    // Creates a suspended thread, names it, then resumes it. Throws
    // win32::error on failure.
    basic_thread(LPTHREAD_START_ROUTINE start_address, LPVOID parameter = nullptr, options opts = {})
    {
        assert(!opts.name.empty());

        // The start context is owned by the new thread once CreateThread
        // succeeds; on failure it is released here.
        auto context = std::make_unique<start_context>(start_address, parameter);

        _handle = win32::handle{::CreateThread(opts.attributes, opts.stack_size, &basic_thread::trampoline,
                                               context.get(), opts.creation_flags | CREATE_SUSPENDED, nullptr)};
        if (!_handle.valid())
            win32::throw_last_error();

        // Naming is part of the creation contract. The thread is still
        // suspended, so a failure can be cleaned up without running any
        // user code.
        if (::SetThreadDescription(_handle.get(), opts.name.data()) == FALSE)
        {
            const auto err = ::GetLastError();
            ::TerminateThread(_handle.get(), 1);
            win32::throw_last_error(static_cast<win32::error>(err));
        }

        if (::ResumeThread(_handle.get()) == static_cast<DWORD>(-1))
        {
            const auto err = ::GetLastError();
            ::TerminateThread(_handle.get(), 1);
            win32::throw_last_error(static_cast<win32::error>(err));
        }

        context.release();
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

    // ── Waiting ──────────────────────────────────────────────
    //
    // join() waits forever; it is the only production way to wait for
    // a worker. Timeouts are forbidden by project policy (see
    // doc/design/0001.md 4.7).
    void join()
    {
        assert(valid());
        if (::WaitForSingleObject(_handle.get(), INFINITE) != WAIT_OBJECT_0)
            win32::throw_last_error();
    }

  private:
    // Per-thread start data. Heap allocated so a basic_thread object
    // can be moved while the thread is starting.
    struct start_context
    {
        start_context(LPTHREAD_START_ROUTINE entry, LPVOID parameter) : entry_point{entry}, parameter{parameter}
        {
        }

        LPTHREAD_START_ROUTINE entry_point;
        LPVOID parameter;
    };

    static DWORD WINAPI trampoline(LPVOID raw_context)
    {
        // The context belongs to this thread; release it even if the
        // entry point exits through a fatal error path.
        std::unique_ptr<start_context> context{static_cast<start_context *>(raw_context)};
        return context->entry_point(context->parameter);
    }

    win32::handle _handle{};
};

} // namespace win32