// tests/test_vt_screen_selftest.cpp
// VT 屏幕模型自测：用已知序列断言单元格/文本/光标/属性。
// 模型自身出错会让依赖它的 conpty 断言失去意义，因此先把它测好。
#include "lib/vt_screen.hpp"

#include <cstdio>
#include <string>

using corehost::test::screen_cell;
using corehost::test::vt_screen;

namespace
{

int failures = 0;

void check(bool condition, const char *message)
{
    if (!condition)
    {
        std::printf("  FAILED: %s\n", message);
        ++failures;
    }
}

bool text_rows()
{
    vt_screen screen{20, 5};
    screen.feed("hello\r\nworld");
    check(screen.text(0) == "hello", "row 0 text");
    check(screen.text(1) == "world", "row 1 text");
    check(screen.cursor().X == 5 && screen.cursor().Y == 1, "cursor after CRLF");
    return true;
}

bool wrapping()
{
    vt_screen screen{5, 3};
    screen.feed("abcdef");
    check(screen.text(0) == "abcde", "wrapped row 0");
    check(screen.text(1) == "f", "wrapped row 1");
    return true;
}

bool carriage_return_and_backspace()
{
    vt_screen screen{10, 2};
    screen.feed("ab\rX");
    check(screen.text(0) == "Xb", "CR overwrite");
    screen.feed("\bY");
    // CR 后写入 X → 光标 x=1；退格回到 x=0，Y 覆盖 X。
    check(screen.text(0) == "Yb", "backspace moves left");
    return true;
}

bool sgr_attributes()
{
    vt_screen screen{10, 2};
    screen.feed("\x1b[1;31;44mR");
    const screen_cell &cell = screen.cell(0, 0);
    check(cell.ch == U'R', "SGR char");
    check(cell.fg == 1, "SGR foreground");
    check(cell.bg == 4, "SGR background");
    check(cell.bold, "SGR bold");
    screen.feed("\x1b[0mN");
    check(screen.cell(1, 0).fg == 7 && !screen.cell(1, 0).bold, "SGR reset");
    return true;
}

bool cursor_positioning()
{
    vt_screen screen{10, 5};
    screen.feed("\x1b[3;4HX");
    check(screen.cell(3, 2).ch == U'X', "CUP position");
    screen.feed("\x1b[2A\x1b[2DY");
    // CUP 到 (3,2) 后写入 X → 光标 x=4；上移 2、左移 2 后落在 (2,0)。
    check(screen.cell(2, 0).ch == U'Y', "CUU/CUB");
    return true;
}

bool erase_line_and_display()
{
    vt_screen screen{10, 3};
    screen.feed("abc\r\x1b[K");
    check(screen.text(0).empty(), "EL 0 clears to end");
    screen.feed("\x1b[2J");
    check(screen.text(0).empty() && screen.text(1).empty(), "ED 2 clears screen");
    return true;
}

bool alternate_buffer()
{
    vt_screen screen{10, 3};
    screen.feed("main");
    screen.feed("\x1b[?1049h");
    check(screen.text(0).empty(), "alt buffer starts blank");
    screen.feed("alt");
    check(screen.text(0) == "alt", "alt buffer text");
    screen.feed("\x1b[?1049l");
    check(screen.text(0) == "main", "main buffer restored");
    return true;
}

} // namespace

int main()
{
    std::printf("=== VT Screen Model ===\n");
    struct
    {
        bool (*test)();
        const char *name;
    } tests[] = {
        {text_rows, "text rows"},
        {wrapping, "wrapping"},
        {carriage_return_and_backspace, "CR/backspace"},
        {sgr_attributes, "SGR attributes"},
        {cursor_positioning, "cursor positioning"},
        {erase_line_and_display, "erase line/display"},
        {alternate_buffer, "alternate buffer"},
    };

    for (const auto &item : tests)
    {
        const int before = failures;
        item.test();
        std::printf("  %s ... %s\n", item.name, failures == before ? "PASSED" : "FAILED");
    }

    std::printf("  %s\n", failures == 0 ? "PASSED" : "FAILED");
    return failures == 0 ? 0 : 1;
}
