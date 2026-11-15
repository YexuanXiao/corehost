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

} // namespace corehost::vtserializer
