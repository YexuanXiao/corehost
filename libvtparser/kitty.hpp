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
    std::uint32_t columns{};
    std::uint32_t rows{};
    std::uint32_t placement_id{};
    std::u16string_view control;
    std::u16string_view payload;
};

namespace detail
{

inline std::uint32_t parse_decimal(std::u16string_view text, std::size_t &position) noexcept
{
    std::uint32_t value = 0;
    while (position < text.size() && text[position] >= u'0' && text[position] <= u'9')
    {
        value = value * 10 + static_cast<std::uint32_t>(text[position] - u'0');
        ++position;
    }
    return value;
}

inline std::optional<std::uint32_t> field_value(std::u16string_view control, char16_t key) noexcept
{
    std::size_t position = 0;
    while (position < control.size())
    {
        if (control[position] == key && position + 1 < control.size() && control[position + 1] == u'=')
        {
            position += 2;
            return parse_decimal(control, position);
        }
        const auto comma = control.find(u',', position);
        if (comma == std::u16string_view::npos)
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
[[nodiscard]] inline std::optional<kitty_graphics> parse_kitty_graphics(std::u16string_view apc) noexcept
{
    if (!apc.starts_with(u"\x1b_G"))
    {
        return std::nullopt;
    }

    auto body = apc.substr(3);
    if (body.ends_with(u"\x1b\\"))
    {
        body.remove_suffix(2);
    }
    else if (!body.empty() && body.back() == 0x07)
    {
        body.remove_suffix(1);
    }

    const auto separator = body.find(u';');
    const auto control = separator == std::u16string_view::npos ? body : body.substr(0, separator);
    const auto payload = separator == std::u16string_view::npos ? std::u16string_view{} : body.substr(separator + 1);

    kitty_graphics result;
    result.control = control;
    result.payload = payload;

    for (std::size_t position = 0; position < control.size();)
    {
        if (control[position] == u'a' && position + 1 < control.size() && control[position + 1] == u'=')
        {
            if (position + 2 < control.size())
            {
                result.action = static_cast<char16_t>(control[position + 2]);
            }
            break;
        }
        const auto comma = control.find(u',', position);
        if (comma == std::u16string_view::npos)
        {
            break;
        }
        position = comma + 1;
    }

    if (const auto id = detail::field_value(control, u'i'))
    {
        result.image_id = *id;
    }
    if (const auto format = detail::field_value(control, u'f'))
    {
        result.format = *format;
    }
    if (const auto width = detail::field_value(control, u's'))
    {
        result.width = *width;
    }
    if (const auto height = detail::field_value(control, u'v'))
    {
        result.height = *height;
    }
    if (const auto columns = detail::field_value(control, u'c'))
    {
        result.columns = *columns;
    }
    if (const auto rows = detail::field_value(control, u'r'))
    {
        result.rows = *rows;
    }
    if (const auto placement = detail::field_value(control, u'p'))
    {
        result.placement_id = *placement;
    }
    return result;
}

} // namespace corehost::vtparser