#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace corehost::screen
{

enum class text_measurement
{
    console,
    unicode,
};

struct width_options final
{
    text_measurement mode{text_measurement::console};
    bool ambiguous_is_wide{false};
};

[[nodiscard]] constexpr bool is_combining(char32_t value) noexcept
{
    return (value >= 0x0300 && value <= 0x036F) ||
           (value >= 0x1AB0 && value <= 0x1AFF) ||
           (value >= 0x1DC0 && value <= 0x1DFF) ||
           (value >= 0x20D0 && value <= 0x20FF) ||
           (value >= 0xFE20 && value <= 0xFE2F);
}

[[nodiscard]] constexpr bool is_wide(char32_t value, width_options options) noexcept
{
    if ((value >= 0x1100 && value <= 0x115F) ||
        (value >= 0x2E80 && value <= 0xA4CF) ||
        (value >= 0xAC00 && value <= 0xD7A3) ||
        (value >= 0xF900 && value <= 0xFAFF) ||
        (value >= 0xFE10 && value <= 0xFE19) ||
        (value >= 0xFE30 && value <= 0xFE6F) ||
        (value >= 0xFF00 && value <= 0xFF60) ||
        (value >= 0xFFE0 && value <= 0xFFE6) ||
        (value >= 0x1F300 && value <= 0x1FAFF) ||
        (value >= 0x20000 && value <= 0x3FFFD))
    {
        return true;
    }

    if (options.mode == text_measurement::unicode)
    {
        // East Asian Ambiguous characters are opt-in.
        return options.ambiguous_is_wide && (value >= 0x00A1 && value <= 0x00FF);
    }
    return false;
}

[[nodiscard]] constexpr int codepoint_width(char32_t value, width_options options) noexcept
{
    if (is_combining(value))
    {
        return 0;
    }
    return is_wide(value, options) ? 2 : 1;
}

struct codepoint_result final
{
    char32_t value{};
    std::size_t units{};
};

[[nodiscard]] constexpr codepoint_result next_codepoint(std::u16string_view text, std::size_t offset) noexcept
{
    if (offset >= text.size())
    {
        return {};
    }

    const auto first = static_cast<std::uint32_t>(text[offset]);
    if (first >= 0xD800 && first <= 0xDBFF && offset + 1 < text.size())
    {
        const auto second = static_cast<std::uint32_t>(text[offset + 1]);
        if (second >= 0xDC00 && second <= 0xDFFF)
        {
            return codepoint_result{
                static_cast<char32_t>(0x10000u + ((first - 0xD800u) << 10) + (second - 0xDC00u)),
                2,
            };
        }
    }
    return codepoint_result{static_cast<char32_t>(first), 1};
}

[[nodiscard]] inline int glyph_width(std::u16string_view glyph, width_options options) noexcept
{
    if (glyph.empty())
    {
        return 0;
    }

    auto result = next_codepoint(glyph, 0);
    auto width = codepoint_width(result.value, options);
    auto offset = result.units;
    while (offset < glyph.size())
    {
        result = next_codepoint(glyph, offset);
        const auto next = codepoint_width(result.value, options);
        if (next > width)
        {
            width = next;
        }
        offset += result.units;
    }
    return width;
}

} // namespace corehost::screen
