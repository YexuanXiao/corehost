#pragma once
#include "pipe_bridge.hpp"

namespace conpty
{

class pipe_bridge_testable : public pipe_bridge
{
  public:
    using pipe_bridge::pipe_bridge;

    COORD test_get_term_cursor() const noexcept
    {
        return _terminal.cursor();
    }

    bool test_is_term_cursor_valid() const noexcept
    {
        return _terminal.cursor_valid();
    }

    void test_set_term_cursor_valid(COORD pos)
    {
        _terminal.set_cursor(pos);
    }

    SHORT test_get_input_column_start() const noexcept
    {
        return _terminal.input_column_start();
    }

    SHORT test_get_input_column_end() const noexcept
    {
        return _terminal.input_column_end();
    }

    void test_set_input_column_start(SHORT x)
    {
        _terminal.set_input_column_start(x);
    }

    void test_set_input_column_end(SHORT x)
    {
        _terminal.set_input_column_end(x);
    }

    const std::u32string &test_get_cooked_buf() const noexcept
    {
        return _cooked_buf;
    }

    size_t test_get_cooked_cursor() const noexcept
    {
        return _cooked_cursor;
    }

    void test_cooked_append(const char32_t *s, size_t n)
    {
        for (size_t i = 0; i < n; ++i)
            _edit_insert(s[i], static_cast<BYTE>(s[i]));
    }

    void test_cooked_backspace()
    {
        _edit_backspace();
    }

    void test_cooked_delete()
    {
        _edit_delete();
    }

    void test_cooked_left()
    {
        _edit_move_left();
    }

    void test_cooked_right()
    {
        _edit_move_right();
    }

    void test_cooked_home()
    {
        _edit_home();
    }

    void test_cooked_end()
    {
        _edit_end();
    }

    size_t test_history_size() const noexcept
    {
        return _history.size();
    }

    void test_history_push()
    {
        history_push();
        _cooked_buf.clear();
        _cooked_cursor = 0;
        _history.reset_browse();
    }

    void test_history_up()
    {
        _edit_history_up();
    }

    void test_history_down()
    {
        _edit_history_down();
    }

    void test_expand_alias()
    {
        _expand_alias();
    }

    std::string test_build_completion_utf8() const
    {
        std::string s;
        convert_u32_to_utf8(_cooked_buf, s);
        s += "\r\n";
        return s;
    }

    COORD test_feed_echo_bytes(const BYTE *bytes, DWORD len)
    {
        for (DWORD i = 0; i < len; ++i)
            _terminal.apply_echo_byte(bytes[i]);
        return _terminal.cursor();
    }

    void test_enter_console_read_mode(SHORT prompt_col = 13)
    {
        _pending.begin_console_read_mode(false, false);
        _read_total = 0;
        _cooked_buf.clear();
        _cooked_cursor = 0;
        _line_found = false;
        _terminal.set_cursor({prompt_col, 0});
        _terminal.reset_bounds(prompt_col);
    }

    void test_feed_raw_bytes(const BYTE *bytes, DWORD len)
    {
        process_input(reinterpret_cast<const char8_t *>(bytes), len);
        vt_flush();
    }

    void test_feed_raw_bytes(const char8_t *bytes, DWORD len)
    {
        process_input(bytes, len);
        vt_flush();
    }

    void test_prepare_raw_read_completion(miniio::io_msg &msg, const BYTE *bytes, DWORD len, bool eof = false)
    {
        _pending.begin_raw_read(msg, false);
        _pending.set_vt_eof(eof);
        _read_total = std::min<DWORD>(len, static_cast<DWORD>(_readbuf.size()));
        if (_read_total > 0)
            std::memcpy(_readbuf.data(), bytes, _read_total);
        prepare_raw_read_completion(msg);
    }

    void test_prepare_console_read_completion(miniio::io_msg &msg, std::u32string_view line, bool unicode)
    {
        _pending.begin_console_read(msg, unicode, false);
        _pending.set_vt_eof(false);
        _read_total = static_cast<DWORD>(line.size());
        _cooked_buf.assign(line.begin(), line.end());
        _cooked_cursor = _cooked_buf.size();
        prepare_console_read_completion(msg);
    }

    bool test_line_found() const noexcept
    {
        return _line_found;
    }

    int test_get_pend_kind() const noexcept
    {
        return static_cast<int>(_pending.kind());
    }

    size_t test_vt_buf_len() const noexcept
    {
        return _vt_output.buffered_size();
    }

    bool test_get_enter_newline_flag() const noexcept
    {
        return _terminal.enter_newline_pending();
    }

    void test_set_enter_newline_flag(bool v) noexcept
    {
        _terminal.set_enter_newline_pending(v);
    }
};

} // namespace conpty
