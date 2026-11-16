#pragma once

#include "vt_parser.hpp"

#include "deque.hpp"

#include <algorithm>
#include <cstddef>
#include <Windows.h>

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <utility>

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

[[nodiscard]] inline key_event key_from_character(char16_t character, bool key_down = true) noexcept
{
    key_event event;
    event.character = character;
    event.key_down = key_down;
    if (character != 0)
    {
        const auto scan = ::VkKeyScanW(static_cast<wchar_t>(character));
        if (scan != -1)
        {
            event.key = static_cast<virtual_key>(LOBYTE(scan));
            const auto shift = HIBYTE(scan);
            event.control_state = (shift & 1) != 0 ? 0x0010 : 0;
            event.control_state |= (shift & 2) != 0 ? 0x0008 : 0;
            event.control_state |= (shift & 4) != 0 ? 0x0004 : 0;
            event.scan_code = static_cast<std::uint16_t>(::MapVirtualKeyW(LOBYTE(scan), MAPVK_VK_TO_VSC));
        }
    }
    return event;
}

enum class input_event_type : std::uint8_t
{
    key,
    mouse,
    window,
    text,
};

struct input_event final
{
    input_event_type type{input_event_type::key};
    key_event key;
    mouse_event mouse;
    window_event window;
    // A text event batches one parser text run. Consumers that need per-key
    // semantics expand it at their boundary; the raw reader consumes it as a
    // batch.
    std::u16string text;
    std::size_t text_offset{};
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
            ++key_count_;
        else if (event.type == input_event_type::text)
            key_count_ += event.text.size();
        events_.push_back(event);
    }

    void push(input_event &&event)
    {
        if (event.type == input_event_type::key && event.key.key_down)
            ++key_count_;
        else if (event.type == input_event_type::text)
            key_count_ += event.text.size();
        events_.push_back(std::move(event));
    }

    void push_front(const input_event &event)
    {
        if (event.type == input_event_type::key && event.key.key_down)
            ++key_count_;
        else if (event.type == input_event_type::text)
            key_count_ += event.text.size();
        events_.push_front(event);
    }

    void push_front(input_event &&event)
    {
        if (event.type == input_event_type::key && event.key.key_down)
            ++key_count_;
        else if (event.type == input_event_type::text)
            key_count_ += event.text.size();
        events_.push_front(std::move(event));
    }

    [[nodiscard]] std::size_t record_count() const noexcept
    {
        std::size_t count = 0;
        for (const auto &event : events_)
            count += event.type == input_event_type::text ? (event.text.size() - event.text_offset) * 2 : 1;
        return count;
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
        std::size_t count = 0;
        for (const auto &event : events_)
        {
            if (count == destination.size())
                break;
            if (event.type != input_event_type::text)
            {
                destination[count++] = event;
                continue;
            }
            for (std::size_t i = event.text_offset; i < event.text.size() && count < destination.size(); ++i)
            {
                input_event down{};
                down.type = input_event_type::key;
                down.key = key_from_character(event.text[i], true);
                destination[count++] = down;
                if (count == destination.size())
                    break;
                input_event up{};
                up.type = input_event_type::key;
                up.key = key_from_character(event.text[i], false);
                destination[count++] = up;
            }
        }
        return count;
    }

    // Raw consumers need the text batch intact. Console consumers use read(),
    // which expands it at the queue boundary.
    std::size_t read_raw(std::span<input_event> destination) noexcept
    {
        const auto count = std::min(destination.size(), events_.size());
        std::copy_n(events_.begin(), count, destination.begin());
        for (std::size_t i = 0; i < count; ++i)
        {
            auto &front = events_.front();
            if (front.type == input_event_type::key && front.key.key_down)
                --key_count_;
            else if (front.type == input_event_type::text)
                key_count_ -= front.text.size() - front.text_offset;
            events_.pop_front();
        }
        return count;
    }

    std::size_t read(std::span<input_event> destination) noexcept
    {
        std::size_t count = 0;
        if (text_up_pending_)
        {
            input_event up{};
            up.type = input_event_type::key;
            up.key = key_from_character(text_up_char_, false);
            destination[count++] = up;
            text_up_pending_ = false;
        }
        while (count < destination.size() && !events_.empty())
        {
            auto &front = events_.front();
            if (front.type != input_event_type::text)
            {
                destination[count++] = front;
                if (front.type == input_event_type::key && front.key.key_down)
                    --key_count_;
                events_.pop_front();
                continue;
            }
            if (front.text_offset >= front.text.size())
            {
                events_.pop_front();
                continue;
            }
            const auto character = front.text[front.text_offset++];
            --key_count_;
            input_event down{};
            down.type = input_event_type::key;
            down.key = key_from_character(character, true);
            destination[count++] = down;
            if (count < destination.size())
            {
                input_event up{};
                up.type = input_event_type::key;
                up.key = key_from_character(character, false);
                destination[count++] = up;
            }
            else
            {
                text_up_pending_ = true;
                text_up_char_ = character;
            }
            if (front.text_offset >= front.text.size())
                events_.pop_front();
        }
        return count;
    }

    void clear() noexcept
    {
        events_.clear();
        key_count_ = 0;
        text_up_pending_ = false;
    }

  private:
    bizwen::deque<input_event> events_;
    std::size_t key_count_{};
    bool text_up_pending_{};
    char16_t text_up_char_{};
};

} // namespace corehost::consoleinput
