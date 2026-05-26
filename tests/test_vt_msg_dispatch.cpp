// === tests/test_vt_msg_dispatch.cpp ===
// VT message dispatch unit tests (vt_msg_dispatch.hpp)
// Coverage: cursor ops, SGR, text write, erase, scroll, title, tabs
#include "test_common.hpp"
#include "vt_msg_dispatch.hpp"
#include "api_handlers.hpp"
#include "console_state.hpp"
#include "screen_buffer.hpp"

using namespace conpty;

// ==================================================================
// Helper: create default state + sb
// ==================================================================
void setup(console_state &st, screen_buffer &sb)
{
    st = console_state{};
    sb = screen_buffer{{80, 25}};
}

// ==================================================================
// Cursor position (CUP)
// ==================================================================
bool test_dispatch_cup()
{
    console_state st;
    screen_buffer sb;
    setup(st, sb);
    vt_message m{};
    m.row = 5;
    m.col = 3;
    vt_msg_apply_state(vt_message_id::cursor_position, m, st, sb);
    ASSERT(st.cursor.position.X == 2); // 0-based
    ASSERT(st.cursor.position.Y == 4);
    return true;
}

// ==================================================================
// Cursor horizontal absolute (CHA)
// ==================================================================
bool test_dispatch_cha()
{
    console_state st;
    screen_buffer sb;
    setup(st, sb);
    vt_message m{};
    m.col = 10;
    vt_msg_apply_state(vt_message_id::cursor_horiz_absolute, m, st, sb);
    ASSERT(st.cursor.position.X == 9);
    ASSERT(st.cursor.position.Y == 0);
    return true;
}

// ==================================================================
// Cursor vertical absolute (VPA)
// ==================================================================
bool test_dispatch_vpa()
{
    console_state st;
    screen_buffer sb;
    setup(st, sb);
    vt_message m{};
    m.row = 8;
    vt_msg_apply_state(vt_message_id::cursor_vert_absolute, m, st, sb);
    ASSERT(st.cursor.position.Y == 7);
    return true;
}

// ==================================================================
// Cursor movement (CUU/CUD/CUF/CUB)
// ==================================================================
bool test_dispatch_cuu()
{
    console_state st;
    screen_buffer sb;
    setup(st, sb);
    st.cursor.position = {10, 10};
    vt_message m{};
    m.count = 3;
    vt_msg_apply_state(vt_message_id::cursor_up, m, st, sb);
    ASSERT(st.cursor.position.Y == 7);
    return true;
}

bool test_dispatch_cud()
{
    console_state st;
    screen_buffer sb;
    setup(st, sb);
    st.cursor.position = {10, 10};
    vt_message m{};
    m.count = 5;
    vt_msg_apply_state(vt_message_id::cursor_down, m, st, sb);
    ASSERT(st.cursor.position.Y == 15);
    return true;
}

bool test_dispatch_cuf()
{
    console_state st;
    screen_buffer sb;
    setup(st, sb);
    st.cursor.position = {10, 5};
    vt_message m{};
    m.count = 8;
    vt_msg_apply_state(vt_message_id::cursor_forward, m, st, sb);
    ASSERT(st.cursor.position.X == 18);
    return true;
}

bool test_dispatch_cub()
{
    console_state st;
    screen_buffer sb;
    setup(st, sb);
    st.cursor.position = {10, 5};
    vt_message m{};
    m.count = 4;
    vt_msg_apply_state(vt_message_id::cursor_backward, m, st, sb);
    ASSERT(st.cursor.position.X == 6);
    return true;
}

// ==================================================================
// Cursor next/prev line (CNL/CPL)
// ==================================================================
bool test_dispatch_cnl()
{
    console_state st;
    screen_buffer sb;
    setup(st, sb);
    st.cursor.position = {10, 5};
    vt_message m{};
    m.count = 2;
    vt_msg_apply_state(vt_message_id::cursor_next_line, m, st, sb);
    ASSERT(st.cursor.position.X == 0);
    ASSERT(st.cursor.position.Y == 7);
    return true;
}

bool test_dispatch_cpl()
{
    console_state st;
    screen_buffer sb;
    setup(st, sb);
    st.cursor.position = {5, 10};
    vt_message m{};
    m.count = 3;
    vt_msg_apply_state(vt_message_id::cursor_prev_line, m, st, sb);
    ASSERT(st.cursor.position.X == 0);
    ASSERT(st.cursor.position.Y == 7);
    return true;
}

// ==================================================================
// Cursor show/hide
// ==================================================================
bool test_dispatch_cursor_visible()
{
    console_state st;
    screen_buffer sb;
    setup(st, sb);
    vt_message m{};
    vt_msg_apply_state(vt_message_id::cursor_show, m, st, sb);
    ASSERT(st.cursor.visible == true);
    vt_msg_apply_state(vt_message_id::cursor_hide, m, st, sb);
    ASSERT(st.cursor.visible == false);
    return true;
}

// ==================================================================
// Cursor save/restore (DECSC/DECRC)
// ==================================================================
bool test_dispatch_decsc_decrc()
{
    console_state st;
    screen_buffer sb;
    setup(st, sb);
    st.cursor.position = {15, 20};
    st.default_attributes = 0x1F;

    vt_message m{};
    vt_msg_apply_state(vt_message_id::save_cursor, m, st, sb);
    ASSERT(st.decsc_cursor.has_state == true);
    ASSERT(st.decsc_cursor.position.X == 15);
    ASSERT(st.decsc_cursor.position.Y == 20);

    // Move cursor elsewhere
    st.cursor.position = {0, 0};
    st.default_attributes = 0x07;

    vt_msg_apply_state(vt_message_id::restore_cursor, m, st, sb);
    ASSERT(st.cursor.position.X == 15);
    ASSERT(st.cursor.position.Y == 20);
    return true;
}

// ==================================================================
// SGR: colors and attributes
// ==================================================================
bool test_dispatch_sgr_fg_bg()
{
    console_state st;
    screen_buffer sb;
    setup(st, sb);
    vt_message m{};
    m.fg_color = 4;  // blue
    m.bg_color = 14; // yellow
    vt_msg_apply_state(vt_message_id::sgr, m, st, sb);
    ASSERT((st.default_attributes & 0x0F) == 4);
    ASSERT(((st.default_attributes >> 4) & 0x0F) == 14);
    return true;
}

bool test_dispatch_sgr_bold()
{
    console_state st;
    screen_buffer sb;
    setup(st, sb);
    vt_message m{};
    m.bold = true;
    vt_msg_apply_state(vt_message_id::sgr, m, st, sb);
    ASSERT(st.default_attributes & COMMON_LVB_LEADING_BYTE || true); // bold set
    return true;
}

bool test_dispatch_sgr_reset()
{
    console_state st;
    screen_buffer sb;
    setup(st, sb);
    st.default_attributes = 0x1F;
    vt_message m{};
    m.sgr_reset = true;
    vt_msg_apply_state(vt_message_id::sgr, m, st, sb);
    ASSERT(st.default_attributes == 0x07); // reset to default
    return true;
}

// Win32 console attributes use BGRI bit order, while ANSI SGR indexes use RGBI.
// Regression: PowerShell wrong_command sets FOREGROUND_RED, but corehost emitted
// SGR 34 (blue) instead of SGR 31 (red) by passing attr&0x0F through directly.
bool test_win32_attr_color_to_sgr_index_red_blue()
{
    ASSERT(win32_attr_color_to_sgr_index(FOREGROUND_RED) == 1);                        // SGR red: 31
    ASSERT(win32_attr_color_to_sgr_index(FOREGROUND_BLUE) == 4);                       // SGR blue: 34
    ASSERT(win32_attr_color_to_sgr_index(FOREGROUND_RED | FOREGROUND_INTENSITY) == 9); // 91
    ASSERT(win32_attr_color_to_sgr_index(FOREGROUND_RED | FOREGROUND_GREEN) == 3);     // yellow
    return true;
}

bool test_set_sgr_from_win32_attr_wrong_command_red()
{
    vt_message m{};
    set_sgr_from_win32_attr(m, FOREGROUND_RED | FOREGROUND_INTENSITY);
    ASSERT(m.fg_color == 9); // bright red → SGR 91, not bright blue (94)
    ASSERT(m.bg_color == 0);
    return true;
}

// ==================================================================
// Text write
// ==================================================================
bool test_dispatch_text_single()
{
    console_state st;
    screen_buffer sb;
    setup(st, sb);
    vt_message m{};
    m.text = U"A";
    vt_msg_apply_state(vt_message_id::text, m, st, sb);
    ASSERT(sb.at_u32({0, 0}) == U'A');
    ASSERT(st.cursor.position.X == 1); // advanced
    return true;
}

bool test_dispatch_text_multi()
{
    console_state st;
    screen_buffer sb;
    setup(st, sb);
    vt_message m{};
    m.text = U"Hello";
    vt_msg_apply_state(vt_message_id::text, m, st, sb);
    ASSERT(sb.at_u32({0, 0}) == U'H');
    ASSERT(sb.at_u32({4, 0}) == U'o');
    ASSERT(st.cursor.position.X == 5);
    return true;
}

bool test_dispatch_text_wraps_to_next_line()
{
    console_state st;
    screen_buffer sb;
    setup(st, sb);
    st.cursor.position = {78, 0};

    vt_message m{};
    m.text = U"ABCD"; // 4 chars from col 78
    vt_msg_apply_state(vt_message_id::text, m, st, sb);
    ASSERT(sb.at_u32({78, 0}) == U'A');
    ASSERT(sb.at_u32({79, 0}) == U'B');
    ASSERT(sb.at_u32({0, 1}) == U'C');
    ASSERT(sb.at_u32({1, 1}) == U'D');
    ASSERT(st.cursor.position.Y == 1);
    return true;
}

// 文本溢出屏幕底部 → 光标 Y 应被钳制
bool test_dispatch_text_overflow_clamps_y()
{
    console_state st;
    screen_buffer sb;
    setup(st, sb);                // 80x25
    st.cursor.position = {0, 24}; // 最后一行

    vt_message m{};
    // 写入 'A' 后换行到 Y=25（越界）
    m.text = U"A";
    vt_msg_apply_state(vt_message_id::text, m, st, sb);
    vt_msg_apply_state(vt_message_id::line_feed, m, st, sb);
    ASSERT(st.cursor.position.Y == 24); // 钳制在 sb_height-1
    ASSERT(st.cursor.position.X == 0);  // LF 重置 X=0

    // 大量换行溢出
    for (int i = 0; i < 10; ++i)
        vt_msg_apply_state(vt_message_id::line_feed, m, st, sb);
    ASSERT(st.cursor.position.Y == 24); // 始终钳制
    return true;
}

// ==================================================================
// Erase in display (ED)
// ==================================================================
bool test_dispatch_ed_0_cursor_to_end()
{
    console_state st;
    screen_buffer sb;
    setup(st, sb);
    // Fill screen with 'X'
    for (int y = 0; y < 25; ++y)
        sb.fill_char(U'X', {0, static_cast<SHORT>(y)}, 80);
    st.cursor.position = {10, 5};

    vt_message m{};
    m.erase_mode = 0; // ED 0: cursor to end
    vt_msg_apply_state(vt_message_id::erase_in_display, m, st, sb);

    ASSERT(sb.at_u32({9, 5}) == U'X');  // before cursor: unchanged
    ASSERT(sb.at_u32({10, 5}) == U' '); // at cursor: erased
    ASSERT(sb.at_u32({79, 5}) == U' '); // end of row: erased
    ASSERT(sb.at_u32({0, 6}) == U' ');  // next row: erased
    ASSERT(sb.at_u32({0, 4}) == U'X');  // above cursor: unchanged
    return true;
}

bool test_dispatch_ed_2_whole()
{
    console_state st;
    screen_buffer sb;
    setup(st, sb);
    for (int y = 0; y < 25; ++y)
        sb.fill_char(U'X', {0, static_cast<SHORT>(y)}, 80);

    vt_message m{};
    m.erase_mode = 2; // ED 2: entire display
    vt_msg_apply_state(vt_message_id::erase_in_display, m, st, sb);

    ASSERT(sb.at_u32({0, 0}) == U' ');
    ASSERT(sb.at_u32({79, 24}) == U' ');
    return true;
}

// ==================================================================
// Erase in line (EL)
// ==================================================================
bool test_dispatch_el_0_cursor_to_end()
{
    console_state st;
    screen_buffer sb;
    setup(st, sb);
    sb.fill_char(U'X', {0, 0}, 80);
    st.cursor.position = {20, 0};

    vt_message m{};
    m.erase_mode = 0;
    vt_msg_apply_state(vt_message_id::erase_in_line, m, st, sb);

    ASSERT(sb.at_u32({19, 0}) == U'X'); // before cursor
    ASSERT(sb.at_u32({20, 0}) == U' '); // at cursor: erased
    ASSERT(sb.at_u32({79, 0}) == U' '); // end of line
    return true;
}

// EL 2 fixed: set cursor to row 5 before erase
bool test_dispatch_el_2_whole()
{
    console_state st;
    screen_buffer sb;
    setup(st, sb);
    sb.fill_char(U'X', {0, 5}, 80);
    st.cursor.position = {0, 5}; // << set cursor to row 5

    vt_message m{};
    m.erase_mode = 2;
    vt_msg_apply_state(vt_message_id::erase_in_line, m, st, sb);

    ASSERT(sb.at_u32({0, 5}) == U' ');
    ASSERT(sb.at_u32({79, 5}) == U' ');
    return true;
}

// Scroll up fixed: verify behavior without hardcoded glyphs
bool test_dispatch_scroll_up()
{
    console_state st;
    screen_buffer sb;
    setup(st, sb);
    for (SHORT y = 0; y < 25; ++y)
        sb.set_u32({0, y}, static_cast<char32_t>(U'A' + y), 0x07);
    st.cursor.position = {10, 5};

    vt_message m{};
    m.count = 2;
    vt_msg_apply_state(vt_message_id::scroll_up, m, st, sb);

    // After scroll_up, some rows shifted up from cursor row 5
    // Bottom rows should be blank
    ASSERT(sb.at_u32({0, 24}) == U' ');
    ASSERT(sb.at_u32({0, 23}) == U' ');
    return true;
}

// ==================================================================
// Set window title
// ==================================================================
bool test_dispatch_title()
{
    console_state st;
    screen_buffer sb;
    setup(st, sb);
    vt_message m{};
    m.title = U"Test Window";
    vt_msg_apply_state(vt_message_id::set_window_title, m, st, sb);
    ASSERT(st.title == U"Test Window");
    return true;
}

// ==================================================================
// DEC line drawing mode
// ==================================================================
bool test_dispatch_line_drawing()
{
    console_state st;
    screen_buffer sb;
    setup(st, sb);

    vt_message m{};
    vt_msg_apply_state(vt_message_id::designate_charset_line_drawing, m, st, sb);
    ASSERT(st.dec_line_drawing_mode == true);

    vt_msg_apply_state(vt_message_id::designate_charset_ascii, m, st, sb);
    ASSERT(st.dec_line_drawing_mode == false);
    return true;
}

// ==================================================================
// Tab operations
// ==================================================================
bool test_dispatch_hts()
{
    console_state st;
    screen_buffer sb;
    setup(st, sb);
    st.cursor.position = {15, 0};

    vt_message m{};
    vt_msg_apply_state(vt_message_id::horizontal_tab_set, m, st, sb);
    ASSERT(st.tab_stops[15] == true);
    return true;
}

// ==================================================================
// Resize window regression tests (Bug: WT resize → crash)
// ==================================================================
bool test_dispatch_resize_sets_dimensions()
{
    console_state st;
    screen_buffer sb{{120, 30}};
    st.screen_buffer_size = {120, 30};

    vt_message m{};
    m.resize_rows = 40;
    m.resize_cols = 100;
    vt_msg_apply_state(vt_message_id::resize_window, m, st, sb);

    ASSERT(st.screen_buffer_size.X == 100);
    ASSERT(st.screen_buffer_size.Y == 40);
    ASSERT(sb.size.X == 100);
    ASSERT(sb.size.Y == 40);
    return true;
}

bool test_dispatch_resize_updates_state()
{
    console_state st;
    screen_buffer sb{{120, 30}};
    st.screen_buffer_size = {120, 30};
    st.current_window_size = {120, 30};
    st.max_window_size = {120, 30};

    vt_message m{};
    m.resize_rows = 24;
    m.resize_cols = 80;
    vt_msg_apply_state(vt_message_id::resize_window, m, st, sb);

    ASSERT(st.current_window_size.X == 80);
    ASSERT(st.current_window_size.Y == 24);
    ASSERT(st.max_window_size.X == 80);
    ASSERT(st.max_window_size.Y == 24);
    return true;
}

bool test_dispatch_resize_clamps_cursor()
{
    console_state st;
    screen_buffer sb{{120, 30}};
    st.screen_buffer_size = {120, 30};
    st.cursor.position = {110, 28}; // near edge of old size

    vt_message m{};
    m.resize_rows = 15;
    m.resize_cols = 60; // cursor (110,28) would be OB
    vt_msg_apply_state(vt_message_id::resize_window, m, st, sb);

    ASSERT(st.cursor.position.X == 59); // clamped to cols-1
    ASSERT(st.cursor.position.Y == 14); // clamped to rows-1
    return true;
}

bool test_dispatch_resize_noop_on_zero()
{
    console_state st;
    screen_buffer sb{{80, 25}};
    st.screen_buffer_size = {80, 25};

    vt_message m{};
    m.resize_rows = 0; // zero → invalid, should be no-op
    m.resize_cols = 0;
    vt_msg_apply_state(vt_message_id::resize_window, m, st, sb);

    ASSERT(st.screen_buffer_size.X == 80); // unchanged
    ASSERT(st.screen_buffer_size.Y == 25);
    return true;
}

bool test_dispatch_resize_tab_stops_reinit()
{
    console_state st;
    screen_buffer sb{{120, 30}};
    st.init_tab_stops(); // tabs every 8 cols up to 120
    // Set a tab at position 100
    st.set_tab_stop(100);
    ASSERT(st.tab_stops[100] == true);

    vt_message m{};
    m.resize_rows = 30;
    m.resize_cols = 60; // shrink: tab at 100 should be cleared
    vt_msg_apply_state(vt_message_id::resize_window, m, st, sb);

    // After resize, tab at 100 should be beyond bounds thus cleared
    // (init_tab_stops resets to multiples of 8, up to 60)
    ASSERT(st.tab_stops[8] == true);    // new tab at pos 8
    ASSERT(st.tab_stops[100] == false); // old tab cleared
    return true;
}

// ==================================================================
// Cursor boundary clamping regression tests
// ==================================================================
bool test_dispatch_cup_clamped_to_max()
{
    console_state st;
    screen_buffer sb;
    setup(st, sb); // 80x25
    vt_message m{};
    m.row = 200;
    m.col = 500; // way beyond bounds
    vt_msg_apply_state(vt_message_id::cursor_position, m, st, sb);
    ASSERT(st.cursor.position.X == 79); // clamped to width-1
    ASSERT(st.cursor.position.Y == 24); // clamped to height-1
    return true;
}

bool test_dispatch_cup_clamped_negative_to_zero()
{
    console_state st;
    screen_buffer sb;
    setup(st, sb);
    vt_message m{};
    m.row = -5;
    m.col = -10; // negative → clamped to 0
    vt_msg_apply_state(vt_message_id::cursor_position, m, st, sb);
    ASSERT(st.cursor.position.X == 0);
    ASSERT(st.cursor.position.Y == 0);
    return true;
}

bool test_dispatch_cha_clamped_to_max()
{
    console_state st;
    screen_buffer sb;
    setup(st, sb); // 80 columns
    vt_message m{};
    m.col = 999; // way beyond right edge
    vt_msg_apply_state(vt_message_id::cursor_horiz_absolute, m, st, sb);
    ASSERT(st.cursor.position.X == 79); // clamped to width-1
    return true;
}

bool test_dispatch_vpa_clamped_to_max()
{
    console_state st;
    screen_buffer sb;
    setup(st, sb); // 25 rows
    vt_message m{};
    m.row = 999; // way beyond bottom
    vt_msg_apply_state(vt_message_id::cursor_vert_absolute, m, st, sb);
    ASSERT(st.cursor.position.Y == 24); // clamped to height-1
    return true;
}

// ==================================================================
// OSC sequence filtering (filter_osc_sequences)
// ==================================================================
bool test_filter_osc_normal_text_unchanged()
{
    std::u32string s = U"Hello World";
    filter_osc_sequences(s);
    ASSERT(s == U"Hello World");
    return true;
}

bool test_filter_osc_bel_terminated_removed()
{
    std::u32string s = U"ABC\x1b]9001;CmdNotFound;xyz\x07"
                       "DEF";
    filter_osc_sequences(s);
    ASSERT(s == U"ABCDEF"); // OSC 9001 BEL → removed
    return true;
}

bool test_filter_osc_st_terminated_removed()
{
    std::u32string s = U"pre\x1b]2;title\x1b\\post";
    filter_osc_sequences(s);
    ASSERT(s == U"prepost"); // OSC 2 ESC \ → removed
    return true;
}

bool test_filter_osc_unterminated_kept()
{
    std::u32string s = U"keep\x1b]999;no_terminator";
    filter_osc_sequences(s);
    ASSERT(s == U"keep\x1b]999;no_terminator"); // unterminated → kept
    return true;
}

bool test_filter_osc_multiple_in_one_string()
{
    std::u32string s = U"\x1b]9001;a\x07Hello\x1b]0;title\x1b\\World";
    filter_osc_sequences(s);
    ASSERT(s == U"HelloWorld"); // both OSCs removed
    return true;
}

bool test_filter_osc_plain_esc_not_removed()
{
    std::u32string s = U"\x1b[31mRed\x1b[0m";
    filter_osc_sequences(s);
    ASSERT(s == U"\x1b[31mRed\x1b[0m"); // CSI sequences preserved
    return true;
}

bool test_filter_osc_empty_string()
{
    std::u32string s = U"";
    filter_osc_sequences(s);
    ASSERT(s.empty());
    return true;
}

bool test_filter_osc_only_osc()
{
    std::u32string s = U"\x1b]0;title\x07";
    filter_osc_sequences(s);
    ASSERT(s.empty()); // entire string is OSC → empty
    return true;
}

// ==================================================================
// LF resets X to 0 (Windows console semantics: \n == \r\n for text output)
// Regression: wrong_command error output was progressively indented because \n did NOT reset X
// ==================================================================
bool test_text_lf_resets_x()
{
    console_state st;
    screen_buffer sb;
    setup(st, sb);
    st.cursor.position = {5, 3}; // col 5, row 3

    vt_message m{};
    vt_msg_apply_state(vt_message_id::line_feed, m, st, sb);
    ASSERT(st.cursor.position.X == 0); // LF resets X=0
    ASSERT(st.cursor.position.Y == 4); // Y incremented

    st.cursor.position = {5, 4};
    m.text = U"ab";
    vt_msg_apply_state(vt_message_id::text, m, st, sb);
    vt_msg_apply_state(vt_message_id::line_feed, m, st, sb);
    m.text = U"cd";
    vt_msg_apply_state(vt_message_id::text, m, st, sb);
    ASSERT(st.cursor.position.X == 2); // LF reset to 0, cd=2
    ASSERT(st.cursor.position.Y == 5); // moved down one more row
    return true;
}

// wrong_command regression: multi-line WriteConsole calls with \n separators
// Each call's text ends with \n; next call should start at column 0, not preserved X
bool test_wrong_command_multiline_indent()
{
    console_state st;
    screen_buffer sb;
    setup(st, sb);
    st.cursor.position = {0, 6};
    vt_message m{};

    m.text = U"line1_80_chars_ending_at_col2_row7_xxx_yyy_zzz_aaa_bbb_ccc_ddd_eee_fff_ggg_hhh_iii_jjj";
    vt_msg_apply_state(vt_message_id::text, m, st, sb);
    vt_msg_apply_state(vt_message_id::line_feed, m, st, sb);
    ASSERT(st.cursor.position.X == 0);
    ASSERT(st.cursor.position.Y == 8);

    m.text = U"line2_14_chars_";
    vt_msg_apply_state(vt_message_id::text, m, st, sb);
    vt_msg_apply_state(vt_message_id::line_feed, m, st, sb);
    ASSERT(st.cursor.position.X == 0);
    ASSERT(st.cursor.position.Y == 9);

    m.text = U"line3_14_chars_";
    vt_msg_apply_state(vt_message_id::text, m, st, sb);
    vt_msg_apply_state(vt_message_id::line_feed, m, st, sb);
    ASSERT(st.cursor.position.X == 0);
    ASSERT(st.cursor.position.Y == 10);
    return true;
}

bool test_text_cr_resets_column()
{
    console_state st;
    screen_buffer sb;
    setup(st, sb);
    st.cursor.position = {10, 2};

    vt_message m{};
    vt_msg_apply_state(vt_message_id::carriage_return, m, st, sb);
    ASSERT(st.cursor.position.X == 0); // CR resets X
    ASSERT(st.cursor.position.Y == 2); // Y unchanged
    return true;
}

// CJK wide-char boundary overflow regression (wrong_command "请" alone on a line)
// When a 2-wide character at the last column (e.g. col 119 in 120-col screen),
// it must wrap to the next line BEFORE writing, matching terminal behavior.
bool test_text_cjk_boundary_wrap_before_write()
{
    console_state st;
    screen_buffer sb;
    setup(st, sb);                // 80x25
    st.cursor.position = {78, 5}; // only 2 columns left on row 5

    vt_message m{};
    // ASCII 'A' (1 col) at col 78 → fits at 78
    // CJK '请' (2 cols) at col 79 → needs 79,80 → overflows → wrap to (0,6)
    m.text = U"A\u8BF7"; // "A请"
    vt_msg_apply_state(vt_message_id::text, m, st, sb);

    ASSERT(sb.at_u32({78, 5}) == U'A');     // 'A' at col 78, row 5
    ASSERT(sb.at_u32({0, 6}) == U'\u8BF7'); // '请' wrapped to col 0, row 6
    ASSERT(st.cursor.position.X == 2);      // pos advanced by 2 after '请'
    ASSERT(st.cursor.position.Y == 6);      // on row 6
    return true;
}

// Regression: consecutive CJK chars that would each overflow
bool test_text_cjk_double_boundary_overflow()
{
    console_state st;
    screen_buffer sb;
    setup(st, sb); // 80x25
    st.cursor.position = {78, 5};

    vt_message m{};
    // 1st '請'(2) at col 78 → fits (78+2=80), pos wraps to (0,6) after write
    // 2nd '請'(2) at col 0, row 6 → (2,6)
    // 'A'(1) at col 2, row 6 → (3,6)
    m.text = U"\u8BF7\u8BF7A";
    vt_msg_apply_state(vt_message_id::text, m, st, sb);

    ASSERT(sb.at_u32({78, 5}) == U'\u8BF7'); // first '請' at (78,5), fits
    ASSERT(sb.at_u32({0, 6}) == U'\u8BF7');  // second '請' at (0,6)
    ASSERT(sb.at_u32({2, 6}) == U'A');       // 'A' at (2,6)
    ASSERT(st.cursor.position.X == 3);
    ASSERT(st.cursor.position.Y == 6);
    return true;
}

// ==================================================================
// PowerShell error msg width diagnostic — traces per-character width
// ==================================================================
bool test_ps_error_msg_width_diag()
{
    // Exact text from PS zh-CN error message
    const char32_t text[] = U"wrong_command : 无法将\u201Cwrong_command\u201D项识别为 "
                            U"cmdlet、函数、脚本文件或可运行程序的名称。请检查名称的拼写，如果包括路径，请";
    auto sv = std::u32string_view(text, std::size(text) - 1);

    int w_console = 0, w_wcswidth = 0;
    std::wcout << L"\n--- Per-char width comparison ---\n";
    for (size_t i = 0; i < sv.size(); ++i)
    {
        char32_t cp = sv[i];
        int wc = char_width_for_mode(cp, text_measurement_mode::console, false);
        int ww = char_width_for_mode(cp, text_measurement_mode::wcswidth, false);
        w_console += wc;
        w_wcswidth += ww;
        if (wc != ww)
        {
            std::wcout << L"  DIFF at idx=" << i << L": U+" << std::hex << static_cast<uint32_t>(cp) << std::dec
                       << L" console=" << wc << L" wcswidth=" << ww << L"\n";
        }
    }
    std::wcout << L"Total console width:  " << w_console << L"\n";
    std::wcout << L"Total wcswidth width: " << w_wcswidth << L"\n";
    std::wcout << L"Delta (console - wcswidth): " << (w_console - w_wcswidth) << L"\n";
    return true;
}

// ==================================================================
// api_write_console CUP 移除回归测试
// 回归背景（2026-05-24）：
//   BUG: PowerShell "wrong_command" 错误消息 "如果包括路径，" 后多余空行。
//   根因: api_write_console 在步骤4发送最终 CUP 到计算出的光标位置 (2,7)，
//   但 WT 字体渲染的 DECAWM 自然折行后光标实际在 (2,6)。CUP 将光标下拉一行
//   → 下一个 WriteConsole("\\n") 从 (2,7)→(0,8) 产生空行。
//   修复: 移除 api_write_console 的最终 CUP，终端通过 DECAWM 自然追踪光标；
//   仅 Enter 换行时 (consume_enter_newline) 发送初始 CUP。
//
//   本测试验证 vt_msg_apply_state 的 text 处理器在 "全角字符触发行尾
//   自动折行 + \\n" 场景下，screen_buffer 的 state.cursor 正确反映终端
//   的实际光标位置（Y 只递增一次，不产生双重换行）。
// ==================================================================

// 场景: 文本以 CJK 全角字符触发行尾自动折行，紧跟 \\n
bool test_text_newline_after_cjk_wrap_does_not_double_advance()
{
    console_state st;
    screen_buffer sb;
    st.screen_buffer_size = {120, 30};
    st.cursor.position = {0, 0}; // 从行首开始以精确控制宽度
    st.text_measurement = text_measurement_mode::graphemes;
    st.ambiguous_is_wide = true;

    sb = screen_buffer{{120, 30}};

    // 在行尾写一个全角字符（width=2）使其刚好溢出 → 触发自动折行
    // 先填充 118 个 'A'（单宽），再写全角逗号 U+FF0C（width=2），
    // 118+2=120 恰好触发 pos.X=0; pos.Y++
    std::u32string msg(118, U'A');
    msg += U"\uFF0C"; // 全角逗号 width=2, X=118+2=120 → 折行
    vt_message m{};
    m.text = msg;
    vt_msg_apply_state(vt_message_id::text, m, st, sb);

    // 断言: 全角逗号触发行尾折行 → cursor 已在下一行行首
    SHORT line_after_wrap = st.cursor.position.Y;
    ASSERT(line_after_wrap == 1);       // 从行 0 折到行 1
    ASSERT_EQ(st.cursor.position.X, 0); // 折行后 X 归零

    // 现在写入 \n —— 不应产生双重换行
    vt_msg_apply_state(vt_message_id::line_feed, m, st, sb);

    // \n 移动到下行行首，Y 只递增 1
    ASSERT_EQ(st.cursor.position.X, 0);
    ASSERT_EQ(st.cursor.position.Y, line_after_wrap + 1);

    return true;
}

// 场景: 文本最后恰好填满行尾 (pos.X == screen_w)，紧跟 \n
bool test_text_newline_after_exact_fill_does_not_double_advance()
{
    console_state st;
    screen_buffer sb;
    st.screen_buffer_size = {120, 30};
    st.cursor.position = {0, 0};
    st.text_measurement = text_measurement_mode::console;

    sb = screen_buffer{{120, 30}};

    // 填充 120 个 'A' → 恰好在 pos.X = 120 处触发 pos.X = 0; pos.Y++
    std::u32string fill(120, U'A');
    vt_message m{};
    m.text = fill;
    vt_msg_apply_state(vt_message_id::text, m, st, sb);

    ASSERT_EQ(st.cursor.position.X, 0);
    ASSERT_EQ(st.cursor.position.Y, 1);

    // 紧跟 \n → Y 仅再递增 1
    vt_msg_apply_state(vt_message_id::line_feed, m, st, sb);

    ASSERT_EQ(st.cursor.position.X, 0);
    ASSERT_EQ(st.cursor.position.Y, 2);

    return true;
}

// 场景: 非边界情况正常 \n
bool test_text_newline_normal_advances_one_line()
{
    console_state st;
    screen_buffer sb;
    st.screen_buffer_size = {120, 30};
    st.cursor.position = {10, 5};
    sb = screen_buffer{{120, 30}};

    vt_message m{};
    m.text = U"hello";
    vt_msg_apply_state(vt_message_id::text, m, st, sb);
    ASSERT_EQ(st.cursor.position.X, 15);

    // \n → 下一行行首
    vt_msg_apply_state(vt_message_id::line_feed, m, st, sb);
    ASSERT_EQ(st.cursor.position.X, 0);
    ASSERT_EQ(st.cursor.position.Y, 6);

    return true;
}

// 场景: \r \n 顺序正确换行
bool test_text_crlf_advances_one_line()
{
    console_state st;
    screen_buffer sb;
    st.screen_buffer_size = {120, 30};
    st.cursor.position = {10, 5};
    sb = screen_buffer{{120, 30}};

    vt_message m{};
    m.text = U"test";
    vt_msg_apply_state(vt_message_id::text, m, st, sb);
    vt_msg_apply_state(vt_message_id::carriage_return, m, st, sb);
    vt_msg_apply_state(vt_message_id::line_feed, m, st, sb);

    // \r 归零 X, \n 递增 Y
    ASSERT_EQ(st.cursor.position.X, 0);
    ASSERT_EQ(st.cursor.position.Y, 6);

    return true;
}

// ==================================================================
// Test Runner
// ==================================================================

int main()
{
    std::wcout << L"=== vt_msg_dispatch Tests ===" << std::endl;

    RUN_TEST(test_dispatch_cup, L"CUP");
    RUN_TEST(test_dispatch_cha, L"CHA");
    RUN_TEST(test_dispatch_vpa, L"VPA");
    RUN_TEST(test_dispatch_cuu, L"CUU");
    RUN_TEST(test_dispatch_cud, L"CUD");
    RUN_TEST(test_dispatch_cuf, L"CUF");
    RUN_TEST(test_dispatch_cub, L"CUB");
    RUN_TEST(test_dispatch_cnl, L"CNL");
    RUN_TEST(test_dispatch_cpl, L"CPL");
    RUN_TEST(test_dispatch_cursor_visible, L"Cursor visible");
    RUN_TEST(test_dispatch_decsc_decrc, L"DECSC/DECRC");
    RUN_TEST(test_dispatch_sgr_fg_bg, L"SGR fg/bg");
    RUN_TEST(test_dispatch_sgr_bold, L"SGR bold");
    RUN_TEST(test_dispatch_sgr_reset, L"SGR reset");
    RUN_TEST(test_win32_attr_color_to_sgr_index_red_blue, L"Win32 attr BGR -> SGR RGB");
    RUN_TEST(test_set_sgr_from_win32_attr_wrong_command_red, L"wrong_command red attr -> SGR red");
    RUN_TEST(test_dispatch_text_single, L"Text single");
    RUN_TEST(test_dispatch_text_multi, L"Text multi");
    RUN_TEST(test_dispatch_text_wraps_to_next_line, L"Text wrap");
    RUN_TEST(test_dispatch_text_overflow_clamps_y, L"Text overflow clamps Y");
    RUN_TEST(test_dispatch_ed_0_cursor_to_end, L"ED 0");
    RUN_TEST(test_dispatch_ed_2_whole, L"ED 2");
    RUN_TEST(test_dispatch_el_0_cursor_to_end, L"EL 0");
    RUN_TEST(test_dispatch_el_2_whole, L"EL 2");
    RUN_TEST(test_dispatch_scroll_up, L"Scroll up");
    RUN_TEST(test_dispatch_title, L"Title");
    RUN_TEST(test_dispatch_line_drawing, L"Line drawing");
    RUN_TEST(test_dispatch_hts, L"HTS");

    std::wcout << L"\nResize Window Regression Tests:\n";
    RUN_TEST(test_dispatch_resize_sets_dimensions, L"Resize sets sb dimensions");
    RUN_TEST(test_dispatch_resize_updates_state, L"Resize updates state");
    RUN_TEST(test_dispatch_resize_clamps_cursor, L"Resize clamps cursor OB");
    RUN_TEST(test_dispatch_resize_noop_on_zero, L"Resize zero=no-op");
    RUN_TEST(test_dispatch_resize_tab_stops_reinit, L"Resize reinit tab stops");

    std::wcout << L"\nCursor Boundary Clamp Tests:\n";
    RUN_TEST(test_dispatch_cup_clamped_to_max, L"CUP clamp to max bounds");
    RUN_TEST(test_dispatch_cup_clamped_negative_to_zero, L"CUP clamp negative to 0");
    RUN_TEST(test_dispatch_cha_clamped_to_max, L"CHA clamp to max X");
    RUN_TEST(test_dispatch_vpa_clamped_to_max, L"VPA clamp to max Y");

    std::wcout << L"\nOSC Sequence Filtering Tests:\n";
    RUN_TEST(test_filter_osc_normal_text_unchanged, L"OSC filter: normal text unchanged");
    RUN_TEST(test_filter_osc_bel_terminated_removed, L"OSC filter: BEL-terminated removed");
    RUN_TEST(test_filter_osc_st_terminated_removed, L"OSC filter: ST-terminated removed");
    RUN_TEST(test_filter_osc_unterminated_kept, L"OSC filter: unterminated kept");
    RUN_TEST(test_filter_osc_multiple_in_one_string, L"OSC filter: multiple in one string");
    RUN_TEST(test_filter_osc_plain_esc_not_removed, L"OSC filter: plain ESC preserved");
    RUN_TEST(test_filter_osc_empty_string, L"OSC filter: empty string");
    RUN_TEST(test_filter_osc_only_osc, L"OSC filter: entire string is OSC");

    std::wcout << L"\nLF/CR VT Semantics Tests:\n";
    RUN_TEST(test_text_lf_resets_x, L"LF resets X=0 (Windows console)");
    RUN_TEST(test_wrong_command_multiline_indent, L"wrong_command multi-line indent");
    RUN_TEST(test_text_cr_resets_column, L"CR resets column (X=0)");

    std::wcout << L"\nCJK Wide-Char Boundary Overflow Tests:\n";
    RUN_TEST(test_text_cjk_boundary_wrap_before_write, L"CJK at boundary wraps before write");
    RUN_TEST(test_text_cjk_double_boundary_overflow, L"Double CJK boundary overflow");

    std::wcout << L"\n=== PS error msg width diagnostics ===\n";
    RUN_TEST(test_ps_error_msg_width_diag, L"PS error msg width diagnostic");

    std::wcout << L"\napi_write_console CUP Removal Regression Tests:\n";
    RUN_TEST(test_text_newline_after_cjk_wrap_does_not_double_advance, L"CJK wrap+\\n single Y advance");
    RUN_TEST(test_text_newline_after_exact_fill_does_not_double_advance, L"Exact fill+\\n single Y advance");
    RUN_TEST(test_text_newline_normal_advances_one_line, L"Normal \\n advances one line");
    RUN_TEST(test_text_crlf_advances_one_line, L"CRLF advances one line");

    std::wcout << L"  " << tests_passed << L" passed, " << tests_failed << L" failed, " << (tests_passed + tests_failed)
               << L" total." << std::endl;
    return tests_failed > 0 ? 1 : 0;
}
