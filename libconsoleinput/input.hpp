#pragma once

#include "vt_parser.hpp"

#include "deque.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace corehost::consoleinput
{

enum class virtual_key : std::uint16_t
{
    up = 0x26,
    down = 0x28,
    left = 0x25,
    right = 0x27,
    home = 0x24,
    end = 0x23,
    insert = 0x2D,
    del = 0x2E,
    page_up = 0x21,
    page_down = 0x22,
    f1 = 0x70,
    f2 = 0x71,
    f3 = 0x72,
    f4 = 0x73,
    f5 = 0x74,
    f6 = 0x75,
    f7 = 0x76,
    f8 = 0x77,
    f9 = 0x78,
    f10 = 0x79,
    f11 = 0x7A,
    f12 = 0x7B,
};

struct mouse_event final
{
    std::int32_t x{};
    std::int32_t y{};
    std::uint32_t button_state{};
    std::uint32_t control_state{};
    std::uint32_t event_flags{};
};

struct window_event final
{
    std::int16_t width{};
    std::int16_t height{};
};

struct key_event final
{
    char16_t character{};
    virtual_key key{};
    std::uint16_t scan_code{};
    std::uint32_t control_state{};
    std::uint16_t repeat_count{1};
    bool key_down{true};
};

enum class input_event_type : std::uint8_t
{
    key,
    mouse,
    window,
};

struct input_event final
{
    input_event_type type{input_event_type::key};
    key_event key;
    mouse_event mouse;
    window_event window;
};

// Converts parser keyboard messages to a portable key event. Non-keyboard
// messages return std::nullopt.
[[nodiscard]] std::optional<key_event> key_from_message(vtparser::vt_message_id id,
                                                        const vtparser::vt_message &message) noexcept;

[[nodiscard]] std::optional<mouse_event> mouse_from_message(vtparser::vt_message_id id,
                                                            const vtparser::vt_message &message,
                                                            std::uint32_t *current_buttons = nullptr) noexcept;

// Batch-oriented input queue. read() copies a whole span and pops from the
// front; an intrusive deque avoids vector front-erase and keeps push_front O(1).
class input_queue final
{
  public:
    void push(const input_event &event)
    {
        if (event.type == input_event_type::key && event.key.key_down)
        {
            ++key_count_;
        }
        events_.push_back(event);
    }

    void push_front(const input_event &event)
    {
        if (event.type == input_event_type::key && event.key.key_down)
        {
            ++key_count_;
        }
        events_.push_front(event);
    }

    [[nodiscard]] std::size_t size() const noexcept
    {
        return events_.size();
    }

    [[nodiscard]] bool empty() const noexcept
    {
        return events_.empty();
    }

    [[nodiscard]] bool has_key() const noexcept
    {
        return key_count_ != 0;
    }

    std::size_t peek(std::span<input_event> destination) const noexcept
    {
        const auto count = std::min(destination.size(), events_.size());
        std::copy_n(events_.begin(), count, destination.begin());
        return count;
    }

    std::size_t read(std::span<input_event> destination) noexcept
    {
        const auto count = std::min(destination.size(), events_.size());
        std::copy_n(events_.begin(), count, destination.begin());
        for (std::size_t i = 0; i < count; ++i)
        {
            if (events_.front().type == input_event_type::key && events_.front().key.key_down)
            {
                --key_count_;
            }
            events_.pop_front();
        }
        return count;
    }

    void clear() noexcept
    {
        events_.clear();
        key_count_ = 0;
    }

  private:
    bizwen::deque<input_event> events_;
    std::size_t key_count_{};
};

} // namespace corehost::consoleinput
