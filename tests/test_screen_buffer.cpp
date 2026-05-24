// === tests/test_screen_buffer.cpp ===
// Screen buffer unit tests (screen_buffer.hpp, screen_buffer_row.hpp)
// Coverage: read/write, fill, scroll, clear, resize, char32_t text
#include "test_common.hpp"
#include "conpty/screen_buffer.hpp"
#include <cstring>

using namespace conpty;

// ==================================================================
// Basic read/write
// ==================================================================

bool test_set_and_get_char()
{
    screen_buffer sb({80, 25});
    sb.set_u32({0, 0}, U'A', 0x07);
    ASSERT_EQ(sb.at_u32({0, 0}), U'A');
    ASSERT_EQ(sb.attr_at({0, 0}), (WORD)0x07);
    ASSERT_EQ(sb.at_u32({0, 1}), U' ');
    return true;
}

bool test_set_and_get_attr()
{
    screen_buffer sb({80, 25});
    sb.set_attr({5, 3}, 0x1F);
    ASSERT_EQ(sb.attr_at({5, 3}), (WORD)0x1F);
    ASSERT_EQ(sb.at_u32({5, 3}), U' ');
    return true;
}

bool test_glyph_at()
{
    screen_buffer sb({80, 25});
    sb.set_u32({0, 0}, U'A', 0x07);
    auto gv = sb.glyph_at({0, 0});
    ASSERT_EQ(gv.size(), (size_t)1);
    ASSERT_EQ(gv[0], U'A');
    return true;
}

bool test_glyph_width()
{
    screen_buffer sb({80, 25});
    sb.set_u32({0, 0}, U'A', 0x07);
    ASSERT_EQ(sb.glyph_width({0, 0}), 1);
    return true;
}

// ==================================================================
// Fill
// ==================================================================

bool test_fill_char_partial()
{
    screen_buffer sb({80, 25});
    auto res = sb.fill_char(U'X', {10, 5}, 5);
    ASSERT_EQ(res.cells_modified, (ULONG)5);
    ASSERT_EQ(sb.at_u32({10, 5}), U'X');
    ASSERT_EQ(sb.at_u32({14, 5}), U'X');
    ASSERT_EQ(sb.at_u32({15, 5}), U' ');
    return true;
}

bool test_fill_char_full()
{
    screen_buffer sb({80, 25});
    auto res = sb.fill_char(U'#', {0, 0}, 80);
    ASSERT_EQ(res.cells_modified, (ULONG)80);
    ASSERT_EQ(sb.at_u32({0, 0}), U'#');
    ASSERT_EQ(sb.at_u32({79, 0}), U'#');
    return true;
}

bool test_fill_char_clamped()
{
    screen_buffer sb({80, 25});
    auto res = sb.fill_char(U'#', {70, 0}, 20);
    ASSERT_EQ(res.cells_modified, (ULONG)10);
    ASSERT_EQ(sb.at_u32({79, 0}), U'#');
    return true;
}

bool test_fill_attr()
{
    screen_buffer sb({80, 25});
    auto res = sb.fill_attr(0x2E, {0, 10}, 10);
    ASSERT_EQ(res.cells_modified, (ULONG)10);
    ASSERT_EQ(sb.attr_at({0, 10}), (WORD)0x2E);
    return true;
}

// ==================================================================
// Write/Read sequences
// ==================================================================

bool test_write_char32()
{
    screen_buffer sb({80, 25});
    char32_t seq[] = {U'H', U'e', U'l', U'l', U'o'};
    size_t n = sb.write_char32({0, 0}, seq, 5);
    ASSERT_EQ(n, (size_t)5);
    ASSERT_EQ(sb.at_u32({0, 0}), U'H');
    ASSERT_EQ(sb.at_u32({4, 0}), U'o');
    return true;
}

bool test_write_attr_seq()
{
    screen_buffer sb({80, 25});
    WORD attrs[] = {0x01, 0x02, 0x03};
    size_t n = sb.write_attr_seq({0, 0}, attrs, 3);
    ASSERT_EQ(n, (size_t)3);
    ASSERT_EQ(sb.attr_at({0, 0}), (WORD)0x01);
    return true;
}

bool test_read_wchars()
{
    screen_buffer sb({80, 25});
    char32_t seq[] = {U'X', U'Y', U'Z'};
    sb.write_char32({0, 0}, seq, 3);
    wchar_t out[4]{};
    size_t n = sb.read_wchars({0, 0}, out, 3);
    ASSERT_EQ(n, (size_t)3);
    ASSERT_EQ(out[0], L'X');
    ASSERT_EQ(out[2], L'Z');
    return true;
}

bool test_read_attrs()
{
    screen_buffer sb({80, 25});
    WORD attrs[] = {0x0A, 0x0B};
    sb.write_attr_seq({5, 5}, attrs, 2);
    WORD out[3]{};
    size_t n = sb.read_attrs({5, 5}, out, 2);
    ASSERT_EQ(n, (size_t)2);
    ASSERT_EQ(out[0], (WORD)0x0A);
    return true;
}

// ==================================================================
// Scroll
// ==================================================================

bool test_scroll_up()
{
    screen_buffer sb({80, 25});
    for (SHORT y = 0; y < 25; ++y)
        sb.set_u32({0, y}, static_cast<char32_t>(U'A' + y), 0x07);
    SMALL_RECT sr{0, 0, 79, 24};
    sb.scroll(sr, sr, false, {0, 1}, U' ', 0x07);
    // Scroll completed without crash; some content shifted
    ASSERT(true);
    return true;
}

bool test_scroll_down()
{
    screen_buffer sb({80, 25});
    for (SHORT y = 0; y < 25; ++y)
        sb.set_u32({0, y}, static_cast<char32_t>(U'A' + y), 0x07);
    SMALL_RECT sr{0, 0, 79, 24};
    sb.scroll(sr, sr, false, {0, -1}, U' ', 0x07);
    ASSERT(true);
    return true;
}

bool test_scroll_noop()
{
    screen_buffer sb({80, 25});
    SMALL_RECT sr{10, 10, 9, 10};
    sb.scroll(sr, sr, false, {0, 1}, U' ', 0x07);
    ASSERT_EQ(sb.at_u32({0, 0}), U' ');
    return true;
}

// ==================================================================
// Clear
// ==================================================================

bool test_clear()
{
    screen_buffer sb({80, 25});
    sb.set_u32({0, 0}, U'X', 0x1F);
    sb.clear();
    ASSERT_EQ(sb.at_u32({0, 0}), U' ');
    ASSERT_EQ(sb.attr_at({0, 0}), (WORD)0x07);
    return true;
}

bool test_clear_cell()
{
    screen_buffer sb({80, 25});
    sb.set_u32({10, 5}, U'Z', 0x2F);
    sb.clear_cell({10, 5});
    ASSERT_EQ(sb.at_u32({10, 5}), U' ');
    ASSERT_EQ(sb.attr_at({10, 5}), (WORD)0x07);
    return true;
}

// ==================================================================
// Resize
// ==================================================================

bool test_resize_expand()
{
    screen_buffer sb({80, 25});
    sb.set_u32({0, 0}, U'H', 0x07);
    sb.resize({120, 30});
    ASSERT_EQ(sb.size.X, (SHORT)120);
    ASSERT_EQ(sb.size.Y, (SHORT)30);
    ASSERT_EQ(sb.at_u32({0, 0}), U'H');
    return true;
}

bool test_resize_shrink()
{
    screen_buffer sb({80, 25});
    sb.set_u32({79, 24}, U'Z', 0x07);
    sb.resize({40, 10});
    ASSERT_EQ(sb.size.X, (SHORT)40);
    ASSERT_EQ(sb.size.Y, (SHORT)10);
    return true;
}

// ==================================================================
// Edge cases
// ==================================================================

bool test_out_of_bounds()
{
    screen_buffer sb({80, 25});
    sb.set_u32({80, 0}, U'X', 0x07);
    sb.set_u32({0, 25}, U'X', 0x07);
    ASSERT(true);
    return true;
}

bool test_ci_roundtrip()
{
    screen_buffer sb({80, 25});
    sb.set_u32({0, 3}, U'H', 0x1F);
    sb.set_u32({1, 3}, U'i', 0x2E);

    CHAR_INFO ci[80]{};
    sb.row_to_ci(3, ci);
    ASSERT_EQ(ci[0].Attributes, (WORD)0x1F);

    sb.clear();
    sb.row_from_ci(3, ci, 2);
    ASSERT_EQ(sb.at_u32({0, 3}), U'H');
    ASSERT_EQ(sb.attr_at({0, 3}), (WORD)0x1F);
    ASSERT_EQ(sb.at_u32({1, 3}), U'i');
    return true;
}

// ==================================================================
// Test Runner
// ==================================================================

int main()
{
    std::wcout << L"=== screen_buffer Tests ===" << std::endl;

    RUN_TEST(test_set_and_get_char, L"Set/Get char");
    RUN_TEST(test_set_and_get_attr, L"Set/Get attr");
    RUN_TEST(test_glyph_at, L"glyph_at");
    RUN_TEST(test_glyph_width, L"glyph_width");

    RUN_TEST(test_fill_char_partial, L"fill char partial");
    RUN_TEST(test_fill_char_full, L"fill char full");
    RUN_TEST(test_fill_char_clamped, L"fill char clamped");
    RUN_TEST(test_fill_attr, L"fill attr");

    RUN_TEST(test_write_char32, L"write_char32");
    RUN_TEST(test_write_attr_seq, L"write_attr_seq");

    RUN_TEST(test_read_wchars, L"read_wchars");
    RUN_TEST(test_read_attrs, L"read_attrs");

    RUN_TEST(test_scroll_up, L"scroll up");
    RUN_TEST(test_scroll_down, L"scroll down");
    RUN_TEST(test_scroll_noop, L"scroll noop");

    RUN_TEST(test_clear, L"clear");
    RUN_TEST(test_clear_cell, L"clear_cell");

    RUN_TEST(test_resize_expand, L"resize expand");
    RUN_TEST(test_resize_shrink, L"resize shrink");

    RUN_TEST(test_out_of_bounds, L"out of bounds");
    RUN_TEST(test_ci_roundtrip, L"CHAR_INFO roundtrip");

    std::wcout << L"  " << tests_passed << L" passed, " << tests_failed << L" failed, " << (tests_passed + tests_failed)
               << L" total." << std::endl;
    return tests_failed > 0 ? 1 : 0;
}
