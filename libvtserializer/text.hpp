#pragma once

#include "buffer.hpp"

#include <libunicode/convert.h>

#include <string_view>
#include <string>

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

inline void append_utf16(byte_buffer &output, std::u16string_view text)
{
    const auto offset = output.size();
    output.resize(offset + text.size() * 3);

    auto *first = reinterpret_cast<char *>(output.mutable_data() + offset);
    auto *last = first;
    for (std::size_t i = 0; i < text.size();)
    {
        const char16_t unit = text[i++];
        char32_t codepoint = unit;
        if (unit >= 0xD800 && unit <= 0xDBFF)
        {
            if (i < text.size())
            {
                const char16_t low = text[i];
                if (low >= 0xDC00 && low <= 0xDFFF)
                {
                    ++i;
                    codepoint = 0x10000 + ((static_cast<char32_t>(unit) - 0xD800) << 10) +
                                (static_cast<char32_t>(low) - 0xDC00);
                }
                else
                {
                    codepoint = 0xFFFD;
                }
            }
            else
            {
                codepoint = 0xFFFD;
            }
        }
        else if (unit >= 0xDC00 && unit <= 0xDFFF)
        {
            codepoint = 0xFFFD;
        }
        last = unicode::encoder<char>{}(codepoint, last);
    }

    output.resize(offset + static_cast<std::size_t>(last - first));
}

} // namespace corehost::vtserializer