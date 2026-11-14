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
    static_assert(alignof(cell) == alignof(std::uint64_t));
    static_assert(std::is_trivially_copyable_v<cell>);

    const cell blank = cell::blank(7, 9);
    check(blank.is_blank(), "blank cell");
    check(blank.primary() == u' ', "blank primary");
    check(blank.style() == 7, "blank style");
    check(blank.hyperlink() == 9, "blank hyperlink");

    const cell scalar = cell::scalar(u'A', 3, cell_width::narrow, 11, 0x0012);
    check(!scalar.is_blank(), "scalar not blank");
    check(scalar.primary() == u'A', "scalar primary");
    check(scalar.content() == cell_content::scalar, "scalar content");
    check(scalar.style() == 3, "scalar style");
    check(scalar.hyperlink() == 11, "scalar hyperlink");
    check(scalar.has(cell_flag::protected_cell) == false, "scalar flags");

    const cell replacement = cell::scalar(0xD83D, 0);
    check(replacement.primary() == u'\uFFFD', "isolated surrogate becomes replacement");

    constexpr cluster_offset offset = 0x123456;
    const cell complex = cell::complex(offset, 2, 4, cell_width::wide, 12, 0x0001);
    check(complex.has_cluster(), "complex has cluster");
    check(complex.primary() == u'\uFFFD', "complex primary is replacement");
    check(complex.offset() == offset, "complex offset");
    check(complex.length() == 2, "complex length");
    check(complex.width() == cell_width::wide, "complex width");
    check(complex.hyperlink() == 12, "complex hyperlink");
    check(complex.has(cell_flag::protected_cell), "complex flag");

    const cell surrogate = cell::surrogate(1, 2);
    const cell grapheme = cell::grapheme(1, 2);
    check(surrogate.content() == cell_content::complex, "surrogate alias");
    check(grapheme.content() == cell_content::complex, "grapheme alias");
    check(surrogate.content() == grapheme.content(), "content aliases equal");
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
    check(store.view(grapheme_store::ref{2, 100}).empty(), "invalid view is empty");
    check(!store.can_append(256), "store respects 8-bit cluster max");
}

void test_styles_and_hyperlinks()
{
    style_table styles;
    check(styles.size() == 1, "default style exists");
    check(styles.at(0) == text_style{}, "default style is zero");

    text_style style;
    style.foreground = color{color_kind::rgb, 0x112233};
    style.attributes = 0x0042;
    style.legacy_attributes = 0x0007;
    const auto first = styles.intern(style);
    const auto second = styles.intern(style);
    check(first == second, "style interning");
    check(first != 0, "interned style is not default");
    check(styles.at(first) == style, "stored style");
    check(styles.size() == 2, "style table size");

    hyperlink_table links;
    check(links.size() == 1, "default hyperlink exists");
    const auto link = links.intern(L"https://example.test/", L"id-1");
    check(link == links.intern(L"https://example.test/", L"id-1"), "hyperlink interning");
    check(links.at(link).id == L"id-1", "hyperlink id");
    check(links.size() == 2, "hyperlink table size");
}

void test_screen_buffer()
{
    screen_buffer screen(4, 3, 2);
    check(screen.width() == 4, "screen width");
    check(screen.height() == 3, "screen height");
    check(screen.alternate_active() == false, "main active");

    (void)screen.write_scalar(0, 0, u'A', 0);
    check(screen.at(0, 0).primary() == u'A', "scalar write");
    check(screen.at(0, 0).style() == 0, "scalar style");
    check(screen.damage_begin(0) <= 0 && screen.damage_end(0) >= 1, "scalar damage");
    screen.clear_damage();
    check(screen.damage_begin(0) == screen.damage_end(0), "damage cleared");

    const std::vector<char16_t> cluster{0xD83D, 0xDE00};
    check(screen.write_cluster(1, 0, cluster, 1), "cluster write");
    const auto cluster_cell = screen.at(1, 0);
    check(cluster_cell.has_cluster(), "cluster cell content");
    check(cluster_cell.primary() == u'\uFFFD', "cluster primary");
    check(cluster_cell.style() == 1, "cluster style");
    const auto cluster_view = screen.cluster_of(cluster_cell);
    check(cluster_view.size() == 2, "cluster view size");
    check(cluster_view[0] == 0xD83D && cluster_view[1] == 0xDE00, "cluster data");
    check(screen.rebuild_graphemes(), "grapheme rebuild");
    const auto rebuilt_cluster = screen.cluster_of(screen.at(1, 0));
    check(rebuilt_cluster.size() == 2 && rebuilt_cluster[0] == 0xD83D, "cluster survives rebuild");

    check(screen.write_scalar(2, 1, u'\u4E2D', 0, cell_width::wide), "wide write");
    check(screen.at(2, 1).is_wide(), "wide head");
    check(screen.at(3, 1).is_continuation(), "wide tail");
    check(!screen.write_scalar(3, 1, u'X', 0, cell_width::wide), "wide write without room");
    check(screen.row(1).used_columns == 4, "wide write marks used columns");

    const cell wide = cell::scalar(u'Y', 0, cell_width::wide);
    check(screen.write_cell(0, 1, wide), "direct wide write");
    check(screen.at(1, 1).is_continuation(), "direct wide tail");
    (void)screen.write_scalar(0, 1, u'Z');
    check(screen.at(0, 1).primary() == u'Z', "overwrite wide head");
    check(screen.at(1, 1).is_blank(), "overwrite wide clears tail");

    screen.fill_rect(rectangle{0, 2, 4, 1}, cell::blank(3));
    check(screen.at(0, 2).style() == 3, "fill rectangle");

    screen.clear_damage();
    screen.scroll_up(1);
    check(screen.history_size() == 1, "scroll records history");
    check(screen.history_line(0).size() == 4, "history line width");
    check(screen.at(0, 0).primary() == u'Z', "scroll moves row 1 to row 0");
    check(screen.history_line(0)[0].primary() == u'A', "scroll history keeps old row 0");
    check(screen.at(0, 2).is_blank(), "scroll bottom line");
    check(screen.damage_begin(0) <= 0 && screen.damage_end(0) >= 4, "scroll damage");

    screen.clear_damage();
    (void)screen.write_scalar(0, 0, u'M');
    screen.set_alternate(true);
    check(screen.alternate_active(), "alternate active");
    check(screen.at(0, 0).is_blank(), "alternate starts blank");
    (void)screen.write_scalar(0, 0, u'Z');
    check(screen.at(0, 0).primary() == u'Z', "alternate write");
    screen.set_alternate(false);
    check(screen.at(0, 0).primary() == u'M', "main preserved after alt");

    screen.resize(6, 2);
    check(screen.width() == 6 && screen.height() == 2, "resize dimensions");
    check(screen.at(0, 0).primary() == u'M', "resize preserves overlap");
    check(screen.at(5, 0).is_blank(), "resize new cells are blank");
    check(screen.history_size() == 0, "resize clears history");
    check(screen.damage_begin(0) == 0 && screen.damage_end(0) == 6, "resize marks damage");

    screen.clear(4);
    check(screen.at(0, 0).style() == 4, "clear style");
    check(screen.at(5, 1).style() == 4, "clear all cells");
}

} // namespace

int main()
{
    test_cell();
    test_grapheme_store();
    test_styles_and_hyperlinks();
    test_screen_buffer();
    std::cout << "libscreen tests passed\n";
    return 0;
}
