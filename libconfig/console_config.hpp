#pragma once

#include <cstddef>

namespace corehost::window
{
    inline constexpr std::size_t default_scrollback_lines = 3000;

    struct console_config final
    {
        std::size_t scrollback_lines{default_scrollback_lines};
    };
}