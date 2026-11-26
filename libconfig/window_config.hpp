#pragma once

#include "console_theme.hpp"
#include "text_options.hpp"

#include <cstdint>

namespace corehost::window
{
    enum class bell_mode : unsigned char
    {
        off,
        visual,
    };

    enum class client_exit_policy : unsigned char
    {
        keep_open,
        exit_if_never_read_input,
        always_exit,
    };

    enum class backdrop_mode : unsigned char
    {
        none,
        mica,
        acrylic,
    };

    // Window/display behavior. File loading is intentionally not implemented.
    struct window_config final
    {
        console_palette palette{console_palette::campbell()};
        bell_mode bell{bell_mode::visual};
        uitext::text_options text_options{};
        backdrop_mode backdrop{backdrop_mode::none};
        std::uint8_t opacity{255};
        bool dark_mode{true};
        client_exit_policy exit_policy{client_exit_policy::keep_open};
    };
}