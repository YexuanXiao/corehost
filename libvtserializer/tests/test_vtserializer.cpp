#include "buffer.hpp"
#include "control.hpp"
#include "attributes.hpp"
#include "text.hpp"
#include "viewport.hpp"

#include <algorithm>
#include <string>
#include <cstdio>
#include <cstdlib>

int main()
{
    using namespace corehost::vtserializer;

    byte_buffer buffer;
    buffer.reserve(4);
    buffer.append_text(u8"AB");
    buffer.push_back(static_cast<char8_t>(u8'C'));
    if (buffer.size() != 3 || buffer.bytes().size() != 3 || buffer.bytes()[2] != u8'C')
    {
        std::fprintf(stderr, "append failed\n");
        return EXIT_FAILURE;
    }

    buffer.clear();
    if (!buffer.empty())
    {
        std::fprintf(stderr, "clear failed\n");
        return EXIT_FAILURE;
    }
    buffer.append_text(u8"xy");
    if (buffer.bytes().size() != 2 || buffer.bytes()[0] != u8'x')
    {
        std::fprintf(stderr, "reuse failed\n");
        return EXIT_FAILURE;
    }

    buffer.clear();
    append_hyperlink_open(buffer, u8"id=x", u8"https://e");
    append_hyperlink_close(buffer);
    constexpr std::u8string_view expected = u8"\x1b]8;id=x;https://e\x07\x1b]8;;\x07";
    if (buffer.bytes().size() != expected.size() ||
        !std::equal(buffer.bytes().begin(), buffer.bytes().end(), expected.begin()))
    {
        std::fprintf(stderr, "hyperlink serialization failed\n");
        return EXIT_FAILURE;
    }

    buffer.clear();
    append_line_rendition(buffer, 3);
    constexpr std::u8string_view rendition = u8"\x1b#3";
    if (buffer.bytes().size() != rendition.size() ||
        !std::equal(buffer.bytes().begin(), buffer.bytes().end(), rendition.begin()))
    {
        std::fprintf(stderr, "line rendition serialization failed\n");
        return EXIT_FAILURE;
    }

    buffer.clear();
    append_utf32(buffer, U"A\u4E2D\U0001F600");
    constexpr std::u8string_view utf8 = u8"A\u4E2D\U0001F600";
    if (buffer.bytes().size() != utf8.size() ||
        !std::equal(buffer.bytes().begin(), buffer.bytes().end(), utf8.begin()))
    {
        std::fprintf(stderr, "utf32 serialization failed\n");
        return EXIT_FAILURE;
    }

    buffer.clear();
    append_utf16(buffer, u"A\u4E2D\U0001F600");
    if (buffer.bytes().size() != utf8.size() ||
        !std::equal(buffer.bytes().begin(), buffer.bytes().end(), utf8.begin()))
    {
        std::fprintf(stderr, "utf16 serialization failed\n");
        return EXIT_FAILURE;
    }

    buffer.clear();
    append_cup(buffer, 0, 0);
    constexpr std::u8string_view cup = u8"\x1b[1;1H";
    if (buffer.bytes().size() != cup.size() ||
        !std::equal(buffer.bytes().begin(), buffer.bytes().end(), cup.begin()))
    {
        std::fprintf(stderr, "cup serialization failed\n");
        return EXIT_FAILURE;
    }

    buffer.clear();
    append_sgr_legacy(buffer, 0x1F);
    constexpr std::u8string_view sgr = u8"\x1b[0;97;44m";
    if (buffer.bytes().size() != sgr.size() ||
        !std::equal(buffer.bytes().begin(), buffer.bytes().end(), sgr.begin()))
    {
        std::fprintf(stderr, "sgr serialization failed\n");
        return EXIT_FAILURE;
    }

    corehost::screen::screen_buffer screen{10, 2};
    corehost::screen::coord cursor{0, 0};
    (void)screen.write_text_row(cursor, u"AB", {}, corehost::screen::cell_style{}, true);
    std::fprintf(stderr, "before render\n");
    std::fflush(stderr);
    buffer.clear();
    render_damage(buffer, screen, corehost::screen::coord{2, 0});
    std::fprintf(stderr, "after render\n");
    std::fflush(stderr);
    bool found_ab = false;
    for (std::size_t i = 0; i + 1 < buffer.bytes().size(); ++i)
    {
        if (buffer.bytes()[i] == u8'A' && buffer.bytes()[i + 1] == u8'B')
        {
            found_ab = true;
            break;
        }
    }
    if (!found_ab)
    {
        std::fprintf(stderr, "viewport text missing\n");
        return EXIT_FAILURE;
    }

    screen.set_flags(0, corehost::screen::line_flag_ops::raw(corehost::screen::line_flag::double_width));
    buffer.clear();
    render_damage(buffer, screen, corehost::screen::coord{2, 0});
    const std::u8string rendition_output{buffer.bytes().begin(), buffer.bytes().end()};
    if (rendition_output.find(u8"\x1b#6") == std::u8string::npos)
    {
        std::fprintf(stderr, "line rendition output missing\n");
        return EXIT_FAILURE;
    }
    screen.set_flags(0, 0);

    const auto link = screen.hyperlinks().intern(u"https://e", u"id");
    (void)screen.write_glyph(corehost::screen::coord{0, 1}, u"L", 1,
                             corehost::screen::cell_style{}, link);
    buffer.clear();
    render_damage(buffer, screen, corehost::screen::coord{1, 1});
    const std::u8string rendered{buffer.bytes().begin(), buffer.bytes().end()};
    if (rendered.find(u8"\x1b]8;id=id;https://e") == std::u8string::npos)
    {
        std::fprintf(stderr, "hyperlink serialization missing\n");
        return EXIT_FAILURE;
    }

    std::printf("libvtserializer tests passed\n");
    return EXIT_SUCCESS;
}