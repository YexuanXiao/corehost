#pragma once

#include "buffer.hpp"

#include <libunicode/convert.h>

#include <array>
#include <string_view>
#include <string>

namespace corehost::vtserializer
{

inline void append_utf16_unit(byte_buffer &output, char16_t unit)
{
    std::array<char, 4> encoded;
    unicode::decoder<char16_t> decoder;
    unicode::encoder<char> encoder;
    const char16_t *iterator = &unit;
    auto *last = encoder(decoder(iterator).value_or(0xFFFD), encoded.data());
    if (last == encoded.data() + 1)
    {
        output.push_back(static_cast<char8_t>(encoded[0]));
        return;
    }
    output.append(std::span<const char8_t>{reinterpret_cast<const char8_t *>(encoded.data()),
                                           static_cast<std::size_t>(last - encoded.data())});
}

inline void append_utf16(byte_buffer &output, std::u16string_view text)
{
    if (text.size() == 1)
    {
        append_utf16_unit(output, text[0]);
        return;
    }

    const auto offset = output.size();
    output.resize(offset + text.size() * 3);

    auto *first = reinterpret_cast<char *>(output.mutable_data() + offset);
    auto *last = first;
    unicode::decoder<char16_t> decoder;
    unicode::encoder<char> encoder;
    auto iterator = text.begin();
    while (iterator != text.end())
    {
        last = encoder(decoder(iterator).value_or(0xFFFD), last);
    }

    output.resize(offset + static_cast<std::size_t>(last - first));
}

} // namespace corehost::vtserializer