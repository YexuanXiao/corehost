#include "console_state.hpp"
#include "geometry.hpp"
#include "legacy_projection.hpp"
#include "screen_buffer.hpp"
#include "text_writer.hpp"
#include "win32_types.hpp"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <string_view>
#include <vector>

namespace
{

constexpr corehost::screen::cell_style style0{0};
constexpr corehost::screen::cell_style style1{1};

int failures = 0;

void check(bool value, const char *message)
{
    if (!value)
    {
        std::fprintf(stderr, "FAIL: %s\n", message);
        ++failures;
    }
}

void test_interop_abi()
{
    using namespace corehost::interop::win32;
    static_assert(sizeof(coord) == 4);
    static_assert(sizeof(small_rect) == 8);
    static_assert(sizeof(char_info) == 4);
    static_assert(sizeof(console_screen_buffer_info) == 22);
    static_assert(sizeof(console_cursor_info) == 8);
    static_assert(sizeof(console_selection_info) == 12);
    static_assert(sizeof(console_font_infoex) == 84);

    const char_info value{u'A', default_attributes};
    check(value.char_data.unicode_char == u'A', "char_info unicode");
    check(value.attributes == 0x07, "char_info attributes");
}

void test_geometry()
{
    using namespace corehost::screen;

    viewport view(coord{20, 10});
    check(view.size() == coord{20, 10}, "initial viewport size");
    check(view.set_rect(rect{2, 1, 9, 5}, coord{20, 10}), "set viewport");
    check(view.origin() == coord{2, 1}, "viewport origin");
    check(view.size() == coord{8, 5}, "viewport size");
    check(view.contains(coord{2, 1}) && view.contains(coord{9, 5}), "viewport contains");
    check(!view.contains(coord{10, 5}), "viewport excludes");
    check(view.relative(coord{3, 2}) == coord{1, 1}, "viewport relative");
    check(view.absolute(coord{1, 1}) == coord{3, 2}, "viewport absolute");

    check(view.snap_to_cursor(coord{19, 9}, coord{20, 10}), "snap viewport");
    check(view.bounds().right == 19 && view.bounds().bottom == 9, "snap bounds");

    const auto win32_value = to_win32(coord{4, 5});
    check(win32_value.x == 4 && win32_value.y == 5, "to win32 coord");
    check(from_win32(win32_value) == coord{4, 5}, "from win32 coord");

    corehost::interop::win32::small_rect abi_rect{1, 2, 3, 4};
    const auto generic = from_win32(abi_rect);
    check(generic.left == 1 && generic.bottom == 4, "from win32 rect");
    check(to_win32(generic).right == 3, "to win32 rect");
}

void test_screen_viewport()
{
    using namespace corehost::screen;

    screen_buffer buffer(10, 5);
    check(buffer.buffer_size() == coord{10, 5}, "buffer size");
    check(buffer.viewport_size() == coord{10, 5}, "default viewport");
    buffer.set_viewport(rect{2, 1, 7, 3});
    check(buffer.viewport_origin() == coord{2, 1}, "screen viewport origin");
    check(buffer.viewport_size() == coord{6, 3}, "screen viewport size");
    check(buffer.snap_viewport_to_cursor(coord{9, 4}), "screen snap");

    buffer.resize(8, 4);
    const auto bounds = buffer.view().bounds();
    check(bounds.left >= 0 && bounds.top >= 0, "resize viewport origin");
    check(bounds.right < 8 && bounds.bottom < 4, "resize viewport bounds");
}

void test_char_info_and_attrs()
{
    using namespace corehost::screen;

    screen_buffer buffer(8, 3);
    const std::vector<char_info> input{
        char_info{u'A', 0x1F},
        char_info{u'B', 0x2A},
        char_info{u'C', 0x07},
    };
    check(buffer.write_char_info(rect{0, 0, 2, 0}, input) == 3, "write char_info count");
    check(buffer.at(0, 0).primary() == u'A', "write char_info char");
    check(buffer.has_damage(), "write marks damage");
    buffer.clear_damage();
    check(!buffer.has_damage(), "clear damage flag");
    check(buffer.attr_at(coord{0, 0}) == 0x1F, "attr_at");
    check(buffer.char_info_at(coord{1, 0}).character == u'B', "char_info_at");

    std::vector<char_info> output(3);
    check(buffer.read_char_info(rect{0, 0, 2, 0}, output) == 3, "read char_info count");
    check(output[1].character == u'B', "read char_info data");
    check(output[1].attributes == 0x2A, "read char_info attr");

    buffer.fill_attrs(rect{1, 0, 2, 0}, 0x5A);
    check(buffer.attr_at(coord{1, 0}) == 0x5A, "fill_attrs first");
    check(buffer.attr_at(coord{2, 0}) == 0x5A, "fill_attrs second");
    check(buffer.at(1, 0).primary() == u'B', "fill attrs preserves chars");

    buffer.fill_chars(rect{1, 0, 2, 0}, u'X');
    check(buffer.at(1, 0).primary() == u'X', "fill chars");
    check(buffer.attr_at(coord{1, 0}) == 0x5A, "fill chars preserves attrs");

    buffer.scroll_region_down(rectangle{0, 0, 10, 2}, 1, cell_style{});
    check(buffer.at(0, 0).primary() == u' ', "scroll down blank top");

    const std::vector<std::uint16_t> attrs{0x11, 0x22};
    check(buffer.write_attrs(coord{0, 1}, attrs) == 2, "write attrs");
    check(buffer.attr_at(coord{0, 1}) == 0x11, "write attrs first");
    check(buffer.attr_at(coord{1, 1}) == 0x22, "write attrs second");

    (void)buffer.write_scalar(0, 2, u'Q', style0, cell_width::narrow);
    check(buffer.write_chars(coord{0, 2}, u"RS") == 2, "write chars");
    check(buffer.at(0, 2).primary() == u'R' && buffer.at(1, 2).primary() == u'S', "write chars data");
    check(buffer.write_chars(coord{2, 2}, u"\U0001F600") == 2, "write surrogate chars");
    check(buffer.at(2, 2).is_wide() && buffer.at(3, 2).is_continuation(), "write surrogate cells");

    const std::vector<char_info> wide{
        char_info{u'\u4E2D', corehost::interop::win32::lvb_leading_byte},
        char_info{u' ', corehost::interop::win32::lvb_trailing_byte},
    };
    check(buffer.write_char_info(rect{4, 0, 5, 0}, wide) == 2, "write wide char_info");
    check(buffer.at(4, 0).is_wide(), "wide head");
    check(buffer.at(5, 0).is_continuation(), "wide tail");

    std::vector<char16_t> chars(4);
    check(buffer.read_chars(coord{0, 2}, chars) >= 2, "read chars");
    check(chars[0] == u'R' && chars[1] == u'S', "read chars data");

    std::vector<corehost::interop::win32::char_info> abi_in(2);
    abi_in[0].char_data.unicode_char = u'K';
    abi_in[0].attributes = 0x3B;
    abi_in[1].char_data.unicode_char = u'L';
    abi_in[1].attributes = 0x3C;
    check(buffer.write_char_info_abi(rect{6, 1, 7, 1}, abi_in) == 2, "write ABI char_info");
    std::vector<corehost::interop::win32::char_info> abi_out(2);
    check(buffer.read_char_info_abi(rect{6, 1, 7, 1}, abi_out) == 2, "read ABI char_info");
    check(abi_out[0].char_data.unicode_char == u'K' && abi_out[1].attributes == 0x3C, "ABI char_info data");
}

void test_text_writer()
{
    using namespace corehost::screen;

    screen_buffer buffer(10, 3);
    coord cursor{0, 0};
    const auto result = buffer.write_text_row(cursor, u"AB", {}, style1);
    check(result.consumed == 2 && !result.row_end, "text writer ascii");
    check(cursor == coord{2, 0}, "text writer cursor");
    check(buffer.line_text(0).starts_with(u"AB"), "text writer text");

    coord wide_cursor{0, 1};
    const auto wide_result = buffer.write_text_row(wide_cursor, u"\u4E2D", {}, style1);
    check(wide_result.consumed == 1, "text writer wide consumed");
    check(wide_cursor == coord{2, 1}, "text writer wide cursor");
    check(buffer.at(0, 1).is_wide() && buffer.at(1, 1).is_continuation(), "text writer wide cells");

    coord glyph_cursor{0, 2};
    check(buffer.write_glyph(glyph_cursor, u"e\u0301", 1, style1), "write glyph cluster");
    check(buffer.glyph_at(0, 2) == u"e\u0301", "glyph cluster data");
    check(glyph_width<text_measurement::console>(u"e\u0301") == 1, "cluster width");

    screen_buffer grapheme_buffer(10, 2);
    coord grapheme_cursor{0, 0};
    const auto grapheme_result =
        grapheme_buffer.write_text_row<text_measurement::graphemes>(grapheme_cursor, u"e\u0301", {}, style1, true);
    check(grapheme_result.consumed == 2, "grapheme writer consumed");
    check(grapheme_cursor.x == 1, "grapheme writer cursor");
    check(grapheme_buffer.glyph_at(0, 0) == u"e\u0301", "grapheme writer cluster");

    check(codepoint_width<text_measurement::console>(U'\u0301') == 0, "combining width");
    check(codepoint_width<text_measurement::console>(U'\u4E2D') == 2, "wide width");
    check(codepoint_width<text_measurement::console>(U'A') == 1, "ascii width");

    static_assert(static_cast<int>(text_measurement::console) == 0);
    static_assert(static_cast<int>(text_measurement::graphemes) == 1);
    static_assert(static_cast<int>(text_measurement::wcswidth) == 2);
    check(codepoint_width<text_measurement::wcswidth>(U'\u4E2D') == 2, "wcswidth wide");
    check(char_width_for_mode<text_measurement::console>(U'\u4E2D') == 2, "console char width");
    check(text_width<text_measurement::console>(u"\u4E2D"
                                                "A") == 3,
          "console text width");
    check(text_width<text_measurement::wcswidth>(u"\u4E2D"
                                                 "A") == 3,
          "wcswidth text width");
    int clusters = 0;
    int cluster_widths = 0;
    for (const auto cluster : grapheme_range<text_measurement::graphemes>{u"e\u0301"
                                                                          u"A"})
    {
        ++clusters;
        cluster_widths += cluster.width;
        check(!cluster.text.empty(), "grapheme iterator span");
    }
    check(clusters == 2 && cluster_widths == 2, "grapheme iterator output");

    int console_cluster_width = 0;
    for (const auto cluster : grapheme_range<text_measurement::console>{u"A\u4E2D"})
    {
        console_cluster_width += cluster.width;
    }
    check(console_cluster_width == 3, "console grapheme width mode");

    check(text_width<text_measurement::graphemes>(u"A\u0301") == 1, "grapheme combining width");
    check(text_width<text_measurement::graphemes>(u"\u2764\uFE0F") == 2, "grapheme emoji width");

    check(console_text_width(u"\u4E2D"
                             "A") == 3,
          "console concrete width");
    check(grapheme_text_width(u"e\u0301"
                              "A",
                              false) == 2,
          "grapheme concrete width");
    check(wcswidth_text_width(u"e\u0301"
                              "A",
                              false) == 2,
          "wcswidth concrete width");
    check(console_glyph_width(u"e\u0301") == 1, "console concrete glyph");
    check(grapheme_glyph_width(u"e\u0301", false) == 1, "grapheme concrete glyph");
    check(wcswidth_glyph_width(u"e\u0301", false) == 1, "wcswidth concrete glyph");
    check(text_width_for(text_measurement::graphemes, u"e\u0301", false) == 1,
          "runtime grapheme measurement");
    check(text_width_for(text_measurement::wcswidth, u"\u4E2D", false) == 2,
          "runtime wcswidth measurement");
    check(codepoint_width_for(text_measurement::console, U'\u0301') == 0,
          "runtime console codepoint width");

    constexpr char16_t family_emoji[] = u"\U0001F468\u200D\U0001F469\u200D\U0001F467";
    screen_buffer console_cluster_buffer(10, 1);
    coord console_cluster_cursor{0, 0};
    const auto console_cluster_result = console_cluster_buffer.write_console_text_row(
        console_cluster_cursor, family_emoji, {}, style1, true);
    check(console_cluster_result.consumed == std::size(family_emoji) - 1,
          "console cluster consumed");
    check(console_cluster_cursor.x ==
              console_text_width(std::u16string_view{family_emoji, std::size(family_emoji) - 1}),
          "console cluster width consistency");

    screen_buffer runtime_buffer(8, 2);
    coord runtime_cursor{0, 0};
    const auto runtime_result = runtime_buffer.write_text_row_for(
        text_measurement::graphemes, false, runtime_cursor, u"e\u0301", {}, style1, true);
    check(runtime_result.consumed == 2 && runtime_cursor.x == 1, "runtime grapheme writer");
    check(runtime_buffer.glyph_at(0, 0) == u"e\u0301", "runtime grapheme cluster");

    runtime_cursor = {};
    const auto wide_runtime_result = runtime_buffer.write_text_row_for(
        text_measurement::wcswidth, false, runtime_cursor, u"\u4E2D", {}, style1, true);
    check(wide_runtime_result.consumed == 1 && runtime_cursor.x == 2, "runtime wcswidth writer");
}

void test_copy_scroll()
{
    using namespace corehost::screen;

    screen_buffer buffer(6, 4);
    (void)buffer.write_scalar(0, 0, u'A', style0);
    (void)buffer.write_scalar(1, 0, u'B', style0);
    buffer.copy_rect(rectangle{0, 0, 2, 1}, rectangle{0, 0, 6, 4}, true, coord{0, 1});
    check(buffer.at(0, 1).primary() == u'A' && buffer.at(1, 1).primary() == u'B', "copy rect");

    buffer.scroll_rect(rectangle{0, 1, 2, 1}, rectangle{0, 1, 2, 2}, true, coord{0, 2}, u'.', 0x07);
    check(buffer.at(0, 2).primary() == u'A' && buffer.at(1, 2).primary() == u'B', "scroll destination");
    check(buffer.at(0, 1).primary() == u'.', "scroll fill source");
}

void test_console_selection()
{
    using namespace corehost::screen;

    screen_buffer buffer(8, 3);
    (void)buffer.write_scalar(0, 0, u'A', style0);
    (void)buffer.write_scalar(1, 0, u'B', style0);
    (void)buffer.write_scalar(0, 1, u'C', style0);
    (void)buffer.write_scalar(1, 1, u'D', style0);

    console_state state(8, 3);
    state.set_selection(0, 0, 1, 1);
    check(state.selected_text(buffer) == u"AB\nCD", "block selection copy");
    state.set_selection(1, 1, 0, 1, selection_mode::line);
    check(state.selected_text(buffer) == u"CD", "line selection copy");
    check(state.is_selected(7, 1), "line selection contains full row");

    state.set_search_matches({rectangle{0, 0, 2, 1}});
    check(state.is_search_match(1, 0), "search match");
    check(!state.is_search_match(2, 0), "search mismatch");

    state.input_mode = 0x0007;
    state.output_mode = 0x0003;
    state.title = u"corehost";
    state.color_table[1] = 0x123456;
    check(state.input_mode == 0x0007 && state.title == u"corehost", "console state fields");

    state.set_cursor(2, 1);
    state.save_cursor();
    state.set_cursor(0, 0);
    check(state.restore_cursor(), "restore cursor");
    check(state.cursor().x == 2 && state.cursor().y == 1, "restored cursor position");
    state.set_cursor(3, 2);
    state.save_alt_cursor();
    state.set_cursor(0, 0);
    check(state.restore_alt_cursor(), "restore alt cursor");
    check(state.cursor().x == 3 && state.cursor().y == 2, "restored alt cursor position");
}

void test_alt_history()
{
    using namespace corehost::screen;

    screen_buffer buffer(4, 2, 2);
    (void)buffer.write_scalar(0, 0, u'M', style0);
    buffer.set_alternate(true);
    check(buffer.alternate_active(), "alternate active");
    check(buffer.at(0, 0).is_blank(), "alternate starts blank");
    (void)buffer.write_scalar(0, 0, u'A', style0);
    buffer.set_alternate(false);
    check(buffer.at(0, 0).primary() == u'M', "main preserved");

    buffer.set_flags(0, line_flag_ops::raw(line_flag::marked));
    buffer.scroll_up(1);
    check(buffer.history_size() == 1, "history size");
    check(buffer.history_flags(0) == line_flag_ops::raw(line_flag::marked), "history flags");
}

} // namespace

int main()
{
    test_interop_abi();
    test_geometry();
    test_screen_viewport();
    test_char_info_and_attrs();
    test_text_writer();
    test_copy_scroll();
    test_console_selection();
    test_alt_history();

    if (failures != 0)
    {
        std::fprintf(stderr, "%d failures\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("libscreen feature tests passed\n");
    return EXIT_SUCCESS;
}
