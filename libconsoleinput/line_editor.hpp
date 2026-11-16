#pragma once

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
        complete,
    };

    [[nodiscard]] std::u16string_view text() const noexcept
    {
        return text_;
    }

    [[nodiscard]] std::size_t cursor() const noexcept
    {
        return cursor_;
    }

    [[nodiscard]] bool insert_mode() const noexcept
    {
        return insert_;
    }

    void reset() noexcept
    {
        text_.clear();
        cursor_ = 0;
        insert_ = false;
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

        for (std::size_t i = 0; i < value.size();)
        {
            std::size_t length = 1;
            if (is_high(value[i]) && i + 1 < value.size() && is_low(value[i + 1]))
                length = 2;
            if (cursor_ < text_.size())
                erase_next();
            text_.insert(cursor_, value.substr(i, length));
            cursor_ += length;
            i += length;
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
        const auto begin = previous(cursor_);
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

    change enter() noexcept
    {
        cursor_ = text_.size();
        return change::complete;
    }

    void consume_completed_line() noexcept
    {
        reset();
    }

  private:
    [[nodiscard]] static bool is_high(char16_t value) noexcept
    {
        return value >= 0xD800 && value <= 0xDBFF;
    }

    [[nodiscard]] static bool is_low(char16_t value) noexcept
    {
        return value >= 0xDC00 && value <= 0xDFFF;
    }

    [[nodiscard]] std::size_t previous(std::size_t at) const noexcept
    {
        if (at == 0)
            return 0;
        --at;
        if (at != 0 && is_low(text_[at]) && is_high(text_[at - 1]))
            --at;
        return at;
    }

    [[nodiscard]] std::size_t next(std::size_t at) const noexcept
    {
        if (at >= text_.size())
            return text_.size();
        ++at;
        if (at < text_.size() && is_low(text_[at]) && is_high(text_[at - 1]))
            ++at;
        return at;
    }

    void erase_next() noexcept
    {
        const auto end = next(cursor_);
        if (end > cursor_)
            text_.erase(cursor_, end - cursor_);
    }

    std::u16string text_;
    std::size_t cursor_{};
    bool insert_{};
};

} // namespace corehost::consoleinput
