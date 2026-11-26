#pragma once

#include "console_theme.hpp"
#include "text_options.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

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
    struct unicode_range final
    {
        std::uint32_t first{};
        std::uint32_t last{};
    };

    struct font_fallback final
    {
        std::wstring family;
        unicode_range range{0, 0x10FFFF};
    };

    inline constexpr std::size_t max_font_fallbacks = 12;

    struct font_fallback_list final
    {
        std::array<font_fallback, max_font_fallbacks> entries{};
        std::size_t count{};
    };

    inline font_fallback_list default_font_fallbacks(uitext::font_family font = uitext::default_font_family)
    {
        font_fallback_list list;
        const auto mono = font == uitext::font_family::cascadia_mono;
        list.entries[0] = font_fallback{mono ? L"Cascadia Mono" : L"Cascadia Code"};
        list.entries[1] = font_fallback{mono ? L"Cascadia Code" : L"Cascadia Mono"};
        list.entries[2] = font_fallback{L"Consolas"};
        list.count = 3;
        return list;
    }

    inline constexpr float default_font_size_dip = 16.0f;
    inline constexpr std::uint32_t default_cursor_blink_ms = 530;
    inline constexpr std::uint32_t default_selection_auto_scroll_interval_ms = 40;
    inline constexpr std::size_t default_selection_auto_scroll_edge_rows = 1;
    inline constexpr bool default_uia_text_changed_events = true;
    inline constexpr bool default_uia_selection_events = true;
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
        float font_size_dip{default_font_size_dip};
        std::uint32_t cursor_blink_ms{default_cursor_blink_ms};
        std::uint32_t selection_auto_scroll_interval_ms{default_selection_auto_scroll_interval_ms};
        std::size_t selection_auto_scroll_edge_rows{default_selection_auto_scroll_edge_rows};
        bool uia_text_changed_events{default_uia_text_changed_events};
        bool uia_selection_events{default_uia_selection_events};
        font_fallback_list font_fallbacks{default_font_fallbacks(text_options.font)};
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