#pragma once

#include <cstddef>

namespace corehost::window
{
    enum class copy_line_ending : unsigned char
    {
        crlf,
        lf,
    };

    inline constexpr std::size_t default_scrollback_lines = 3000;
    inline constexpr copy_line_ending default_copy_line_ending = copy_line_ending::crlf;

    struct console_config final
    {
        std::size_t scrollback_lines{default_scrollback_lines};
        copy_line_ending copy_ending{default_copy_line_ending};
    };
}