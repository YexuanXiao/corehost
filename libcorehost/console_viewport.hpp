#pragma once
#include <windows.h>
#include <algorithm>
#include "default_console_size.hpp"

namespace conpty
{

class console_viewport
{
  public:
    console_viewport() noexcept
    {
        reset_to_buffer(default_console_size);
    }

    explicit console_viewport(COORD buffer_size) noexcept
    {
        reset_to_buffer(buffer_size);
    }

    SMALL_RECT rect() const noexcept
    {
        return _rect;
    }

    COORD origin() const noexcept
    {
        return {_rect.Left, _rect.Top};
    }

    COORD size() const noexcept
    {
        return {static_cast<SHORT>(_rect.Right - _rect.Left + 1),
                static_cast<SHORT>(_rect.Bottom - _rect.Top + 1)};
    }

    bool covers(COORD buffer_size) const noexcept
    {
        const auto width = valid_dimension(buffer_size.X);
        const auto height = valid_dimension(buffer_size.Y);
        return _rect.Left == 0 && _rect.Top == 0 && _rect.Right == width - 1 && _rect.Bottom == height - 1;
    }

    bool contains(COORD buffer_position) const noexcept
    {
        return buffer_position.X >= _rect.Left && buffer_position.X <= _rect.Right && buffer_position.Y >= _rect.Top &&
               buffer_position.Y <= _rect.Bottom;
    }

    COORD relative_position(COORD buffer_position) const noexcept
    {
        return {static_cast<SHORT>(buffer_position.X - _rect.Left), static_cast<SHORT>(buffer_position.Y - _rect.Top)};
    }

    COORD absolute_position(COORD terminal_position) const noexcept
    {
        return {static_cast<SHORT>(_rect.Left + terminal_position.X),
                static_cast<SHORT>(_rect.Top + terminal_position.Y)};
    }

    COORD clamped_relative_position(COORD buffer_position) const noexcept
    {
        auto terminal_position = relative_position(buffer_position);
        const auto viewport_size = size();
        terminal_position.X = std::clamp<SHORT>(terminal_position.X, 0, static_cast<SHORT>(viewport_size.X - 1));
        terminal_position.Y = std::clamp<SHORT>(terminal_position.Y, 0, static_cast<SHORT>(viewport_size.Y - 1));
        return terminal_position;
    }

    bool set_rect(SMALL_RECT rect, COORD buffer_size) noexcept
    {
        const auto old = _rect;
        const auto buffer_width = valid_dimension(buffer_size.X);
        const auto buffer_height = valid_dimension(buffer_size.Y);
        const auto width = clamped_dimension(static_cast<int>(rect.Right) - rect.Left + 1, buffer_width);
        const auto height = clamped_dimension(static_cast<int>(rect.Bottom) - rect.Top + 1, buffer_height);
        const auto left = std::clamp<SHORT>(rect.Left, 0, static_cast<SHORT>(buffer_width - width));
        const auto top = std::clamp<SHORT>(rect.Top, 0, static_cast<SHORT>(buffer_height - height));
        _rect = {left, top, static_cast<SHORT>(left + width - 1), static_cast<SHORT>(top + height - 1)};
        return old.Left != _rect.Left || old.Top != _rect.Top || old.Right != _rect.Right || old.Bottom != _rect.Bottom;
    }

    bool set_size(COORD viewport_size, COORD buffer_size) noexcept
    {
        SMALL_RECT next = _rect;
        next.Right = static_cast<SHORT>(next.Left + std::max<SHORT>(viewport_size.X, 1) - 1);
        next.Bottom = static_cast<SHORT>(next.Top + std::max<SHORT>(viewport_size.Y, 1) - 1);
        return set_rect(next, buffer_size);
    }

    bool set_origin(COORD origin, COORD buffer_size) noexcept
    {
        const auto viewport_size = size();
        return set_rect({origin.X, origin.Y, static_cast<SHORT>(origin.X + viewport_size.X - 1),
                         static_cast<SHORT>(origin.Y + viewport_size.Y - 1)},
                        buffer_size);
    }

    bool move_origin(COORD delta, COORD buffer_size) noexcept
    {
        return set_origin({static_cast<SHORT>(_rect.Left + delta.X), static_cast<SHORT>(_rect.Top + delta.Y)},
                          buffer_size);
    }

    bool clamp_to_buffer(COORD buffer_size) noexcept
    {
        return set_rect(_rect, buffer_size);
    }

    bool snap_to_cursor(COORD cursor_position, COORD buffer_size) noexcept
    {
        auto next_origin = origin();
        if (cursor_position.X < _rect.Left)
            next_origin.X = cursor_position.X;
        else if (cursor_position.X > _rect.Right)
            next_origin.X = static_cast<SHORT>(next_origin.X + cursor_position.X - _rect.Right);

        if (cursor_position.Y < _rect.Top)
            next_origin.Y = cursor_position.Y;
        else if (cursor_position.Y > _rect.Bottom)
            next_origin.Y = static_cast<SHORT>(next_origin.Y + cursor_position.Y - _rect.Bottom);

        return set_origin(next_origin, buffer_size);
    }

    void reset_to_buffer(COORD buffer_size) noexcept
    {
        const auto width = valid_dimension(buffer_size.X);
        const auto height = valid_dimension(buffer_size.Y);
        _rect = {0, 0, static_cast<SHORT>(width - 1), static_cast<SHORT>(height - 1)};
    }

  private:
    SMALL_RECT _rect{};

    static SHORT valid_dimension(SHORT value) noexcept
    {
        return std::max<SHORT>(value, 1);
    }

    static SHORT clamped_dimension(int value, SHORT maximum) noexcept
    {
        return static_cast<SHORT>(std::clamp(value, 1, static_cast<int>(maximum)));
    }
};

} // namespace conpty
