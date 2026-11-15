#include "console_state.hpp"
#include "screen_buffer.hpp"

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <span>
#include <string>
#include <vector>

namespace
{

using namespace corehost::screen;

constexpr cell_style style0{0};
constexpr cell_style style1{1};
constexpr cell_style style3{3};
constexpr cell_style style4{4};
constexpr cell_style style7{7};
constexpr cell_hyperlink link9{9};
constexpr cell_hyperlink link11{11};
constexpr cell_hyperlink link12{12};
constexpr cell_flags flags12{0x12};
constexpr cell_flags flag1{1};

void check(bool condition, const char *message)
{
    if (!condition)
    {
        std::cerr << "FAILED: " << message << '\n';
        assert(condition);
        std::abort();
    }
}

void test_cell()
{
    static_assert(sizeof(cell) == 16);
    static_assert(alignof(cell) == alignof(std::int32_t));
    static_assert(std::is_trivially_copyable_v<cell>);

    const cell blank = cell::blank(style7, link9);
    check(blank.is_blank(), "blank cell");
    check(blank.primary() == u' ', "blank primary");
    check(blank.style() == style7, "blank style");
    check(blank.hyperlink() == link9, "blank hyperlink");
    check(blank.type() == cell_type::blank, "blank type");

    const cell scalar = cell::scalar(u'A', style3, cell_width::narrow, link11, flags12);
    check(!scalar.is_blank(), "scalar not blank");
    check(scalar.primary() == u'A', "scalar primary");
    check(scalar.type() == cell_type::scalar, "scalar type");
    check(scalar.style() == style3, "scalar style");
    check(scalar.hyperlink() == link11, "scalar hyperlink");
    check(!scalar.has(cell_flags::protected_cell), "scalar flags");

    const cell replacement = cell::scalar(0xD83D, style0);
    check(replacement.primary() == u'\uFFFD', "isolated surrogate becomes replacement");

    constexpr std::uint32_t offset = 0x123456;
    const cell complex = cell::complex(offset, 2, style4, cell_width::wide, link12, flag1);
    check(complex.has_cluster(), "complex has cluster");
    check(complex.primary() == u'\uFFFD', "complex primary is replacement");
    check(complex.offset() == offset, "complex offset");
    check(complex.length() == 2, "complex length");
    check(complex.width() == cell_width::wide, "complex width");
    check(complex.hyperlink() == link12, "complex hyperlink");
    check(complex.has(cell_flags::protected_cell), "complex flag");

    const cell surrogate = cell::surrogate(1, 2);
    const cell grapheme = cell::grapheme(1, 2);
    check(surrogate.type() == cell_type::complex, "surrogate alias");
    check(grapheme.type() == cell_type::complex, "grapheme alias");
    check(surrogate.type() == grapheme.type(), "type aliases equal");

    const cell image = cell::image(17, style4, cell_width::wide, link12, flag1);
    check(image.has_image(), "image cell");
    check(image.image_fragment_id() == 17, "image fragment id");
    check(image.type() == cell_type::image, "image type");
    check(image.width() == cell_width::wide, "image width");
    check(image.has_image(), "image predicate");
}

void test_grapheme_store()
{
    grapheme_store store;
    check(store.size() == 0, "store initial size");
    check(store.can_append(1), "store can append 1");
    check(!store.can_append(0), "store cannot append 0");

    const std::vector<char16_t> cluster{0xD83D, 0xDE00, 0x200D, 0xD83D, 0xDCBB};
    const auto ref = store.append(cluster);
    check(ref.offset == 0, "first store offset");
    check(ref.length == 5, "first store length");
    const auto view = store.view(ref);
    check(view.size() == cluster.size(), "store view size");
    check(std::equal(view.begin(), view.end(), cluster.begin(), cluster.end()), "store view data");

    const std::vector<char16_t> cluster2{0x0041};
    const auto ref2 = store.append(cluster2);
    check(ref2.offset == 5, "second store offset");
    check(ref2.length == 1, "second store length");
    check(store.size() == 6, "store total size");
    check(store.view(ref2)[0] == u'A', "second store data");
    check(!store.can_append(256), "store respects 8-bit cluster max");
}

void test_styles_and_hyperlinks()
{
    style_table styles;
    check(styles.size() == 1, "default style exists");
    check(styles.at(style0) == text_style{}, "default style is zero");

    text_style style;
    style.foreground = color{color_kind::rgb, 0x112233};
    style.attributes = 0x0042;
    style.legacy_attributes = 0x0007;
    const auto first = styles.intern(style);
    const auto second = styles.intern(style);
    check(first == second, "style interning");
    check(first != style0, "interned style is not default");
    check(styles.at(first) == style, "stored style");
    check(styles.size() == 2, "style table size");

    hyperlink_table links;
    check(links.size() == 1, "default hyperlink exists");
    const auto link = links.intern(u"https://example.test/", u"id-1");
    check(link == links.intern(u"https://example.test/", u"id-1"), "hyperlink interning");
    check(links.at(link).id == u"id-1", "hyperlink id");
    check(links.size() == 2, "hyperlink table size");
}

void test_line_flags_and_images()
{
    screen_buffer screen(4, 3, 4);
    screen.add_flags(0, line_flag::wrapped);
    screen.add_flags(0, line_flag::marked);
    check(screen.has_flag(0, line_flag::wrapped), "wrapped flag");
    check(screen.has_flag(0, line_flag::marked), "marked flag");
    check(screen.has_flag(0, line_flag::user_mark) == false, "unset flag");

    screen.remove_flags(0, line_flag::marked);
    check(!screen.has_flag(0, line_flag::marked), "remove flag");
    screen.set_flags(0, line_flag_ops::raw(line_flag::double_width));
    check(screen.is_double_width(0), "line rendition flag");
    screen.set_flags(
        0, line_flag_ops::combine(line_flag_ops::raw(line_flag::marked), line_flag_ops::raw(line_flag::output_start)));
    check(screen.has_flag(0, line_flag::output_start), "output start flag");
    screen.set_command_exit_code(0, 7);
    check(screen.command_exit_code(0).value_or(-1) == 7, "command exit code");

    screen.scroll_up(1);
    check(screen.history_flags(0) == line_flag_ops::combine(line_flag_ops::raw(line_flag::marked),
                                                            line_flag_ops::raw(line_flag::output_start)),
          "history keeps line flags");
    check(screen.history_command_exit_code(0).value_or(-1) == 7, "history keeps command exit code");

    const image_fragment fragment{42, 7, 0, 0, 16, 16, image_layer::replace};
    const auto fragment_id = screen.images().intern(fragment);
    check(fragment_id != 0, "image fragment interned");
    check(screen.images().intern(fragment) == fragment_id, "image fragment dedup");
    check(!screen.write_image(0, 1, 0), "invalid image id");
    check(screen.write_image(0, 1, fragment_id), "image write");
    const auto image_cell = screen.at(0, 1);
    check(image_cell.has_image(), "image cell type");
    const auto *stored_image = screen.image_of(image_cell);
    check(stored_image != nullptr, "image lookup");
    check(stored_image->image_id == 42, "image id");
    check(stored_image->layer == image_layer::replace, "image layer");
}

void test_console_state()
{
    console_state state(20, 5);
    state.set_cursor(3, 2);
    check(state.cursor().x == 3 && state.cursor().y == 2, "console cursor");
    state.move_cursor(100, -100);
    check(state.cursor().x == 19 && state.cursor().y == 0, "console cursor clamp");

    check(state.tabs()[0], "tab 0");
    check(state.tabs()[8], "tab 8");
    check(state.next_tab_stop(0) == 8, "next tab");
    check(state.previous_tab_stop(8) == 0, "previous tab");
    state.tabs().set(4, true);
    check(state.previous_tab_stop(8) == 4, "custom tab");

    state.set_scroll_region(1, 3);
    check(state.in_scroll_region(1) && state.in_scroll_region(3), "scroll region");
    check(!state.in_scroll_region(0), "outside scroll region");
    state.reset_scroll_region();
    check(state.scroll_top() == 0 && state.scroll_bottom() == 4, "reset scroll region");

    state.set_selection(2, 1, 5, 3);
    check(state.has_selection(), "selection active");
    check(state.is_selected(4, 2), "selection contains");
    check(!state.is_selected(1, 2), "selection excludes");
    state.clear_selection();
    check(!state.has_selection(), "selection cleared");

    state.set_search_matches({rectangle{1, 1, 3, 1}, rectangle{8, 2, 2, 2}});
    check(state.is_search_match(2, 1), "search match");
    check(!state.is_search_match(4, 1), "search miss");
    state.resize(10, 4);
    check(state.width() == 10 && state.height() == 4, "console resize");
    check(state.search_matches().empty(), "resize clears search");
    check(!state.has_selection(), "resize clears selection");
}
void test_screen_buffer()
{
    screen_buffer screen(4, 3, 2);
    check(screen.width() == 4, "screen width");
    check(screen.height() == 3, "screen height");
    check(!screen.alternate_active(), "main active");

    (void)screen.write_scalar(0, 0, u'A', style0);
    check(screen.at(0, 0).primary() == u'A', "scalar write");
    check(screen.at(0, 0).style() == style0, "scalar style");
    check(screen.write_codepoint(2, 0, U'\U0001F600', style1), "codepoint write");
    check(screen.glyph_at(2, 0) == u"\xD83D\xDE00", "codepoint glyph");
    check(screen.codepoint_at(2, 0) == U'\U0001F600', "codepoint decode");
    check(screen.line_text(0).find(u"\xD83D\xDE00") != std::u16string::npos, "line text codepoint");
    check(screen.write_legacy_scalar(0, 2, u'Q', 0x1F), "legacy scalar");
    check(screen.legacy_at(0, 2).attributes == 0x1F, "legacy attributes");
    const auto legacy_color_style = screen.styles().at(screen.style_for_attributes(0x1C));
    check(legacy_color_style.foreground.kind == color_kind::indexed &&
              legacy_color_style.foreground.value == 9,
          "legacy foreground color");
    check(legacy_color_style.background.kind == color_kind::indexed &&
              legacy_color_style.background.value == 4,
          "legacy background color");
    const auto default_style = screen.styles().at(screen.style_for_attributes(0x07));
    check(default_style.foreground.kind == color_kind::default_color &&
              default_style.background.kind == color_kind::default_color,
          "legacy default attributes use terminal defaults");
    const auto red_foreground_style = screen.styles().at(screen.style_for_attributes(0x0C));
    check(red_foreground_style.foreground.kind == color_kind::indexed &&
              red_foreground_style.foreground.value == 9 &&
              red_foreground_style.background.kind == color_kind::default_color,
          "legacy red foreground keeps default background");
    check(screen.damage_begin(0) <= 0 && screen.damage_end(0) >= 1, "scalar damage");
    screen.clear_damage();
    check(screen.damage_begin(0) == screen.damage_end(0), "damage cleared");

    const std::vector<char16_t> cluster{0xD83D, 0xDE00};
    check(screen.write_cluster(1, 0, cluster, style1), "cluster write");
    const auto cluster_cell = screen.at(1, 0);
    check(cluster_cell.has_cluster(), "cluster cell type");
    check(cluster_cell.primary() == u'\uFFFD', "cluster primary");
    check(cluster_cell.style() == style1, "cluster style");
    const auto cluster_view = screen.cluster_of(cluster_cell);
    check(cluster_view.size() == 2, "cluster view size");
    check(cluster_view[0] == 0xD83D && cluster_view[1] == 0xDE00, "cluster data");
    check(screen.rebuild_graphemes(), "grapheme rebuild");
    const auto rebuilt_cluster = screen.cluster_of(screen.at(1, 0));
    check(rebuilt_cluster.size() == 2 && rebuilt_cluster[0] == 0xD83D, "cluster survives rebuild");

    check(screen.write_scalar(2, 1, u'\u4E2D', style0, cell_width::wide), "wide write");
    check(screen.at(2, 1).is_wide(), "wide head");
    check(screen.at(3, 1).is_continuation(), "wide tail");
    check(!screen.write_scalar(3, 1, u'X', style0, cell_width::wide), "wide write without room");
    check(screen.row(1).used_columns == 4, "wide write marks used columns");

    const cell wide = cell::scalar(u'Y', style0, cell_width::wide);
    check(screen.write_cell(0, 1, wide), "direct wide write");
    check(screen.at(1, 1).is_continuation(), "direct wide tail");
    (void)screen.write_scalar(0, 1, u'Z', style0);
    check(screen.at(0, 1).primary() == u'Z', "overwrite wide head");
    check(screen.at(1, 1).is_blank(), "overwrite wide clears tail");

    screen.fill_rect(rectangle{0, 2, 4, 1}, cell::blank(style3));
    check(screen.at(0, 2).style() == style3, "fill rectangle");

    screen.clear_damage();
    screen.scroll_up(1);
    check(screen.history_size() == 1, "scroll records history");
    check(screen.history_line(0).size() == 4, "history line width");
    check(screen.at(0, 0).primary() == u'Z', "scroll moves row 1 to row 0");
    check(screen.history_line(0)[0].primary() == u'A', "scroll history keeps old row 0");
    check(screen.at(0, 2).is_blank(), "scroll bottom line");
    check(screen.damage_begin(0) <= 0 && screen.damage_end(0) >= 4, "scroll damage");

    screen.clear_damage();
    (void)screen.write_scalar(0, 0, u'M', style0);
    screen.set_alternate(true);
    check(screen.alternate_active(), "alternate active");
    check(screen.at(0, 0).is_blank(), "alternate starts blank");
    (void)screen.write_scalar(0, 0, u'Z', style0);
    check(screen.at(0, 0).primary() == u'Z', "alternate write");
    screen.set_alternate(false);
    check(screen.at(0, 0).primary() == u'M', "main preserved after alt");

    screen.resize(6, 2);
    check(screen.width() == 6 && screen.height() == 2, "resize dimensions");
    check(screen.at(0, 0).primary() == u'M', "resize preserves overlap");
    check(screen.at(5, 0).is_blank(), "resize new cells are blank");
    check(screen.history_size() == 0, "resize clears history");
    check(screen.damage_begin(0) == 0 && screen.damage_end(0) == 6, "resize marks damage");

    screen.clear(style4);
    check(screen.at(0, 0).style() == style4, "clear style");
    check(screen.at(5, 1).style() == style4, "clear all cells");
}

} // namespace

int main()
{
    test_cell();
    test_grapheme_store();
    test_styles_and_hyperlinks();
    test_line_flags_and_images();
    test_console_state();
    test_screen_buffer();
    std::cout << "libscreen tests passed\n";
    return 0;
}
