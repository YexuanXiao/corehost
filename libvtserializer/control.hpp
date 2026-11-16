#pragma once

#include "buffer.hpp"
#include "text.hpp"

#include <charconv>
#include <cstdint>
#include <string_view>

namespace corehost::vtserializer
{

inline void append_hyperlink_open(byte_buffer &output, std::u8string_view params, std::u8string_view uri)
{
    output.append_text(u8"\x1b]8;");
    output.append_text(params);
    output.append_text(u8";");
    output.append_text(uri);
    output.push_back(static_cast<char8_t>(0x07));
}

inline void append_hyperlink_close(byte_buffer &output)
{
    output.append_text(u8"\x1b]8;;");
    output.push_back(static_cast<char8_t>(0x07));
}

inline void append_line_rendition(byte_buffer &output, unsigned value)
{
    const char8_t sequence[3]{
        static_cast<char8_t>(0x1B),
        u8'#',
        static_cast<char8_t>(u8'0' + value),
    };
    output.append(std::span<const char8_t>{sequence, 3});
}

inline void append_cup(byte_buffer &output, unsigned row, unsigned column)
{
    char sequence[32];
    auto *first = sequence;
    *first++ = static_cast<char>(0x1B);
    *first++ = '[';
    auto result = std::to_chars(first, sequence + sizeof(sequence), row + 1);
    first = result.ptr;
    *first++ = ';';
    result = std::to_chars(first, sequence + sizeof(sequence), column + 1);
    first = result.ptr;
    *first++ = 'H';
    output.append(std::span<const char8_t>{reinterpret_cast<const char8_t *>(sequence),
                                           static_cast<std::size_t>(first - sequence)});
}

inline void append_scroll_up(byte_buffer &output, unsigned lines)
{
    if (lines == 0)
        return;
    char sequence[24];
    auto *first = sequence;
    *first++ = static_cast<char>(0x1B);
    *first++ = '[';
    const auto result = std::to_chars(first, sequence + sizeof(sequence), lines);
    first = result.ptr;
    *first++ = 'S';
    output.append(std::span<const char8_t>{reinterpret_cast<const char8_t *>(sequence),
                                           static_cast<std::size_t>(first - sequence)});
}

inline void append_hyperlink_open_id(byte_buffer &output, std::u16string_view id, std::u16string_view uri)
{
    output.append_text(u8"\x1b]8;id=");
    append_utf16(output, id);
    output.append_text(u8";");
    append_utf16(output, uri);
    output.push_back(static_cast<char8_t>(0x07));
}

inline void append_cursor_visibility(byte_buffer &output, bool visible)
{
    output.append_text(visible ? u8"\x1b[?25h" : u8"\x1b[?25l");
}

inline void append_cursor_shape(byte_buffer &output, unsigned style, bool blinking)
{
    const unsigned code = style * 2 + (blinking ? 1u : 2u);
    char sequence[8];
    auto *first = sequence;
    *first++ = static_cast<char>(0x1B);
    *first++ = '[';
    auto result = std::to_chars(first, sequence + sizeof(sequence), code);
    first = result.ptr;
    *first++ = ' ';
    *first++ = 'q';
    output.append(std::span<const char8_t>{reinterpret_cast<const char8_t *>(sequence),
                                           static_cast<std::size_t>(first - sequence)});
}

inline void append_private_mode(byte_buffer &output, int mode, bool enabled)
{
    output.append_text(u8"\x1b[?");
    output.append_number(mode);
    output.append_text(enabled ? u8"h" : u8"l");
}

inline void append_erase_line(byte_buffer &output, int mode = 0)
{
    output.append_text(u8"\x1b[");
    output.append_number(mode);
    output.append_text(u8"K");
}

inline void append_erase_display(byte_buffer &output, int mode = 0)
{
    output.append_text(u8"\x1b[");
    output.append_number(mode);
    output.append_text(u8"J");
}

inline void append_clear_screen(byte_buffer &output)
{
    output.append_text(u8"\x1b[2J");
}

inline void append_window_title(byte_buffer &output, std::u16string_view title)
{
    output.append_text(u8"\x1b]0;");
    append_utf16(output, title);
    output.push_back(static_cast<char8_t>(0x07));
}
inline void append_request_cursor_position(byte_buffer &output)
{
    output.append_text(u8"\x1b[6n");
}

} // namespace corehost::vtserializer
