#pragma once

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

    static constexpr console_palette campbell() noexcept
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
} // namespace corehost::window