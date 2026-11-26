#pragma once

#include "console_theme.hpp"
#include "text_options.hpp"

#include <cstddef>
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

    enum class hyperlink_modifier : unsigned char
    {
        none,
        control,
        alt,
        shift,
    };

    inline constexpr bell_mode default_bell_mode = bell_mode::visual;
    inline constexpr std::uint8_t default_opacity = 255;
    inline constexpr bool default_dark_mode = true;
    inline constexpr client_exit_policy default_exit_policy = client_exit_policy::always_exit;
    inline constexpr std::size_t default_initial_columns = 120;
    inline constexpr std::size_t default_initial_rows = 30;
    inline constexpr std::size_t default_min_columns = 36;
    inline constexpr std::size_t default_min_rows = 2;
    inline constexpr std::int32_t default_candidate_offset_cells = 2;
    inline constexpr hyperlink_modifier default_hyperlink_modifier = hyperlink_modifier::control;

    enum class backdrop_mode : unsigned char
    {
        none,
        mica,
        acrylic,
    };

    inline constexpr backdrop_mode default_backdrop_mode = backdrop_mode::none;
    inline constexpr console_palette default_palette = console_palette::campbell();

    // Window/display behavior. File loading is intentionally not implemented.
    struct window_config final
    {
        console_palette palette{default_palette};
        bell_mode bell{default_bell_mode};
        uitext::text_options text_options{};
        backdrop_mode backdrop{default_backdrop_mode};
        std::uint8_t opacity{default_opacity};
        bool dark_mode{default_dark_mode};
        client_exit_policy exit_policy{default_exit_policy};
        std::int32_t candidate_offset_cells{default_candidate_offset_cells};
        std::size_t initial_columns{default_initial_columns};
        std::size_t initial_rows{default_initial_rows};
        std::size_t min_columns{default_min_columns};
        std::size_t min_rows{default_min_rows};
        hyperlink_modifier hyperlink{default_hyperlink_modifier};
    };
}