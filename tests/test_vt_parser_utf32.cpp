// ── tests/test_vt_parser_utf32.cpp ─────────────────────────
// Unit tests for the char32_t VT parser public API.
//
// These tests exercise the parser through vt_parse_result only. They do not
// inspect parser internals, so they protect the contract used by pipe_bridge,
// api_handlers and the keyboard input path.

#include "test_common.hpp"
#include "conpty_vt_parser.hpp"
#include "vt_parser_test_helpers.hpp"
#include "utility/crtdbg.hpp"

#include <array>
#include <string>
#include <string_view>
#include <vector>

using namespace conpty;
using conpty::test::is_parse_continue;
using conpty::test::parse_one;
using conpty::test::reset_test_vt_parser_message;

struct expected_sequence
{
    std::u32string_view sequence;
    vt_message_id id;
};

vt_parse_result parse_complete(vt_parser &parser, std::u32string_view input)
{
    while (!input.empty())
    {
        auto result = parser.parse(input);
        input.remove_prefix(result.consumed);
        if (!is_parse_continue(result))
            return result;
    }
    return {};
}

bool expect_id_and_raw(std::u32string_view sequence, vt_message_id id)
{
    raw_u32_buffer raw;
    vt_parser parser{raw};

    const auto result = parse_complete(parser, sequence);
    ASSERT(result.id == id);
    ASSERT(result.consumed == sequence.size());
    if (sequence.starts_with(U"\x1b"))
        ASSERT(result.raw_sequence == sequence);
    else
        ASSERT(result.raw_sequence.empty());
    return true;
}

bool expect_unknown(std::u32string_view sequence)
{
    raw_u32_buffer raw;
    vt_parser parser{raw};

    const auto result = parse_complete(parser, sequence);
    ASSERT(result.id == vt_message_id::unknown_sequence);
    ASSERT(result.consumed == sequence.size());
    ASSERT(result.raw_sequence == sequence);
    ASSERT(result.message.payload.text == sequence);
    return true;
}

bool test_ground_text_range()
{
    raw_u32_buffer raw;
    vt_parser parser{raw};

    const auto result = parser.parse(U"hello world");
    ASSERT(result.id == vt_message_id::continue_text);
    ASSERT(result.consumed == 11);
    ASSERT(result.raw_sequence.empty());
    ASSERT(result.message.payload.text == U"hello world");
    return true;
}

bool test_ground_text_stops_before_control()
{
    raw_u32_buffer raw;
    vt_parser parser{raw};

    auto input = std::u32string_view{U"hello\rworld"};
    auto result = parser.parse(input);
    ASSERT(result.id == vt_message_id::continue_text);
    ASSERT(result.consumed == 5);
    ASSERT(result.message.payload.text == U"hello");
    parser.reset<vt_message_id::continue_text>();

    input.remove_prefix(result.consumed);
    result = parser.parse(input);
    ASSERT(result.id == vt_message_id::carriage_return);
    ASSERT(result.consumed == 1);
    ASSERT(result.raw_sequence.empty());
    reset_test_vt_parser_message(parser, result.id);

    input.remove_prefix(result.consumed);
    result = parser.parse(input);
    ASSERT(result.id == vt_message_id::continue_text);
    ASSERT(result.consumed == 5);
    ASSERT(result.message.payload.text == U"world");
    return true;
}

bool test_ground_controls()
{
    raw_u32_buffer raw;
    vt_parser parser{raw};

    auto result = parse_one(parser, U'\r');
    ASSERT(result.id == vt_message_id::carriage_return);
    ASSERT(result.consumed == 1);
    reset_test_vt_parser_message(parser, result.id);

    result = parse_one(parser, U'\n');
    ASSERT(result.id == vt_message_id::line_feed);
    reset_test_vt_parser_message(parser, result.id);

    result = parse_one(parser, U'\t');
    ASSERT(result.id == vt_message_id::cursor_forward_tab);
    ASSERT(result.message.payload.count.value == 1);
    reset_test_vt_parser_message(parser, result.id);

    result = parse_one(parser, U'\b');
    ASSERT(result.id == vt_message_id::char_del);
    reset_test_vt_parser_message(parser, result.id);

    result = parse_one(parser, 0x7f);
    ASSERT(result.id == vt_message_id::char_del);
    reset_test_vt_parser_message(parser, result.id);

    result = parse_one(parser, 0x1a);
    ASSERT(result.id == vt_message_id::char_sub);
    reset_test_vt_parser_message(parser, result.id);

    result = parse_one(parser, 0x00);
    ASSERT(result.id == vt_message_id::char_nul);
    return true;
}

bool test_esc_sequences()
{
    constexpr std::array cases{
        expected_sequence{U"\x1bM", vt_message_id::reverse_index},
        expected_sequence{U"\x1b"
                          "7",
                          vt_message_id::save_cursor},
        expected_sequence{U"\x1b"
                          "8",
                          vt_message_id::restore_cursor},
        expected_sequence{U"\x1bH", vt_message_id::horizontal_tab_set},
        expected_sequence{U"\x1b=", vt_message_id::keypad_app_mode},
        expected_sequence{U"\x1b>", vt_message_id::keypad_numeric_mode},
        expected_sequence{U"\x1b(0", vt_message_id::designate_charset_line_drawing},
        expected_sequence{U"\x1b(B", vt_message_id::designate_charset_ascii},
    };

    for (const auto &item : cases)
        ASSERT(expect_id_and_raw(item.sequence, item.id));
    return true;
}

bool test_csi_cursor_and_edit_sequences()
{
    constexpr std::array cases{
        expected_sequence{U"\x1b[12A", vt_message_id::cursor_up},
        expected_sequence{U"\x1b[2B", vt_message_id::cursor_down},
        expected_sequence{U"\x1b[3C", vt_message_id::cursor_forward},
        expected_sequence{U"\x1b[4D", vt_message_id::cursor_backward},
        expected_sequence{U"\x1b[5E", vt_message_id::cursor_next_line},
        expected_sequence{U"\x1b[6F", vt_message_id::cursor_prev_line},
        expected_sequence{U"\x1b[7S", vt_message_id::scroll_up},
        expected_sequence{U"\x1b[8T", vt_message_id::scroll_down},
        expected_sequence{U"\x1b[9@", vt_message_id::insert_characters},
        expected_sequence{U"\x1b[10P", vt_message_id::delete_characters},
        expected_sequence{U"\x1b[11X", vt_message_id::erase_characters},
        expected_sequence{U"\x1b[12L", vt_message_id::insert_lines},
        expected_sequence{U"\x1b[13M", vt_message_id::delete_lines},
        expected_sequence{U"\x1b[14I", vt_message_id::cursor_forward_tab},
        expected_sequence{U"\x1b[15Z", vt_message_id::cursor_backward_tab},
    };

    for (const auto &item : cases)
    {
        raw_u32_buffer raw;
        vt_parser parser{raw};
        const auto result = parse_complete(parser, item.sequence);
        ASSERT(result.id == item.id);
        ASSERT(result.raw_sequence == item.sequence);
        ASSERT(result.message.payload.count.value >= 1);
    }
    return true;
}

bool test_csi_position_sequences()
{
    raw_u32_buffer raw;
    vt_parser parser{raw};

    auto result = parser.parse(U"\x1b[5;9H");
    ASSERT(result.id == vt_message_id::cursor_position);
    ASSERT(result.message.payload.position.row == 5);
    ASSERT(result.message.payload.position.col == 9);
    reset_test_vt_parser_message(parser, result.id);

    result = parser.parse(U"\x1b[6;10f");
    ASSERT(result.id == vt_message_id::cursor_position);
    ASSERT(result.message.payload.position.row == 6);
    ASSERT(result.message.payload.position.col == 10);
    reset_test_vt_parser_message(parser, result.id);

    result = parser.parse(U"\x1b[42G");
    ASSERT(result.id == vt_message_id::cursor_horiz_absolute);
    ASSERT(result.message.payload.position.col == 42);
    reset_test_vt_parser_message(parser, result.id);

    result = parser.parse(U"\x1b[7d");
    ASSERT(result.id == vt_message_id::cursor_vert_absolute);
    ASSERT(result.message.payload.position.row == 7);
    reset_test_vt_parser_message(parser, result.id);

    result = parser.parse(U"\x1b[0;0H");
    ASSERT(result.id == vt_message_id::cursor_position);
    ASSERT(result.message.payload.position.row == 1);
    ASSERT(result.message.payload.position.col == 1);
    return true;
}

bool test_csi_erase_tabs_scroll_region_and_shape()
{
    raw_u32_buffer raw;
    vt_parser parser{raw};

    auto result = parser.parse(U"\x1b[2J");
    ASSERT(result.id == vt_message_id::erase_in_display);
    ASSERT(result.message.payload.erase_mode == 2);
    reset_test_vt_parser_message(parser, result.id);

    result = parser.parse(U"\x1b[1K");
    ASSERT(result.id == vt_message_id::erase_in_line);
    ASSERT(result.message.payload.erase_mode == 1);
    reset_test_vt_parser_message(parser, result.id);

    result = parser.parse(U"\x1b[3;20r");
    ASSERT(result.id == vt_message_id::set_scrolling_region);
    ASSERT(result.message.payload.scroll_region.top == 3);
    ASSERT(result.message.payload.scroll_region.bottom == 20);
    reset_test_vt_parser_message(parser, result.id);

    result = parser.parse(U"\x1b[4 q");
    ASSERT(result.id == vt_message_id::set_cursor_shape);
    ASSERT(result.message.payload.cursor_shape == 4);
    reset_test_vt_parser_message(parser, result.id);

    result = parser.parse(U"\x1b[0g");
    ASSERT(result.id == vt_message_id::tab_clear_current);
    reset_test_vt_parser_message(parser, result.id);

    result = parser.parse(U"\x1b[3g");
    ASSERT(result.id == vt_message_id::tab_clear_all);
    return true;
}

bool test_csi_modes_queries_and_buffers()
{
    constexpr std::array cases{
        expected_sequence{U"\x1b[s", vt_message_id::ansi_save_cursor},
        expected_sequence{U"\x1b[u", vt_message_id::ansi_restore_cursor},
        expected_sequence{U"\x1b[?12h", vt_message_id::cursor_enable_blinking},
        expected_sequence{U"\x1b[?12l", vt_message_id::cursor_disable_blinking},
        expected_sequence{U"\x1b[?25h", vt_message_id::cursor_show},
        expected_sequence{U"\x1b[?25l", vt_message_id::cursor_hide},
        expected_sequence{U"\x1b[?1h", vt_message_id::cursor_keys_app_mode},
        expected_sequence{U"\x1b[?1l", vt_message_id::cursor_keys_normal_mode},
        expected_sequence{U"\x1b[?3h", vt_message_id::set_columns_132},
        expected_sequence{U"\x1b[?3l", vt_message_id::set_columns_80},
        expected_sequence{U"\x1b[?1049h", vt_message_id::use_alternate_buffer},
        expected_sequence{U"\x1b[?1049l", vt_message_id::use_main_buffer},
        expected_sequence{U"\x1b[6n", vt_message_id::report_cursor_position},
        expected_sequence{U"\x1b[0c", vt_message_id::device_attributes},
        expected_sequence{U"\x1b[!p", vt_message_id::soft_reset},
    };

    for (const auto &item : cases)
        ASSERT(expect_id_and_raw(item.sequence, item.id));
    return true;
}

bool test_sgr_flags_clear_and_colors()
{
    raw_u32_buffer raw;
    vt_parser parser{raw};

    auto result = parser.parse(U"\x1b[0;1;2;3;4;5;7;8;9m");
    ASSERT(result.id == vt_message_id::sgr);
    const auto &set_sgr = result.message.payload.sgr;
    ASSERT(set_sgr.has_reset());
    ASSERT(set_sgr.has(vt_sgr_flag::bold));
    ASSERT(set_sgr.has(vt_sgr_flag::faint));
    ASSERT(set_sgr.has(vt_sgr_flag::italic));
    ASSERT(set_sgr.has(vt_sgr_flag::underline));
    ASSERT(set_sgr.has(vt_sgr_flag::blink));
    ASSERT(set_sgr.has(vt_sgr_flag::negative));
    ASSERT(set_sgr.has(vt_sgr_flag::conceal));
    ASSERT(set_sgr.has(vt_sgr_flag::strikethrough));
    reset_test_vt_parser_message(parser, result.id);

    result = parser.parse(U"\x1b[22;23;24;25;27;28;29m");
    ASSERT(result.id == vt_message_id::sgr);
    const auto &clear_sgr = result.message.payload.sgr;
    ASSERT(clear_sgr.clears(vt_sgr_flag::bold));
    ASSERT(clear_sgr.clears(vt_sgr_flag::faint));
    ASSERT(clear_sgr.clears(vt_sgr_flag::italic));
    ASSERT(clear_sgr.clears(vt_sgr_flag::underline));
    ASSERT(clear_sgr.clears(vt_sgr_flag::blink));
    ASSERT(clear_sgr.clears(vt_sgr_flag::negative));
    ASSERT(clear_sgr.clears(vt_sgr_flag::conceal));
    ASSERT(clear_sgr.clears(vt_sgr_flag::strikethrough));
    reset_test_vt_parser_message(parser, result.id);

    result = parser.parse(U"\x1b[31;94;48;5;123m");
    ASSERT(result.id == vt_message_id::sgr);
    ASSERT(result.message.payload.sgr.fg.is_indexed());
    ASSERT(result.message.payload.sgr.fg.value == 12);
    ASSERT(result.message.payload.sgr.bg.is_indexed());
    ASSERT(result.message.payload.sgr.bg.value == 123);
    reset_test_vt_parser_message(parser, result.id);

    result = parser.parse(U"\x1b[38;2;10;20;30;48;2;40;50;60m");
    ASSERT(result.id == vt_message_id::sgr);
    ASSERT(result.message.payload.sgr.fg.is_rgb());
    ASSERT(result.message.payload.sgr.fg.value == 10);
    ASSERT(result.message.payload.sgr.fg.g == 20);
    ASSERT(result.message.payload.sgr.fg.b == 30);
    ASSERT(result.message.payload.sgr.bg.is_rgb());
    ASSERT(result.message.payload.sgr.bg.value == 40);
    ASSERT(result.message.payload.sgr.bg.g == 50);
    ASSERT(result.message.payload.sgr.bg.b == 60);
    reset_test_vt_parser_message(parser, result.id);

    result = parser.parse(U"\x1b[39;49m");
    ASSERT(result.id == vt_message_id::sgr);
    ASSERT(result.message.payload.sgr.fg.is_default());
    ASSERT(result.message.payload.sgr.bg.is_default());
    return true;
}

bool test_osc_title_bel_and_st()
{
    raw_u32_buffer raw;
    vt_parser parser{raw};

    auto result = parser.parse(U"\x1b]0;PowerShell\x07");
    ASSERT(result.id == vt_message_id::set_window_title);
    ASSERT(result.message.payload.title == U"PowerShell");
    ASSERT(result.raw_sequence == U"\x1b]0;PowerShell\x07");
    reset_test_vt_parser_message(parser, result.id);

    result = parser.parse(U"\x1b]2;corehost\x1b\\");
    ASSERT(result.id == vt_message_id::set_window_title);
    ASSERT(result.message.payload.title == U"corehost");
    ASSERT(result.raw_sequence == U"\x1b]2;corehost\x1b\\");
    return true;
}

bool test_osc_palette()
{
    raw_u32_buffer raw;
    vt_parser parser{raw};

    const auto result = parser.parse(U"\x1b]4;12;rgb:0a/14/1e\x07");
    ASSERT(result.id == vt_message_id::set_palette_color);
    ASSERT(result.message.payload.palette.index == 12);
    ASSERT(result.message.payload.palette.r == 0x0a);
    ASSERT(result.message.payload.palette.g == 0x14);
    ASSERT(result.message.payload.palette.b == 0x1e);
    return true;
}

bool test_ss3_keys()
{
    constexpr std::array cases{
        expected_sequence{U"\x1bOA", vt_message_id::key_up},    expected_sequence{U"\x1bOB", vt_message_id::key_down},
        expected_sequence{U"\x1bOC", vt_message_id::key_right}, expected_sequence{U"\x1bOD", vt_message_id::key_left},
        expected_sequence{U"\x1bOH", vt_message_id::key_home},  expected_sequence{U"\x1bOF", vt_message_id::key_end},
        expected_sequence{U"\x1bOP", vt_message_id::key_f1},    expected_sequence{U"\x1bOQ", vt_message_id::key_f2},
        expected_sequence{U"\x1bOR", vt_message_id::key_f3},    expected_sequence{U"\x1bOS", vt_message_id::key_f4},
    };

    for (const auto &item : cases)
        ASSERT(expect_id_and_raw(item.sequence, item.id));
    return true;
}

bool test_csi_tilde_keys()
{
    constexpr std::array cases{
        expected_sequence{U"\x1b[2~", vt_message_id::key_insert},
        expected_sequence{U"\x1b[3~", vt_message_id::key_delete},
        expected_sequence{U"\x1b[5~", vt_message_id::key_page_up},
        expected_sequence{U"\x1b[6~", vt_message_id::key_page_down},
        expected_sequence{U"\x1b[15~", vt_message_id::key_f5},
        expected_sequence{U"\x1b[17~", vt_message_id::key_f6},
        expected_sequence{U"\x1b[18~", vt_message_id::key_f7},
        expected_sequence{U"\x1b[19~", vt_message_id::key_f8},
        expected_sequence{U"\x1b[20~", vt_message_id::key_f9},
        expected_sequence{U"\x1b[21~", vt_message_id::key_f10},
        expected_sequence{U"\x1b[23~", vt_message_id::key_f11},
        expected_sequence{U"\x1b[24~", vt_message_id::key_f12},
    };

    for (const auto &item : cases)
        ASSERT(expect_id_and_raw(item.sequence, item.id));
    return true;
}

bool test_resize_cpr_and_win32_input()
{
    raw_u32_buffer raw;
    vt_parser parser{raw};

    auto result = parser.parse(U"\x1b[8;30;120t");
    ASSERT(result.id == vt_message_id::resize_window);
    ASSERT(result.message.payload.resize.rows == 30);
    ASSERT(result.message.payload.resize.cols == 120);
    reset_test_vt_parser_message(parser, result.id);

    result = parser.parse(U"\x1b[24;80R");
    ASSERT(result.id == vt_message_id::cpr_response);
    ASSERT(result.message.payload.cpr.row == 24);
    ASSERT(result.message.payload.cpr.col == 80);
    reset_test_vt_parser_message(parser, result.id);

    result = parser.parse(U"\x1b[13;28;20320;1;32;2_");
    ASSERT(result.id == vt_message_id::win32_input_key);
    const auto &key = result.message.payload.win32_key;
    ASSERT(key.vk == 13);
    ASSERT(key.sc == 28);
    ASSERT(key.uc == 20320);
    ASSERT(key.key_down);
    ASSERT(key.control_state == 32);
    ASSERT(key.repeat_count == 2);
    return true;
}

bool test_unknown_sequences_preserve_raw()
{
    constexpr std::array cases{
        U"\x1bX",           U"\x1b(Z",        U"\x1b[?2A", U"\x1b[999~",           U"\x1b[38;2;1m",
        U"\x1b[4;480;640t", U"\x1b[8;0;120t", U"\x1b[4q",  U"\x1b]99;ignored\x07", U"\x1b]4;12;not-rgb\x07",
        U"\x1bOZ",
    };

    for (const auto sequence : cases)
        ASSERT(expect_unknown(sequence));
    return true;
}

bool test_incomplete_sequences_continue_across_calls()
{
    raw_u32_buffer raw;
    vt_parser parser{raw};

    auto result = parser.parse(U"\x1b[38;2;1");
    ASSERT(result.id == vt_message_id::continue_);
    ASSERT(result.consumed == 8);

    result = parser.parse(U";2;3m");
    ASSERT(result.id == vt_message_id::sgr);
    ASSERT(result.consumed == 5);
    ASSERT(result.raw_sequence == U"\x1b[38;2;1;2;3m");
    ASSERT(result.message.payload.sgr.fg.is_rgb());
    ASSERT(result.message.payload.sgr.fg.value == 1);
    ASSERT(result.message.payload.sgr.fg.g == 2);
    ASSERT(result.message.payload.sgr.fg.b == 3);
    reset_test_vt_parser_message(parser, result.id);

    result = parser.parse(U"\x1b]0;split");
    ASSERT(result.id == vt_message_id::continue_);
    result = parser.parse(U" title\x1b\\");
    ASSERT(result.id == vt_message_id::set_window_title);
    ASSERT(result.message.payload.title == U"split title");
    ASSERT(result.raw_sequence == U"\x1b]0;split title\x1b\\");
    return true;
}

bool test_parse_range_consumes_one_complete_message()
{
    raw_u32_buffer raw;
    vt_parser parser{raw};

    auto input = std::u32string_view{U"\x1b[2Jrest"};
    auto result = parser.parse(input);
    ASSERT(result.id == vt_message_id::erase_in_display);
    ASSERT(result.consumed == 4);
    ASSERT(result.message.payload.erase_mode == 2);
    reset_test_vt_parser_message(parser, result.id);

    input.remove_prefix(result.consumed);
    result = parser.parse(input);
    ASSERT(result.id == vt_message_id::continue_text);
    ASSERT(result.consumed == 4);
    ASSERT(result.message.payload.text == U"rest");
    return true;
}

bool test_text_before_vt_sequence_is_delivered_first()
{
    raw_u32_buffer raw;
    vt_parser parser{raw};

    auto input = std::u32string_view{U"abc\x1b[Adef"};
    auto result = parser.parse(input);
    ASSERT(result.id == vt_message_id::continue_text);
    ASSERT(result.consumed == 3);
    ASSERT(result.message.payload.text == U"abc");
    parser.reset<vt_message_id::continue_text>();

    input.remove_prefix(result.consumed);
    result = parser.parse(input);
    ASSERT(result.id == vt_message_id::cursor_up);
    ASSERT(result.consumed == 3);
    ASSERT(result.raw_sequence == U"\x1b[A");
    reset_test_vt_parser_message(parser, result.id);

    input.remove_prefix(result.consumed);
    result = parser.parse(input);
    ASSERT(result.id == vt_message_id::continue_text);
    ASSERT(result.message.payload.text == U"def");
    return true;
}

bool test_reset_clears_payload_for_next_message()
{
    raw_u32_buffer raw;
    vt_parser parser{raw};

    auto result = parser.parse(U"\x1b[8;30;120t");
    ASSERT(result.id == vt_message_id::resize_window);
    ASSERT(result.message.payload.resize.rows == 30);
    ASSERT(result.message.payload.resize.cols == 120);
    reset_test_vt_parser_message(parser, result.id);

    result = parser.parse(U"\x1b[2J");
    ASSERT(result.id == vt_message_id::erase_in_display);
    ASSERT(result.message.payload.erase_mode == 2);
    reset_test_vt_parser_message(parser, result.id);

    result = parser.parse(U"\x1b[8;25;80t");
    ASSERT(result.id == vt_message_id::resize_window);
    ASSERT(result.message.payload.resize.rows == 25);
    ASSERT(result.message.payload.resize.cols == 80);
    return true;
}

bool test_all_supported_sequences_parse_from_one_byte_steps()
{
    constexpr std::array cases{
        expected_sequence{U"\x1bM", vt_message_id::reverse_index},
        expected_sequence{U"\x1b[12A", vt_message_id::cursor_up},
        expected_sequence{U"\x1b[5;9H", vt_message_id::cursor_position},
        expected_sequence{U"\x1b[2J", vt_message_id::erase_in_display},
        expected_sequence{U"\x1b[31m", vt_message_id::sgr},
        expected_sequence{U"\x1b]0;title\x07", vt_message_id::set_window_title},
        expected_sequence{U"\x1bOD", vt_message_id::key_left},
        expected_sequence{U"\x1b[15~", vt_message_id::key_f5},
        expected_sequence{U"\x1b[8;30;120t", vt_message_id::resize_window},
    };

    for (const auto &item : cases)
    {
        raw_u32_buffer raw;
        vt_parser parser{raw};
        vt_parse_result result{};
        for (const auto ch : item.sequence)
        {
            result = parse_one(parser, ch);
            if (!is_parse_continue(result))
                break;
        }
        ASSERT(result.id == item.id);
        ASSERT(result.raw_sequence == item.sequence);
    }
    return true;
}

int main()
{
    utility::suppress_crt_error_dialogs();
    std::wcout << L"VT Parser Tests (vt_parse_result API)\n";

    RUN_TEST(test_ground_text_range, L"Ground text range");
    RUN_TEST(test_ground_text_stops_before_control, L"Ground text stops before control");
    RUN_TEST(test_ground_controls, L"Ground controls");
    RUN_TEST(test_esc_sequences, L"ESC and charset sequences");
    RUN_TEST(test_csi_cursor_and_edit_sequences, L"CSI cursor/edit sequences");
    RUN_TEST(test_csi_position_sequences, L"CSI position sequences");
    RUN_TEST(test_csi_erase_tabs_scroll_region_and_shape, L"CSI erase/tabs/region/shape");
    RUN_TEST(test_csi_modes_queries_and_buffers, L"CSI modes/queries/buffers");
    RUN_TEST(test_sgr_flags_clear_and_colors, L"SGR flags/colors");
    RUN_TEST(test_osc_title_bel_and_st, L"OSC title BEL/ST");
    RUN_TEST(test_osc_palette, L"OSC palette");
    RUN_TEST(test_ss3_keys, L"SS3 keys");
    RUN_TEST(test_csi_tilde_keys, L"CSI tilde keys");
    RUN_TEST(test_resize_cpr_and_win32_input, L"Resize/CPR/Win32 input");
    RUN_TEST(test_unknown_sequences_preserve_raw, L"Unknown sequences preserve raw");
    RUN_TEST(test_incomplete_sequences_continue_across_calls, L"Incomplete sequences continue");
    RUN_TEST(test_parse_range_consumes_one_complete_message, L"Range consumes one message");
    RUN_TEST(test_text_before_vt_sequence_is_delivered_first, L"Text before VT delivered first");
    RUN_TEST(test_reset_clears_payload_for_next_message, L"Reset clears payload");
    RUN_TEST(test_all_supported_sequences_parse_from_one_byte_steps, L"One-byte step parsing");

    std::wcout << L"\nTotal: " << (tests_passed + tests_failed) << L" | Passed: " << tests_passed << L" | Failed: "
               << tests_failed << std::endl;
    return tests_failed == 0 ? 0 : 1;
}
