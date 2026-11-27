#pragma once

#include "raw_buffer.hpp"
#include "text_writer.hpp"

#include <algorithm>
#include <cstddef>
#include <string>
#include <string_view>

namespace corehost::consoleinput
{

// Incremental UTF-16 line state shared by the raw and cooked input paths.
class line_editor final
{
  public:
    enum class change
    {
        none,
        append,
        redraw,
    };

    [[nodiscard]] std::u16string_view text() const noexcept
    {
        return text_;
    }

    [[nodiscard]] std::size_t cursor() const noexcept
    {
        return cursor_;
    }

    void reset() noexcept
    {
        text_.clear();
        cursor_ = 0;
    }

    void assign(std::u16string_view value, std::size_t cursor)
    {
        text_.assign(value);
        cursor_ = std::min(cursor, text_.size());
    }

    void truncate(std::size_t maximum)
    {
        if (text_.size() <= maximum)
            return;
        text_.resize(maximum);
        cursor_ = std::min(cursor_, text_.size());
    }

    change append(std::u16string_view value)
    {
        if (value.empty())
            return change::none;
        if (!insert_)
        {
            const bool at_end = cursor_ == text_.size();
            if (!at_end)
                erase_next();
            text_.insert(cursor_, value);
            cursor_ += value.size();
            return at_end ? change::append : change::redraw;
        }

        auto offset = std::size_t{0};
        while (offset < value.size())
        {
            const auto decoded = screen::next_codepoint(value, offset);
            if (cursor_ < text_.size())
                erase_next();
            text_.append(value.data() + offset, decoded.units);
            cursor_ += decoded.units;
            offset += decoded.units;
        }
        return change::redraw;
    }

    change insert(char16_t value)
    {
        if (cursor_ == text_.size() && !insert_)
        {
            text_.push_back(value);
            ++cursor_;
            return change::append;
        }
        if (insert_ && cursor_ < text_.size())
            erase_next();
        text_.insert(cursor_, 1, value);
        ++cursor_;
        return change::redraw;
    }

    change backspace() noexcept
    {
        if (cursor_ == 0)
            return change::none;
        auto begin = previous(cursor_);
        text_.erase(begin, cursor_ - begin);
        cursor_ = begin;
        return change::redraw;
    }

    change del() noexcept
    {
        if (cursor_ >= text_.size())
            return change::none;
        erase_next();
        return change::redraw;
    }

    change left() noexcept
    {
        const auto value = previous(cursor_);
        if (value == cursor_)
            return change::none;
        cursor_ = value;
        return change::redraw;
    }

    change right() noexcept
    {
        const auto value = next(cursor_);
        if (value == cursor_)
            return change::none;
        cursor_ = value;
        return change::redraw;
    }

    change home() noexcept
    {
        if (cursor_ == 0)
            return change::none;
        cursor_ = 0;
        return change::redraw;
    }

    change end() noexcept
    {
        if (cursor_ == text_.size())
            return change::none;
        cursor_ = text_.size();
        return change::redraw;
    }

    change toggle_insert() noexcept
    {
        insert_ = !insert_;
        return change::none;
    }

    void consume_completed_line() noexcept
    {
        reset();
    }

  private:
    [[nodiscard]] std::size_t previous(std::size_t at) const noexcept
    {
        if (at == 0)
            return 0;
        auto offset = std::size_t{0};
        auto last = std::size_t{0};
        while (offset < at)
        {
            last = offset;
            const auto decoded = screen::next_codepoint(text_, offset);
            offset += decoded.units;
        }
        return last;
    }

    [[nodiscard]] std::size_t next(std::size_t at) const noexcept
    {
        if (at >= text_.size())
            return text_.size();
        const auto decoded = screen::next_codepoint(text_, at);
        return at + decoded.units;
    }

    void erase_next() noexcept
    {
        const auto end = next(cursor_);
        if (end > cursor_)
            text_.erase(cursor_, end - cursor_);
    }

    vtparser::raw_u16string text_;
    std::size_t cursor_{};
    bool insert_{};
};

} // namespace corehost::consoleinput
