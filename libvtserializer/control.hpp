#pragma once

#include "buffer.hpp"

#include <cstdint>
#include <string_view>

namespace corehost::vtserializer
{

inline void append_hyperlink_open(byte_buffer &output,
                                  std::u8string_view params,
                                  std::u8string_view uri)
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

} // namespace corehost::vtserializer
