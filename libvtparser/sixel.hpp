#pragma once

#include <optional>
#include <string_view>

namespace corehost::vtparser
{

struct sixel_graphics final
{
    std::u32string_view parameters;
    std::u32string_view data;
};

// Parses a DCS payload captured by vt_parser. The data after 'q' stays a view;
// palette/color decoding is intentionally left to the image layer.
[[nodiscard]] inline std::optional<sixel_graphics> parse_sixel(std::u32string_view dcs) noexcept
{
    if (!dcs.starts_with(U"\x1bP"))
    {
        return std::nullopt;
    }

    auto body = dcs.substr(2);
    if (body.ends_with(U"\x1b\\"))
    {
        body.remove_suffix(2);
    }
    else if (!body.empty() && body.back() == 0x07)
    {
        body.remove_suffix(1);
    }

    const auto marker = body.find(U'q');
    if (marker == std::u32string_view::npos)
    {
        return std::nullopt;
    }

    return sixel_graphics{
        body.substr(0, marker),
        body.substr(marker + 1),
    };
}

} // namespace corehost::vtparser