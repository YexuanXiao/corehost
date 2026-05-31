#pragma once
#include <windows.h>
#include <cstddef>

namespace conpty
{

class terminal_cursor_state
{
  public:
    COORD cursor() const noexcept
    {
        return _cursor;
    }

    bool cursor_valid() const noexcept
    {
        return _cursor_valid;
    }

    void set_cursor(COORD cursor) noexcept
    {
        _cursor = cursor;
        _cursor_valid = true;
    }

    void advance() noexcept
    {
        if (!_cursor_valid)
            return;
        ++_cursor.X;
        if (_cursor.X > _input_column_end)
            _input_column_end = _cursor.X;
    }

    void retreat() noexcept
    {
        if (!_cursor_valid)
            return;
        if (_cursor.X > _input_column_start)
            --_cursor.X;
    }

    void crlf() noexcept
    {
        if (!_cursor_valid)
            return;
        _cursor.X = 0;
        ++_cursor.Y;
    }

    void carriage_return() noexcept
    {
        if (_cursor_valid)
            _cursor.X = 0;
    }

    void line_feed() noexcept
    {
        if (!_cursor_valid)
            return;
        _cursor.X = 0;
        ++_cursor.Y;
    }

    SHORT column_for_offset(size_t offset) const noexcept
    {
        return static_cast<SHORT>(_input_column_start + offset);
    }

    void set_cursor_x(SHORT x) noexcept
    {
        if (_cursor_valid)
            _cursor.X = x;
    }

    void set_cursor_x_unchecked(SHORT x) noexcept
    {
        _cursor.X = x;
    }

    void reset_bounds(SHORT x) noexcept
    {
        _input_column_start = x;
        _input_column_end = x;
    }

    void extend_bounds() noexcept
    {
        ++_input_column_end;
    }

    void retract_bounds() noexcept
    {
        if (_input_column_end > _input_column_start)
            --_input_column_end;
    }

    void set_bounds_end_for_length(size_t length) noexcept
    {
        _input_column_end = static_cast<SHORT>(_input_column_start + length);
    }

    SHORT input_column_start() const noexcept
    {
        return _input_column_start;
    }

    SHORT input_column_end() const noexcept
    {
        return _input_column_end;
    }

    void set_input_column_start(SHORT x) noexcept
    {
        _input_column_start = x;
    }

    void set_input_column_end(SHORT x) noexcept
    {
        _input_column_end = x;
    }

    bool consume_enter_newline() noexcept
    {
        if (!_enter_pending_newline)
            return false;
        _enter_pending_newline = false;
        return true;
    }

    bool enter_newline_pending() const noexcept
    {
        return _enter_pending_newline;
    }

    void set_enter_newline_pending(bool pending) noexcept
    {
        _enter_pending_newline = pending;
    }

    COORD enter_dest() const noexcept
    {
        return _enter_dest;
    }

    void reset_enter_newline() noexcept
    {
        _enter_pending_newline = false;
    }

    void mark_enter_newline_at_cursor() noexcept
    {
        _enter_dest = _cursor;
        _enter_pending_newline = true;
    }

    void set_pending_inherit_cursor() noexcept
    {
        _pending_inherit_cursor = true;
    }

    bool pending_inherit_cursor() const noexcept
    {
        return _pending_inherit_cursor;
    }

    void finish_inherit_cursor(COORD cursor) noexcept
    {
        set_cursor(cursor);
        reset_bounds(cursor.X);
        _pending_inherit_cursor = false;
    }

    void apply_echo_byte(BYTE byte) noexcept
    {
        if (!_cursor_valid)
            return;
        if (byte == '\r')
        {
            _cursor.X = 0;
        }
        else if (byte == '\n')
        {
            _cursor.X = 0;
            ++_cursor.Y;
        }
        else if (byte == 0x08 || byte == 0x7F)
        {
            if (_cursor.X > _input_column_start)
                --_cursor.X;
            if (_cursor.X < _input_column_end)
                _input_column_end = _cursor.X;
        }
        else if (byte >= 0x20)
        {
            ++_cursor.X;
            if (_cursor.X > _input_column_end)
                _input_column_end = _cursor.X;
        }
    }

  private:
    COORD _cursor{0, 0};
    bool _cursor_valid = false;
    bool _enter_pending_newline = false;
    COORD _enter_dest{0, 0};
    bool _pending_inherit_cursor = false;
    SHORT _input_column_start = 0;
    SHORT _input_column_end = 0;
};

} // namespace conpty
