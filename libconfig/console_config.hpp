#pragma once

#include <cstddef>

namespace corehost::window
{
    inline constexpr int default_start_acp = 65001;
    inline constexpr std::size_t default_initial_columns = 120;
    inline constexpr std::size_t default_initial_rows = 30;
    inline constexpr bool default_ambiguous_is_wide = false;

    struct console_config final
    {
        int start_acp{default_start_acp};
        std::size_t initial_cols{default_initial_columns};
        std::size_t initial_rows{default_initial_rows};
        bool ambiguous_is_wide{default_ambiguous_is_wide};
    };
}