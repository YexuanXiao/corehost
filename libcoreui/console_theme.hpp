#pragma once

#include "color_types.hpp"
#include "style.hpp"

#include <array>
#include <cstdint>

namespace corehost::window
{

[[nodiscard]] inline console_color resolve_color(const screen::color &value, const console_palette &palette,
                                                 bool foreground) noexcept
{
    switch (value.kind)
    {
    case screen::color_kind::default_color:
        return foreground ? palette.default_foreground : palette.default_background;
    case screen::color_kind::indexed: {
        const auto index = value.value & 0xFFu;
        if (index < 16)
        {
            return palette.indexed[index];
        }
        if (index < 232)
        {
            const auto cube = index - 16;
            constexpr std::uint8_t levels[6] = {0, 95, 135, 175, 215, 255};
            return console_color{levels[(cube / 36) % 6], levels[(cube / 6) % 6], levels[cube % 6]};
        }
        if (index < 256)
        {
            const auto level = static_cast<std::uint8_t>(8 + 10 * (index - 232));
            return console_color{level, level, level};
        }
        return foreground ? palette.default_foreground : palette.default_background;
    }
    case screen::color_kind::rgb:
        return console_color{static_cast<std::uint8_t>((value.value >> 16) & 0xFF),
                             static_cast<std::uint8_t>((value.value >> 8) & 0xFF),
                             static_cast<std::uint8_t>(value.value & 0xFF)};
    }
    return foreground ? palette.default_foreground : palette.default_background;
}

[[nodiscard]] inline console_color foreground_color(const screen::text_style &style,
                                                    const console_palette &palette,
                                                    bool bold_is_bright = false) noexcept
{
    auto effective = style;
    if (bold_is_bright &&
        (effective.attributes & screen::text_style_attribute_bit(screen::text_style_attribute::bold)) != 0 &&
        effective.foreground.kind == screen::color_kind::indexed && effective.foreground.value < 8)
    {
        effective.foreground.value += 8;
    }
    const bool negative =
        (effective.attributes & screen::text_style_attribute_bit(screen::text_style_attribute::negative)) != 0;
    return negative ? resolve_color(effective.background, palette, false)
                    : resolve_color(effective.foreground, palette, true);
}

[[nodiscard]] inline std::uint8_t blend_channel(std::uint8_t foreground, std::uint8_t background,
                                                float amount) noexcept
{
    return static_cast<std::uint8_t>(static_cast<float>(foreground) * (1.0f - amount) +
                                     static_cast<float>(background) * amount);
}

[[nodiscard]] inline console_color blend_color(console_color foreground, console_color background,
                                               float amount) noexcept
{
    return console_color{blend_channel(foreground.r, background.r, amount),
                         blend_channel(foreground.g, background.g, amount),
                         blend_channel(foreground.b, background.b, amount)};
}

[[nodiscard]] inline console_color background_color(const screen::text_style &style,
                                                    const console_palette &palette) noexcept
{
    const bool negative = (style.attributes & screen::text_style_attribute_bit(screen::text_style_attribute::negative)) != 0;
    return negative ? resolve_color(style.foreground, palette, true) : resolve_color(style.background, palette, false);
}

} // namespace corehost::window
