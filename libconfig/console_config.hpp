#pragma once

#include <cstddef>

namespace corehost::window
{
    enum class text_measurement_mode : unsigned char
    {
        console = 0,
        graphemes = 1,
        wcswidth = 2,
    };

    inline constexpr text_measurement_mode default_text_measurement = text_measurement_mode::graphemes;
    inline constexpr int default_start_acp = 65001;
    inline constexpr std::size_t default_initial_columns = 120;
    inline constexpr std::size_t default_initial_rows = 30;
    inline constexpr bool default_ambiguous_is_wide = false;

    struct console_config final
    {
        text_measurement_mode text_measurement{default_text_measurement};
        int start_acp{default_start_acp};
        std::size_t initial_cols{default_initial_columns};
        std::size_t initial_rows{default_initial_rows};
        bool ambiguous_is_wide{default_ambiguous_is_wide};
    };
}