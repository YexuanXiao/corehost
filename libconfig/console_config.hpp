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
    inline constexpr int default_start_acp = 65001;
    inline constexpr std::size_t default_initial_columns = 120;
    inline constexpr std::size_t default_initial_rows = 30;
    inline constexpr bool default_ambiguous_is_wide = false;
    inline constexpr copy_line_ending default_copy_line_ending = copy_line_ending::crlf;

    struct console_config final
    {
        std::size_t scrollback_lines{default_scrollback_lines};
        copy_line_ending copy_ending{default_copy_line_ending};
        int start_acp{default_start_acp};
        std::size_t initial_cols{default_initial_columns};
        std::size_t initial_rows{default_initial_rows};
        bool ambiguous_is_wide{default_ambiguous_is_wide};
    };
}