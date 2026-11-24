#pragma once

#include "screen_buffer.hpp"

#include <Windows.h>

#include <array>
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

// Single-producer / single-consumer lock-free queue between the console IO
// thread and the UI thread. The IO thread publishes immutable damaged rows;
// the UI thread takes them before rendering.
class screen_update_mailbox final
{
  public:
    static constexpr std::size_t capacity = 512;

    screen_update_mailbox() = default;

    screen_update_mailbox(const screen_update_mailbox &) = delete;
    screen_update_mailbox &operator=(const screen_update_mailbox &) = delete;

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

    void publish(std::vector<screen_row_update> updates) noexcept
    {
        auto write = write_.load(std::memory_order_relaxed);
        for (auto &row : updates)
        {
            const auto next = (write + 1) % capacity;
            if (next == read_.load(std::memory_order_acquire))
            {
                break;
            }
            ring_[write] = std::move(row);
            write = next;
            write_.store(write, std::memory_order_release);
        }

        const auto window = window_.load(std::memory_order_acquire);
        if (window != nullptr)
        {
            (void)::PostMessageW(window, screen_update_message, 0, 0);
        }
    }

    bool take(std::vector<screen_row_update> &updates)
    {
        auto read = read_.load(std::memory_order_relaxed);
        auto write = write_.load(std::memory_order_acquire);
        if (read == write)
        {
            return false;
        }

        while (read != write)
        {
            updates.push_back(std::move(ring_[read]));
            read = (read + 1) % capacity;
            read_.store(read, std::memory_order_release);
            write = write_.load(std::memory_order_acquire);
        }
        return true;
    }

  private:
    std::array<screen_row_update, capacity> ring_{};
    std::atomic<std::size_t> write_{};
    std::atomic<std::size_t> read_{};
    std::atomic<HWND> window_{};
};

} // namespace corehost::window
