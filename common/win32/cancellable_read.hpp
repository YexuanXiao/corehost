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
// - The reader signals an "in read" auto-reset event immediately
//   before calling ReadFile and resets it afterwards.
// - A stopper waits on {reader thread, "in read" event} with no
//   timeout: whenever the event signals, the reader has just entered
//   ReadFile, so cancelling is meaningful; when the thread handle
//   signals, the reader has exited.
// - No timer, no retry interval and no wait limit is involved.
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
//   reader.wait_for_exit(thread);   // 无限等待，事件驱动
//
// Only one thread may call read() on a given instance.

#include "win32/error.hpp"
#include "win32/event.hpp"
#include "win32/handle.hpp"
#include "win32/thread.hpp"
#include "win32/wait.hpp"

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

    // Creating the "in read" event can fail; that is fatal, so the
    // constructor is intentionally not noexcept.
    explicit cancellable_read(win32::handle_view pipe)
        : _pipe{pipe}, _in_read{win32::create_tag, /*manual_reset=*/false, /*initial=*/false}
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

        _in_read.set();
        const BOOL ok = ::ReadFile(_pipe.get(), destination.data(), static_cast<DWORD>(destination.size()), &read_bytes,
                                   nullptr);
        _in_read.reset();

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
    // false when the reader is not inside ReadFile (it either has not
    // entered yet or already left); the caller then waits for the
    // "in read" event again instead of re-checking on a timer.
    [[nodiscard]] bool cancel_pending(win32::handle_view reader_thread) const
    {
        if (::CancelSynchronousIo(reader_thread.get()) != FALSE)
            return true;

        const auto err = win32::get_last_error();
        if (err == win32::error::not_found)
            return false;

        win32::throw_last_error(err);
    }

    // Waits until the reader thread exits, cancelling the read it is
    // currently blocked in. Pure event-driven: waits on {reader thread,
    // "in read" event} with no timeout, no retry interval and no wait
    // limit.
    //
    // Termination is guaranteed by construction: the "in read" event is
    // set immediately before ReadFile, so every read the reader enters
    // wakes this loop; every such read is cancelled; the reader checks
    // the stop flag after each read and exits. If cancellation itself
    // fails unexpectedly, that is fatal and throws.
    void wait_for_exit(basic_thread &reader_thread)
    {
        for (;;)
        {
            const auto wait = win32::wait_any(reader_thread.view(), _in_read.view(), INFINITE);
            if (wait.abandoned())
                win32::throw_last_error(win32::error::invalid_state);

            // index 0: the reader thread has exited.
            if (wait.index == 0)
                return;

            // index 1: the reader just entered ReadFile; cancel it. If it
            // completed normally in the meantime, the next wait simply
            // observes either the thread exit or the next read.
            (void)cancel_pending(reader_thread.view());
        }
    }

  private:
    // Non-owning pipe handle; the session owns it.
    win32::handle_view _pipe;

    // Auto-reset: signaled while this thread is inside ReadFile. The
    // stopper waits on it instead of polling; no memory ordering is
    // needed because the wait itself is the synchronization.
    win32::event _in_read;

    // Set by request_stop(); distinguishes "cancelled" from "aborted by
    // somebody else", which is fatal.
    std::atomic<bool> _stop{false};
};

} // namespace win32
