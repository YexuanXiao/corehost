#pragma once

#include "console_config.hpp"
#include "console_theme.hpp"
#include "text_options.hpp"
#include "window_config.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

// Complete configuration schema derived from 配置.txt. The active window/console
// objects remain the compatibility subset used by the current implementation;
// corehost_config aggregates them with the remaining documented groups.
namespace corehost::window
{
enum class theme_mode : unsigned char
{
    light,
    dark,
    auto_mode,
};

enum class selection_foreground : unsigned char
{
    background,
    reverse,
};

enum class cursor_text_mode : unsigned char
{
    background,
    reverse,
};

enum class text_underline_style : unsigned char
{
    none,
    single,
    double_underline,
    curl,
    dotted,
    dashed,
};

enum class text_strikethrough_style : unsigned char
{
    none,
    single,
    double_underline,
};

enum class launch_mode : unsigned char
{
    automatic,
    maximized,
    fullscreen,
};

enum class mouse_selection_mode : unsigned char
{
    line,
    block,
    disable,
};

enum class mouse_shift_capture : unsigned char
{
    never,
    always,
    shift,
};

enum class bell_mode_full : unsigned char
{
    none,
    auditory,
    visual,
    window,
    taskbar,
};

enum class clipboard_policy : unsigned char
{
    deny,
    allow,
};

enum class clipboard_format : unsigned char
{
    text,
    html,
    rtf,
};

enum class hyperlink_open_policy : unsigned char
{
    allow,
    deny,
};

enum class antialias_mode : unsigned char
{
    grayscale,
    cleartype,
    aliased,
};

enum class colorspace_mode : unsigned char
{
    srgb,
    bt2020,
};

enum class progress_display : unsigned char
{
    none,
    tab,
    taskbar,
};

enum class graphics_file_policy : unsigned char
{
    deny,
    allow,
};

struct color_scheme final
{
    console_color foreground{};
    console_color background{};
    console_color selection_background{};
    selection_foreground selection_foreground_mode{selection_foreground::background};
    console_color underline{};
    console_color cursor{};
    cursor_text_mode cursor_text{};
    console_color hyperlink{};
    console_color scrollbar{};
    console_color scrollbar_background{};
    std::array<console_color, 16> ansi{};
    float faint_opacity{0.5f};
};

struct color_config final
{
    theme_mode mode{theme_mode::auto_mode};
    color_scheme light{};
    color_scheme dark{};
};

struct background_config final
{
    float opacity{1.0f};
    std::wstring image;
    float image_opacity{1.0f};
};

struct cursor_config final
{
    cursor_style style{cursor_style::automatic};
    bool blink{true};
    std::uint32_t blink_rate{530};
};

struct font_config final
{
    font_fallback_list family_fallback_list{default_font_fallbacks()};
    float size{16.0f};
    bool ligatures{};
    std::array<float, 5> variation{};
    std::array<float, 5> variation_bold{};
    std::array<float, 5> variation_italic{};
    std::array<float, 5> variation_bold_italic{};
    antialias_mode antialias{antialias_mode::grayscale};
    bool color_emoji{true};
};

struct text_style_config final
{
    bool bold_is_bright{};
    text_underline_style underline{text_underline_style::single};
    text_strikethrough_style strikethrough{text_strikethrough_style::single};
    bool blink_enable{};
};

struct window_behavior_config final
{
    bool shadow{true};
    backdrop_mode blur{backdrop_mode::none};
    launch_mode launch{launch_mode::automatic};
    int position_x{};
    int position_y{};
    bool always_on_top{};
    bool inherit_working_directory{};
    bool allow_move_resize{true};
};

struct mouse_config final
{
    bool hide_while_typing{};
    mouse_shift_capture shift_capture{mouse_shift_capture::shift};
    int scroll_multiplier{1};
    mouse_selection_mode selection_mode{mouse_selection_mode::line};
    bool selection_copy_on_select{};
    bool selection_double_click{true};
    bool selection_triple_click{true};
    bool selection_trim_block{};
    right_click_action right_click{right_click_action::automatic};
    bool middle_click_paste{};
};

struct clipboard_config final
{
    clipboard_policy read_policy{clipboard_policy::deny};
    clipboard_policy write_policy{clipboard_policy::deny};
    clipboard_policy selection_policy{clipboard_policy::deny};
    std::size_t paste_max_size{};
    clipboard_format copy_format{clipboard_format::text};
    bool large_paste_warning{};
    bool multi_line_paste_confirm{};
    bool bracketed_paste{true};
};

inline bool clipboard_read_allowed(const clipboard_config &config) noexcept
{
    return config.read_policy == clipboard_policy::allow;
}

inline bool clipboard_write_allowed(const clipboard_config &config) noexcept
{
    return config.write_policy == clipboard_policy::allow;
}

inline bool clipboard_paste_fits(const clipboard_config &config, std::size_t bytes, std::size_t cells) noexcept
{
    const auto limit = config.paste_max_size == 0 ? cells * sizeof(char16_t) : config.paste_max_size;
    return bytes <= limit;
}

struct hyperlink_config final
{
    bool enable{true};
    text_underline_style underline{text_underline_style::single};
    hyperlink_open_policy open_policy{hyperlink_open_policy::allow};
    bool ctrl_click{true};
    hyperlink_modifier modifier{default_hyperlink_modifier};
};

struct title_config final
{
    bool force_corehost{};
};

struct scrollback_config final
{
    int lines{3000};
    bool smooth{};
    bool scroll_on_output{true};
    bool scroll_to_bottom_on_input{true};
    std::uint32_t animation_duration{};
    float width{16.0f};
};

struct bell_config final
{
    bell_mode_full mode{bell_mode_full::visual};
};

struct notification_config final
{
    bool enable{};
    clipboard_policy policy{clipboard_policy::deny};
    bool sound{};
    bool only_when_unfocused{true};
};

struct progress_config final
{
    progress_display display{progress_display::none};
    bool allow_program{};
};

struct term_config final
{
    std::wstring name{L"corehost"};
    std::wstring colorterm{L"true"};
    std::wstring program{L"corehost"};
    std::wstring program_version{};
};

struct graphics_config final
{
    bool sixel{true};
    bool kitty{};
    bool iterm2{};
    bool unicode_placeholder{};
    bool confirm_large_image{};
    graphics_file_policy file_path_policy{graphics_file_policy::deny};
};

struct render_config final
{
    std::uint32_t frame_rate_limit{};
    colorspace_mode colorspace{colorspace_mode::srgb};
    bool retro_effect{};
};

struct ime_config final
{
    bool tsf_enabled{true};
    std::int32_t candidate_offset_cells{2};
    bool message_pump{};
};

struct accessibility_config final
{
    bool uia_enable{true};
    int high_contrast{};
    bool reduce_motion{};
};

inline color_scheme campbell_scheme()
{
    color_scheme result;
    const auto palette = console_palette::campbell();
    result.ansi = palette.indexed;
    result.foreground = palette.default_foreground;
    result.background = palette.default_background;
    result.selection_background = console_color{0x26, 0x4F, 0x78};
    result.underline = palette.default_foreground;
    result.cursor = palette.default_foreground;
    result.hyperlink = console_color{0x3B, 0x78, 0xFF};
    result.scrollbar = console_color{0x9A, 0x9A, 0x9A};
    result.scrollbar_background = console_color{0x30, 0x30, 0x30};
    return result;
}

inline color_scheme one_half_light_scheme()
{
    color_scheme result;
    result.foreground = console_color{0x38, 0x3A, 0x42};
    result.background = console_color{0xFA, 0xFA, 0xFA};
    result.selection_background = console_color{0xD0, 0xD0, 0xD0};
    result.underline = result.foreground;
    result.cursor = console_color{0x52, 0x6F, 0xFF};
    result.hyperlink = console_color{0x01, 0x84, 0xBC};
    result.scrollbar = console_color{0x80, 0x80, 0x80};
    result.scrollbar_background = console_color{0xE0, 0xE0, 0xE0};
    result.ansi = {
        console_color{0x38, 0x3A, 0x42}, console_color{0xE4, 0x56, 0x49},
        console_color{0x50, 0xA1, 0x4F}, console_color{0xC1, 0x84, 0x01},
        console_color{0x01, 0x84, 0xBC}, console_color{0xA6, 0x26, 0xA4},
        console_color{0x09, 0x97, 0xB3}, console_color{0xB3, 0xB3, 0xB3},
        console_color{0x4F, 0x52, 0x5E}, console_color{0xE0, 0x6C, 0x75},
        console_color{0x98, 0xC3, 0x79}, console_color{0xE5, 0xC0, 0x7B},
        console_color{0x61, 0xAF, 0xEF}, console_color{0xC6, 0x78, 0xDD},
        console_color{0x56, 0xB6, 0xC2}, console_color{0xDB, 0xDB, 0xDB},
    };
    return result;
}

inline corehost::window::color_config default_color_config()
{
    return color_config{
        .mode = theme_mode::dark,
        .light = one_half_light_scheme(),
        .dark = campbell_scheme(),
    };
}

struct corehost_config final
{
    console_config console;
    window_config window;
    color_config color{default_color_config()};
    background_config background;
    cursor_config cursor;
    font_config font;
    text_style_config text;
    window_behavior_config window_behavior;
    mouse_config mouse;
    clipboard_config clipboard;
    hyperlink_config hyperlink;
    title_config title;
    scrollback_config scrollback;
    bell_config bell;
    notification_config notification;
    progress_config progress;
    term_config term;
    graphics_config graphics;
    render_config render;
    ime_config ime;
    accessibility_config accessibility;
};

using config = corehost_config;

} // namespace corehost::window
