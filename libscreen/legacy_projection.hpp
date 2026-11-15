#pragma once

#include "cell.hpp"
#include "style.hpp"
#include "win32_types.hpp"

#include <cstdint>

namespace corehost::screen
{

// Platform-neutral CHAR_INFO used inside libscreen. libinterop owns the ABI
// layout; conversion is explicit at the boundary.
struct char_info final
{
    char16_t character{u' '};
    std::uint16_t attributes{interop::win32::default_attributes};

    friend constexpr bool operator==(const char_info &, const char_info &) noexcept = default;
};

[[nodiscard]] inline char_info project_cell(const cell &value, const style_table &styles) noexcept
{
    auto attributes = styles.at(value.style()).legacy_attributes;
    if (value.is_wide())
    {
        attributes = static_cast<std::uint16_t>(attributes | interop::win32::lvb_leading_byte);
    }
    else if (value.is_continuation())
    {
        attributes = static_cast<std::uint16_t>(attributes | interop::win32::lvb_trailing_byte);
    }

    char16_t character = value.text_unit();
    if (value.has_cluster() || value.has_image())
    {
        character = u'\uFFFD';
    }

    return char_info{character, attributes};
}

[[nodiscard]] inline interop::win32::char_info to_win32(char_info value) noexcept
{
    interop::win32::char_info result{};
    result.char_data.unicode_char = value.character;
    result.attributes = value.attributes;
    return result;
}

[[nodiscard]] inline char_info from_win32(interop::win32::char_info value) noexcept
{
    return char_info{value.char_data.unicode_char, value.attributes};
}

static_assert(sizeof(char_info) == sizeof(interop::win32::char_info));

} // namespace corehost::screen
