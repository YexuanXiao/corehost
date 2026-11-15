#include "buffer.hpp"
#include "control.hpp"
#include "text.hpp"
#include "text.hpp"

#include <algorithm>
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

    std::printf("libvtserializer tests passed\n");
    return EXIT_SUCCESS;
}