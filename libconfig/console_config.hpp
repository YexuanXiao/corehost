#pragma once

#include <cstddef>

namespace corehost::window
{
    struct console_config final
    {
        std::size_t scrollback_lines{3000};
    };
}