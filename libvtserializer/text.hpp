#pragma once

#include "buffer.hpp"

#include <libunicode/convert.h>

#include <string_view>
#include <string>

namespace corehost::vtserializer
{

inline void append_utf16(byte_buffer &output, std::u16string_view text)
{
    const auto offset = output.size();
    output.resize(offset + text.size() * 3);

    auto *first = reinterpret_cast<char *>(output.mutable_data() + offset);
    auto *last = first;
    unicode::decoder<char16_t> decoder;
    unicode::encoder<char> encoder;
    auto iterator = text.begin();
    while (iterator != text.end())
    {
        auto codepoint = decoder(iterator);
        if (!codepoint.has_value())
        {
            codepoint = 0xFFFD;
        }
        last = encoder(*codepoint, last);
    }

    output.resize(offset + static_cast<std::size_t>(last - first));
}

} // namespace corehost::vtserializer