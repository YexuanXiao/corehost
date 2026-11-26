#pragma once

#include "console_theme.hpp"
#include "text_options.hpp"

#include <cstdint>

namespace corehost::window
{
    enum class backdrop_mode : std::uint8_t
    {
        none,
        mica,
        acrylic,
    };

    // All window-mode settings live here. File loading is intentionally not
    // implemented yet; callers construct this type directly.
    struct window_config final
    {
        console_palette palette{console_palette::campbell()};
        uitext::text_options text_options{};
        backdrop_mode backdrop{backdrop_mode::none};
        std::uint8_t opacity{255};
        bool dark_mode{true};
    };
}
