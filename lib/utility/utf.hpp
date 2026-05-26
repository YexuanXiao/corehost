#pragma once
#include <span>

namespace utility
{

enum class utf_class
{
    unknown,
    utf8bom,
    utf16le,
    utf16be,
    utf32le,
    utf32be,
};

inline constexpr utf_class detect_utf_encoding(std::span<unsigned char> data)
{
    if (data.size() >= 4)
    {
        if (data[0] == 0x00 && data[1] == 0x00 && data[2] == 0xFE && data[3] == 0xFF)
            return utf_class::utf32be;
        if (data[0] == 0xFF && data[1] == 0xFE && data[2] == 0x00 && data[3] == 0x00)
            return utf_class::utf32le;
    }
    if (data.size() >= 2)
    {
        if (data[0] == 0xFE && data[1] == 0xFF)
            return utf_class::utf16be;
        if (data[0] == 0xFF && data[1] == 0xFE)
            return utf_class::utf16le;
    }
    if (data.size() >= 3 && data[0] == 0xEF && data[1] == 0xBB && data[2] == 0xBF)
        return utf_class::utf8bom;
    return utf_class::unknown;
}
} // namespace utility