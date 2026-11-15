#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

namespace corehost::vtparser
{

struct kitty_graphics final
{
    char16_t action{};
    std::uint32_t image_id{};
    std::uint32_t format{};
    std::uint32_t width{};
    std::uint32_t height{};
    std::u32string_view control;
    std::u32string_view payload;
};

namespace detail
{

inline std::uint32_t parse_decimal(std::u32string_view text, std::size_t &position) noexcept
{
    std::uint32_t value = 0;
    while (position < text.size() && text[position] >= U'0' && text[position] <= U'9')
    {
        value = value * 10 + static_cast<std::uint32_t>(text[position] - U'0');
        ++position;
    }
    return value;
}

inline std::optional<std::uint32_t> field_value(std::u32string_view control, char16_t key) noexcept
{
    std::size_t position = 0;
    while (position < control.size())
    {
        if (control[position] == key && position + 1 < control.size() && control[position + 1] == U'=')
        {
            position += 2;
            return parse_decimal(control, position);
        }
        const auto comma = control.find(U',', position);
        if (comma == std::u32string_view::npos)
        {
            break;
        }
        position = comma + 1;
    }
    return std::nullopt;
}

} // namespace detail

// Parses the APC payload captured by vt_parser. The input includes ESC _ G and
// the trailing ST/BEL. Only metadata is parsed; image bytes stay a view.
[[nodiscard]] inline std::optional<kitty_graphics>
parse_kitty_graphics(std::u32string_view apc) noexcept
{
    if (!apc.starts_with(U"\x1b_G"))
    {
        return std::nullopt;
    }

    auto body = apc.substr(3);
    if (body.ends_with(U"\x1b\\"))
    {
        body.remove_suffix(2);
    }
    else if (!body.empty() && body.back() == 0x07)
    {
        body.remove_suffix(1);
    }

    const auto separator = body.find(U';');
    const auto control = separator == std::u32string_view::npos ? body : body.substr(0, separator);
    const auto payload = separator == std::u32string_view::npos ? std::u32string_view{}
                                                                 : body.substr(separator + 1);

    kitty_graphics result;
    result.control = control;
    result.payload = payload;

    for (std::size_t position = 0; position < control.size();)
    {
        if (control[position] == U'a' && position + 1 < control.size() && control[position + 1] == U'=')
        {
            if (position + 2 < control.size())
            {
                result.action = static_cast<char16_t>(control[position + 2]);
            }
            break;
        }
        const auto comma = control.find(U',', position);
        if (comma == std::u32string_view::npos)
        {
            break;
        }
        position = comma + 1;
    }

    if (const auto id = detail::field_value(control, U'i'))
    {
        result.image_id = *id;
    }
    if (const auto format = detail::field_value(control, U'f'))
    {
        result.format = *format;
    }
    if (const auto width = detail::field_value(control, U's'))
    {
        result.width = *width;
    }
    if (const auto height = detail::field_value(control, U'v'))
    {
        result.height = *height;
    }
    return result;
}

} // namespace corehost::vtparser