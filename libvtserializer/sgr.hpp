#pragma once

#include "buffer.hpp"

#include "style.hpp"

#include <charconv>
#include <cstdint>

namespace corehost::vtserializer
{

inline char *append_color(char *first, char *last,
                          const screen::color &value,
                          unsigned base_code) noexcept
{
    if (value.kind == screen::color_kind::rgb)
    {
        *first++ = ';';
        auto result = std::to_chars(first, last, base_code);
        first = result.ptr;
        *first++ = ';';
        *first++ = '2';
        for (unsigned shift = 16; shift != 0; shift -= 8)
        {
            *first++ = ';';
            result = std::to_chars(first, last, (value.value >> shift) & 0xFFu);
            first = result.ptr;
        }
        *first++ = ';';
        result = std::to_chars(first, last, value.value & 0xFFu);
        return result.ptr;
    }
    if (value.kind == screen::color_kind::indexed)
    {
        *first++ = ';';
        auto result = std::to_chars(first, last, base_code);
        first = result.ptr;
        *first++ = ';';
        *first++ = '5';
        *first++ = ';';
        result = std::to_chars(first, last, value.value);
        return result.ptr;
    }
    return first;
}

inline void append_sgr_style(byte_buffer &output, const screen::text_style &style)
{
    char sequence[96]{};
    auto *first = sequence;
    *first++ = static_cast<char>(0x1B);
    *first++ = '[';
    *first++ = '0';

    first = append_color(first, sequence + sizeof(sequence), style.foreground, 38);
    first = append_color(first, sequence + sizeof(sequence), style.background, 48);
    first = append_color(first, sequence + sizeof(sequence), style.underline, 58);

    *first++ = 'm';
    output.append(std::span<const char8_t>{reinterpret_cast<const char8_t *>(sequence),
                                           static_cast<std::size_t>(first - sequence)});
}

} // namespace corehost::vtserializer