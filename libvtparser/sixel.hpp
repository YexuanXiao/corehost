#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

namespace corehost::vtparser
{

struct sixel_graphics final
{
    std::u16string_view parameters;
    std::u16string_view data;
    std::uint32_t width{};
    std::uint32_t height{};
};

namespace detail
{

inline std::uint32_t raster_value(std::u16string_view data, std::size_t &position) noexcept
{
    std::uint32_t value = 0;
    while (position < data.size() && data[position] >= u'0' && data[position] <= u'9')
    {
        value = value * 10 + static_cast<std::uint32_t>(data[position] - u'0');
        ++position;
    }
    return value;
}

inline void parse_raster_attributes(std::u16string_view data, std::uint32_t &width, std::uint32_t &height) noexcept
{
    const auto marker = data.find(u'"');
    if (marker == std::u16string_view::npos)
    {
        return;
    }

    std::size_t position = marker + 1;
    for (int field = 0; field < 4 && position < data.size(); ++field)
    {
        const auto value = raster_value(data, position);
        if (field == 2)
        {
            width = value;
        }
        else if (field == 3)
        {
            height = value;
        }
        if (position >= data.size() || data[position] != u';')
        {
            break;
        }
        ++position;
    }
}

} // namespace detail

// Parses a DCS payload captured by vt_parser. The data after 'q' stays a view;
// palette/color decoding is intentionally left to the image layer.
[[nodiscard]] inline std::optional<sixel_graphics> parse_sixel(std::u16string_view dcs) noexcept
{
    if (!dcs.starts_with(u"\x1bP"))
    {
        return std::nullopt;
    }

    auto body = dcs.substr(2);
    if (body.ends_with(u"\x1b\\"))
    {
        body.remove_suffix(2);
    }
    else if (!body.empty() && body.back() == 0x07)
    {
        body.remove_suffix(1);
    }

    const auto marker = body.find(u'q');
    if (marker == std::u16string_view::npos)
    {
        return std::nullopt;
    }

    sixel_graphics result;
    result.parameters = body.substr(0, marker);
    result.data = body.substr(marker + 1);
    detail::parse_raster_attributes(result.data, result.width, result.height);
    return result;
}

} // namespace corehost::vtparser
