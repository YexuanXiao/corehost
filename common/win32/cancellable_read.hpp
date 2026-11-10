#pragma once

// ── Cancellable blocking read ─────────────────────────────────
//
// Anonymous pipe handles (the ones corehost receives for terminal
// input and PtySignal traffic) are synchronous: a thread blocked in
// ReadFile cannot be woken by a waitable handle, because the pipe
// itself is not waitable for readability and cannot be opened with
// FILE_FLAG_OVERLAPPED by us.
//
// This wrapper makes such a read cancellable by another thread:
// - The reader marks "in read" before calling ReadFile.
// - A stopper sets the stop flag and then calls cancel_pending(),
//   which calls CancelSynchronousIo() while the reader is inside
//   ReadFile.
// - There is a window between "stop check" and "enter ReadFile"; the
//   stopper must therefore keep calling cancel_pending() until the
//   reader thread has exited. See wait_for_exit() for that loop.
// - A cancelled read returns status::cancelled, which is NOT EOF.
//   A closed write end returns status::closed (ReadFile succeeds with
//   zero bytes). Unexpected ReadFile failures throw, because a
//   blocking pipe read has no other legitimate failure mode.
//
// Usage (reader thread):
//   win32::cancellable_read reader{pipe};
//   DWORD n = 0;
//   switch (reader.read(std::span{buffer}, n)) { ... }
//
// Usage (stopper thread):
//   reader.request_stop();
//   reader.wait_for_exit(thread, timeout);
//
// Only one thread may call read() on a given instance.

#include "win32/error.hpp"
#include "win32/handle.hpp"
#include "win32/thread.hpp"

#include <Windows.h>

#include <atomic>
#include <cassert>
#include <cstddef>
#include <span>

namespace win32
{

class cancellable_read
{
  public:
    enum class status
    {
        // Some bytes (possibly a partial message) were read.
        data,
        // The peer closed the pipe (ReadFile succeeded with zero bytes).
        closed,
        // The read was cancelled by request_stop()/cancel_pending().
        cancelled,
    };

    explicit cancellable_read(win32::handle_view pipe) noexcept : _pipe{pipe}
    {
    }

    cancellable_read(const cancellable_read &) = delete;
    cancellable_read &operator=(const cancellable_read &) = delete;

    // ── Reader side ──────────────────────────────────────────
    //
    // Blocks until data, EOF or cancellation. read_bytes receives the
    // number of bytes read (0 for closed/cancelled).
    [[nodiscard]] status read(std::span<std::byte> destination, DWORD &read_bytes)
    {
        assert(!destination.empty());
        read_bytes = 0;

        _in_read.store(true, std::memory_order_release);
        const BOOL ok = ::ReadFile(_pipe.get(), destination.data(), static_cast<DWORD>(destination.size()), &read_bytes,
                                   nullptr);
        _in_read.store(false, std::memory_order_release);

        if (ok != FALSE)
            return read_bytes == 0 ? status::closed : status::data;

        const auto err = win32::get_last_error();
        if (err == win32::error::operation_aborted)
        {
            // A cancellation that the reader did not ask for would mean
            // somebody else cancelled this thread's I/O; treat it as a
            // fatal inconsistency instead of silently reporting EOF.
            if (_stop.load(std::memory_order_acquire))
                return status::cancelled;
            win32::throw_last_error(err);
        }

        // The peer closing between two reads can surface as a broken
        // pipe instead of a zero-byte read; both mean "no more data".
        if (err == win32::error::broken_pipe || err == win32::error::pipe_not_connected || err == win32::error::no_data)
            return status::closed;

        win32::throw_last_error(err);
    }

    // ── Stopper side ─────────────────────────────────────────
    void request_stop() noexcept
    {
        _stop.store(true, std::memory_order_release);
    }

    [[nodiscard]] bool stop_requested() const noexcept
    {
        return _stop.load(std::memory_order_acquire);
    }

    // Cancels the reader's blocking ReadFile if it is inside one.
    // Returns true when a cancellation request was issued. Returns
    // false when the reader is not currently inside ReadFile (or just
    // left it); the caller must retry, because the reader may enter
    // ReadFile right after this check.
    [[nodiscard]] bool cancel_pending(win32::handle_view reader_thread) const
    {
        // Read the flag first: CancelSynchronousIo on a thread that is
        // not inside a synchronous read fails with ERROR_NOT_FOUND.
        if (!_in_read.load(std::memory_order_acquire))
            return false;

        if (::CancelSynchronousIo(reader_thread.get()) != FALSE)
            return true;

        const auto err = win32::get_last_error();
        if (err == win32::error::not_found)
            return false;

        win32::throw_last_error(err);
    }

    // Waits until the reader thread exits, cancelling its pending read
    // whenever it is inside one. Returns false if the thread did not
    // exit within timeout_ms.
    //
    // The 5ms retry period is a shutdown-only cost; it bounds the
    // window in which the reader can slip into ReadFile between the
    // flag check and the cancellation.
    [[nodiscard]] bool wait_for_exit(basic_thread &reader_thread, DWORD timeout_ms)
    {
        const ULONGLONG deadline = ::GetTickCount64() + timeout_ms;
        for (;;)
        {
            const ULONGLONG now = ::GetTickCount64();
            if (now >= deadline)
                return false;

            const ULONGLONG remaining = deadline - now;
            const DWORD slice = remaining < 5 ? static_cast<DWORD>(remaining) : 5;

            if (reader_thread.join_for(slice))
                return true;

            (void)cancel_pending(reader_thread.view());
        }
    }

  private:
    // Non-owning pipe handle; the session owns it.
    win32::handle_view _pipe;

    // True while this thread is inside ReadFile. Set before the call so
    // the stopper can decide whether CancelSynchronousIo is meaningful.
    std::atomic<bool> _in_read{false};

    // Set by request_stop(); distinguishes "cancelled" from "aborted by
    // somebody else", which is fatal.
    std::atomic<bool> _stop{false};
};

} // namespace win32
