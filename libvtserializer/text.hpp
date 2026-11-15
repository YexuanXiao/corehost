#pragma once

#include "buffer.hpp"

#include <libunicode/convert.h>

#include <iterator>
#include <string_view>
#include <string>

namespace corehost::vtserializer
{

inline void append_utf16(byte_buffer &output, std::u16string_view text)
{
    output.reserve(output.size() + text.size() * 3);
    auto destination = std::back_inserter(output);

    unicode::decoder<char16_t> decoder;
    unicode::encoder<char> encoder;
    auto iterator = text.begin();
    while (iterator != text.end())
    {
        destination = encoder(decoder(iterator).value_or(0xFFFD), destination);
    }
}

} // namespace corehost::vtserializer