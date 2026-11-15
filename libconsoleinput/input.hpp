#pragma once

#include "vt_parser.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

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
};

struct input_event final
{
    input_event_type type{input_event_type::key};
    key_event key;
};

// Converts parser keyboard messages to a portable key event. Non-keyboard
// messages return std::nullopt.
[[nodiscard]] std::optional<key_event>
key_from_message(vtparser::vt_message_id id, const vtparser::vt_message &message) noexcept;

// Batch-oriented input queue. read() copies a whole span and never pops one
// element at a time; consumed storage is compacted only when it dominates.
class input_queue final
{
  public:
    void push(const input_event &event)
    {
        events_.push_back(event);
    }

    void push_front(const input_event &event)
    {
        if (head_ != 0)
        {
            events_[--head_] = event;
            return;
        }
        events_.insert(events_.begin(), event);
    }

    [[nodiscard]] std::size_t size() const noexcept
    {
        return events_.size() - head_;
    }

    [[nodiscard]] bool empty() const noexcept
    {
        return size() == 0;
    }

    std::size_t read(std::span<input_event> destination) noexcept
    {
        const auto count = std::min(destination.size(), size());
        std::copy_n(events_.begin() + static_cast<std::ptrdiff_t>(head_), count, destination.begin());
        head_ += count;
        if (head_ != 0 && head_ * 2 >= events_.size())
        {
            events_.erase(events_.begin(), events_.begin() + static_cast<std::ptrdiff_t>(head_));
            head_ = 0;
        }
        return count;
    }

    void clear() noexcept
    {
        events_.clear();
        head_ = 0;
    }

  private:
    std::vector<input_event> events_;
    std::size_t head_{};
};

} // namespace corehost::consoleinput