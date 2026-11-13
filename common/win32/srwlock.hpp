#pragma once

// ── Win32 SRWLock / condition variable wrappers ────────────────
//
// Provides standard-library-shaped wrappers around SRWLOCK and
// CONDITION_VARIABLE. The project does not use CRITICAL_SECTION,
// std::mutex or std::shared_mutex directly: SRWLock is smaller,
// allocation-free and supports shared (reader) acquisition.
//
// Design:
// - noexcept acquire/release: the SRWLock API cannot fail.
// - Behaves like std::shared_mutex: lock/unlock for exclusive,
//   lock_shared/unlock_shared for shared, so it can be used with
//   std::unique_lock and std::shared_lock.
// - NOT recursive. Acquiring the same lock twice on one thread
//   deadlocks (same as SRWLOCK); do not "fix" that here.
// - Move/copy disabled: the lock address is observed by waiters.
//
// condition_variable:
// - Waits require an exclusive lock held through std::unique_lock.
// - Predicate overloads absorb spurious wakeups.
// - Timed waits return std::cv_status or the predicate result, and
//   throw win32::error only for unexpected failures, never for
//   ERROR_TIMEOUT.
//
// Usage:
//   win32::srwlock m;
//   {
//       std::shared_lock lock{m};      // shared (reader)
//   }
//   {
//       std::unique_lock lock{m};      // exclusive (writer)
//       m.unlock(); ... m.lock();      // manual control if needed
//   }

#include "win32/error.hpp"

#include <Windows.h>

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <limits>
#include <mutex>

namespace win32
{

// ── Shared/exclusive lock ──────────────────────────────────────
class srwlock
{
  public:
    // SRWLock can be initialized as a null pointer, which is a
    // compatibility guarantee at the ABI level.
    constexpr srwlock() noexcept = default;

    ~srwlock() noexcept = default;

    srwlock(const srwlock &) = delete;
    srwlock &operator=(const srwlock &) = delete;

    // ── Exclusive ────────────────────────────────────────────
    void lock() noexcept
    {
        ::AcquireSRWLockExclusive(&_lock);
    }

    [[nodiscard]] bool try_lock() noexcept
    {
        return ::TryAcquireSRWLockExclusive(&_lock) != FALSE;
    }

    void unlock() noexcept
    {
        ::ReleaseSRWLockExclusive(&_lock);
    }

    // ── Shared ───────────────────────────────────────────────
    void lock_shared() noexcept
    {
        ::AcquireSRWLockShared(&_lock);
    }

    [[nodiscard]] bool try_lock_shared() noexcept
    {
        return ::TryAcquireSRWLockShared(&_lock) != FALSE;
    }

    void unlock_shared() noexcept
    {
        ::ReleaseSRWLockShared(&_lock);
    }

    // ── Native access ────────────────────────────────────────
    // Used by condition_variable::wait; callers must not lock it
    // through the native handle behind the wrapper's back.
    [[nodiscard]] SRWLOCK *native_handle() noexcept
    {
        return &_lock;
    }

  private:
    SRWLOCK _lock{};
};

// ── Condition variable ────────────────────────────────────────
class condition_variable
{
  public:
    condition_variable() noexcept
    {
        ::InitializeConditionVariable(&_cv);
    }

    ~condition_variable() noexcept = default;

    // CONDITION_VARIABLE must not be moved once waiters exist.
    condition_variable(const condition_variable &) = delete;
    condition_variable &operator=(const condition_variable &) = delete;

    void notify_one() noexcept
    {
        ::WakeConditionVariable(&_cv);
    }

    void notify_all() noexcept
    {
        ::WakeAllConditionVariable(&_cv);
    }

    // ── Untimed wait ─────────────────────────────────────────
    // Re-acquires the exclusive lock before returning.
    void wait(std::unique_lock<srwlock> &lock)
    {
        assert(lock.owns_lock());
        if (::SleepConditionVariableSRW(&_cv, lock.mutex()->native_handle(), INFINITE, 0) == FALSE)
            win32::throw_last_error();
    }

    // Predicate form: returns only when pred() is true.
    template <typename Predicate>
    void wait(std::unique_lock<srwlock> &lock, Predicate pred)
    {
        while (!pred())
            wait(lock);
    }

    // ── Timed wait ───────────────────────────────────────────
    // ERROR_TIMEOUT is a normal result, not an error.
    template <typename Rep, typename Period>
    [[nodiscard]] std::cv_status wait_for(std::unique_lock<srwlock> &lock,
                                          const std::chrono::duration<Rep, Period> &relative_time)
    {
        return wait_for_impl(lock, to_milliseconds(relative_time));
    }

    template <typename Rep, typename Period, typename Predicate>
    [[nodiscard]] bool wait_for(std::unique_lock<srwlock> &lock,
                                const std::chrono::duration<Rep, Period> &relative_time, Predicate pred)
    {
        const auto deadline = std::chrono::steady_clock::now() + relative_time;
        while (!pred())
        {
            if (std::chrono::steady_clock::now() >= deadline)
                return pred();
            (void)wait_for_impl(lock, to_milliseconds(deadline - std::chrono::steady_clock::now()));
        }
        return true;
    }

  private:
    // Convert a duration to Win32 milliseconds. Non-positive durations
    // mean "poll once"; the upper bound keeps values below INFINITE.
    template <typename Rep, typename Period>
    [[nodiscard]] static DWORD to_milliseconds(const std::chrono::duration<Rep, Period> &duration) noexcept
    {
        // Parenthesized max(): <Windows.h> defines a max() macro unless
        // NOMINMAX is set by the translation unit.
        constexpr auto max_ms = static_cast<std::int64_t>((std::numeric_limits<DWORD>::max)() - 1);
        const auto ms = std::chrono::ceil<std::chrono::milliseconds>(duration).count();
        if (ms <= 0)
            return 0;
        if (ms >= max_ms)
            return static_cast<DWORD>(max_ms);
        return static_cast<DWORD>(ms);
    }

    [[nodiscard]] std::cv_status wait_for_impl(std::unique_lock<srwlock> &lock, DWORD timeout_ms)
    {
        assert(lock.owns_lock());
        if (::SleepConditionVariableSRW(&_cv, lock.mutex()->native_handle(), timeout_ms, 0) != FALSE)
            return std::cv_status::no_timeout;

        const auto err = win32::get_last_error();
        if (err == win32::error::timeout)
            return std::cv_status::timeout;

        win32::throw_last_error(err);
    }

    CONDITION_VARIABLE _cv{};
};

} // namespace win32
