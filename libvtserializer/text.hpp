#pragma once

#include "buffer.hpp"

#include <libunicode/convert.h>

#include <string_view>

namespace corehost::vtserializer
{

inline void append_utf32(byte_buffer &output, std::u32string_view text)
{
    const auto offset = output.size();
    output.resize(offset + text.size() * 4);

    auto *first = reinterpret_cast<char *>(output.mutable_data() + offset);
    auto *last = first;
    for (const char32_t codepoint : text)
    {
        last = unicode::encoder<char>{}(codepoint, last);
    }

    output.resize(offset + static_cast<std::size_t>(last - first));
}

} // namespace corehost::vtserializer