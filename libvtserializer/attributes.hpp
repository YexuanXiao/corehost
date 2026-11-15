#pragma once

#include "buffer.hpp"

#include <charconv>
#include <cstdint>

namespace corehost::vtserializer
{

// Maps Win32 BGRI attributes to a compact 16-color SGR sequence. One stack
// buffer is filled and appended once; no per-cell strings or allocations.
[[nodiscard]] constexpr unsigned legacy_color_index(unsigned bgri) noexcept
{
    return static_cast<unsigned>((bgri & 0x04 ? 1u : 0u) | (bgri & 0x02 ? 2u : 0u) | (bgri & 0x01 ? 4u : 0u));
}
inline void append_sgr_legacy(byte_buffer &output, std::uint16_t attributes)
{
    const unsigned foreground = attributes & 0x0F;
    const unsigned background = (attributes >> 4) & 0x0F;

    const unsigned fg = legacy_color_index(foreground);
    const unsigned bg = legacy_color_index(background);
    const unsigned fg_code = (foreground & 0x08 ? 90u : 30u) + fg;
    const unsigned bg_code = (background & 0x08 ? 100u : 40u) + bg;

    char sequence[32]{};
    auto *first = sequence;
    *first++ = static_cast<char>(0x1B);
    *first++ = '[';
    *first++ = '0';
    *first++ = ';';
    auto result = std::to_chars(first, sequence + sizeof(sequence), fg_code);
    first = result.ptr;
    *first++ = ';';
    result = std::to_chars(first, sequence + sizeof(sequence), bg_code);
    first = result.ptr;
    *first++ = 'm';

    output.append(std::span<const char8_t>{reinterpret_cast<const char8_t *>(sequence),
                                           static_cast<std::size_t>(first - sequence)});
}

} // namespace corehost::vtserializer