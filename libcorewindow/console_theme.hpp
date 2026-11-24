#pragma once

#include "style.hpp"

#include <array>
#include <cstdint>

namespace corehost::window
{

struct console_color final
{
    std::uint8_t r{};
    std::uint8_t g{};
    std::uint8_t b{};
};

// Theme interface. Indexed colors are resolved through this table; undefined
// entries fall back to the default foreground/background colors.
enum class faint_mode : std::uint8_t
{
    blend_background,
    alpha,
};

struct console_palette final
{
    std::array<console_color, 16> indexed{};
    console_color default_foreground{0xCC, 0xCC, 0xCC};
    console_color default_background{0x0C, 0x0C, 0x0C};
    faint_mode faint{faint_mode::blend_background};

    // Windows Terminal's Campbell scheme.
    static console_palette campbell() noexcept
    {
        return console_palette{
            .indexed =
                {
                    console_color{0x0C, 0x0C, 0x0C},
                    console_color{0xC5, 0x0F, 0x1F},
                    console_color{0x13, 0xA1, 0x0E},
                    console_color{0xC1, 0x9C, 0x00},
                    console_color{0x00, 0x37, 0xDA},
                    console_color{0x88, 0x17, 0x98},
                    console_color{0x3A, 0x96, 0xDD},
                    console_color{0xCC, 0xCC, 0xCC},
                    console_color{0x76, 0x76, 0x76},
                    console_color{0xE7, 0x48, 0x56},
                    console_color{0x16, 0xC6, 0x0C},
                    console_color{0xF9, 0xF1, 0xA5},
                    console_color{0x3B, 0x78, 0xFF},
                    console_color{0xB4, 0x00, 0x9E},
                    console_color{0x61, 0xD6, 0xD6},
                    console_color{0xF2, 0xF2, 0xF2},
                },
            .default_foreground = console_color{0xCC, 0xCC, 0xCC},
            .default_background = console_color{0x0C, 0x0C, 0x0C},
        };
    }
};

[[nodiscard]] inline console_color resolve_color(const screen::color &value, const console_palette &palette,
                                                 bool foreground) noexcept
{
    switch (value.kind)
    {
    case screen::color_kind::default_color:
        return foreground ? palette.default_foreground : palette.default_background;
    case screen::color_kind::indexed:
        return palette.indexed[value.value & 0x0F];
    case screen::color_kind::rgb:
        return console_color{static_cast<std::uint8_t>((value.value >> 16) & 0xFF),
                             static_cast<std::uint8_t>((value.value >> 8) & 0xFF),
                             static_cast<std::uint8_t>(value.value & 0xFF)};
    }
    return foreground ? palette.default_foreground : palette.default_background;
}

[[nodiscard]] inline console_color foreground_color(const screen::text_style &style,
                                                    const console_palette &palette) noexcept
{
    const bool negative = (style.attributes & screen::text_style_attribute_bit(screen::text_style_attribute::negative)) != 0;
    return negative ? resolve_color(style.background, palette, false) : resolve_color(style.foreground, palette, true);
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
