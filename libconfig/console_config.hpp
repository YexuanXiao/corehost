#pragma once

#include "console_theme.hpp"

namespace corehost::window
{
    enum class bell_mode : unsigned char
    {
        off,
        visual,
    };

    // Console behavior shared by all window-mode front ends.
    struct console_config final
    {
        console_palette palette{console_palette::campbell()};
        bell_mode bell{bell_mode::visual};
    };
}