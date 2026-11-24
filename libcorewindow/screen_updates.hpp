#pragma once

#include "console_state.hpp"
#include "screen_buffer.hpp"

#include <Windows.h>

#include <atomic>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace corehost::window
{

struct screen_cell_update final
{
    screen::cell_style style{};
    screen::cell_width width{};
    screen::cell_flags flags{};
    screen::cell_hyperlink hyperlink{};
    std::size_t text_offset{};
    std::size_t text_length{};
};

struct screen_row_update final
{
    std::size_t y{};
    std::size_t begin{};
    std::size_t end{};
    std::vector<screen_cell_update> cells;
    std::u16string text;
};

inline constexpr UINT screen_update_message = WM_APP + 1;

// Shared console screen. The console IO thread is the only writer; the UI
// thread asks for damage before rendering. Access is serialized with an
// SRWLOCK, not std::mutex.
class console_screen final
{
  public:
    class write_lock final
    {
      public:
        explicit write_lock(console_screen &owner) noexcept : owner_{owner}
        {
            ::AcquireSRWLockExclusive(&owner_.lock_);
        }

        ~write_lock()
        {
            ::ReleaseSRWLockExclusive(&owner_.lock_);
        }

        write_lock(const write_lock &) = delete;
        write_lock &operator=(const write_lock &) = delete;

      private:
        console_screen &owner_;
    };

    console_screen(std::size_t width, std::size_t height) : screen_{width, height}, state_{width, height}
    {
    }

    console_screen(const console_screen &) = delete;
    console_screen &operator=(const console_screen &) = delete;

    [[nodiscard]] screen::screen_buffer &buffer() noexcept
    {
        return screen_;
    }

    [[nodiscard]] screen::console_state &state() noexcept
    {
        return state_;
    }

    void attach(HWND window) noexcept
    {
        window_.store(window, std::memory_order_release);
    }

    void detach() noexcept
    {
        window_.store(nullptr, std::memory_order_release);
    }

    [[nodiscard]] HWND window() const noexcept
    {
        return window_.load(std::memory_order_acquire);
    }

    bool copy_updates(std::vector<screen_row_update> &updates)
    {
        ::AcquireSRWLockExclusive(&lock_);
        updates.clear();
        if (!screen_.has_damage())
        {
            ::ReleaseSRWLockExclusive(&lock_);
            return false;
        }

        for (std::size_t y = 0; y < screen_.height(); ++y)
        {
            auto begin = screen_.damage_begin(y);
            auto end = screen_.damage_end(y);
            if (begin >= end)
            {
                continue;
            }

            while (begin > 0 && screen_.at(begin, y).is_continuation())
            {
                --begin;
            }
            if (end > 0 && end < screen_.width() && screen_.at(end - 1, y).is_wide())
            {
                ++end;
            }

            screen_row_update row{};
            row.y = y;
            row.begin = begin;
            row.end = end;
            row.cells.reserve(end - begin);

            for (std::size_t x = begin; x < end; ++x)
            {
                std::u16string glyph;
                screen_.glyph_at_into(x, y, glyph);
                const auto offset = row.text.size();
                row.text.append(glyph);
                const auto &value = screen_.at(x, y);
                row.cells.push_back(screen_cell_update{
                    .style = value.style(),
                    .width = value.width(),
                    .flags = value.flags(),
                    .hyperlink = value.hyperlink(),
                    .text_offset = offset,
                    .text_length = glyph.size(),
                });
            }

            updates.push_back(std::move(row));
        }

        screen_.clear_damage();
        ::ReleaseSRWLockExclusive(&lock_);
        return !updates.empty();
    }

    void notify_if_damaged() noexcept
    {
        ::AcquireSRWLockShared(&lock_);
        const auto damaged = screen_.has_damage();
        ::ReleaseSRWLockShared(&lock_);

        const auto window = window_.load(std::memory_order_acquire);
        if (damaged && window != nullptr)
        {
            (void)::PostMessageW(window, screen_update_message, 0, 0);
        }
    }

  private:
    SRWLOCK lock_{SRWLOCK_INIT};
    screen::screen_buffer screen_;
    screen::console_state state_;
    std::atomic<HWND> window_{};
};

} // namespace corehost::window
