#pragma once

#include "screen_buffer.hpp"

#include <Windows.h>

#include <cstddef>
#include <iterator>
#include <mutex>
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

// Queue between the console IO thread and the UI thread. The IO thread
// publishes immutable damaged rows; the UI thread takes them before rendering.
class screen_update_mailbox final
{
  public:
    screen_update_mailbox() = default;

    screen_update_mailbox(const screen_update_mailbox &) = delete;
    screen_update_mailbox &operator=(const screen_update_mailbox &) = delete;

    void attach(HWND window) noexcept
    {
        std::scoped_lock lock{mutex_};
        window_ = window;
    }

    void detach() noexcept
    {
        std::scoped_lock lock{mutex_};
        window_ = nullptr;
    }

    [[nodiscard]] HWND window() noexcept
    {
        std::scoped_lock lock{mutex_};
        return window_;
    }

    void publish(std::vector<screen_row_update> updates) noexcept
    {
        if (updates.empty())
        {
            return;
        }

        HWND window = nullptr;
        {
            std::scoped_lock lock{mutex_};
            pending_.insert(pending_.end(), std::make_move_iterator(updates.begin()),
                            std::make_move_iterator(updates.end()));
            window = window_;
        }

        if (window != nullptr)
        {
            (void)::PostMessageW(window, screen_update_message, 0, 0);
        }
    }

    bool take(std::vector<screen_row_update> &updates)
    {
        std::scoped_lock lock{mutex_};
        if (pending_.empty())
        {
            return false;
        }
        updates = std::move(pending_);
        pending_.clear();
        return true;
    }

  private:
    std::mutex mutex_;
    HWND window_{};
    std::vector<screen_row_update> pending_;
};

} // namespace corehost::window
