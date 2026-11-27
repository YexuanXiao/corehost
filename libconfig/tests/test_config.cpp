#include "config.hpp"

#include <cstdio>
#include <cstdlib>

int main()
{
    using namespace corehost::window;

    static_assert(default_scrollback_lines == 3000);
    static_assert(default_initial_columns == 120);
    static_assert(default_initial_rows == 30);
    static_assert(default_min_columns == 36);
    static_assert(default_min_rows == 2);
    static_assert(default_cursor_blink_ms == 530);
    static_assert(default_mouse_wheel_lines == 3);
    static_assert(default_uia_enabled);
    static_assert(default_tsf_enabled);

    const corehost_config config{};

    if (config.console.initial_cols != 120 ||
        config.console.initial_rows != 30 || config.console.ambiguous_is_wide)
    {
        std::fprintf(stderr, "console defaults mismatch\n");
        return EXIT_FAILURE;
    }

    if (config.window.initial_columns != 120 || config.window.initial_rows != 30 ||
        config.window.min_columns != 36 || config.window.min_rows != 2 ||
        config.window.scrollbar != scrollbar_visibility::auto_hide ||
        config.window.scrollback_lines != default_scrollback_lines ||
        config.window.copy_ending != copy_line_ending::crlf || config.window.font_fallbacks.count != 1 ||
        config.window.font_fallbacks.entries[0].family != L"Cascadia Mono" ||
        config.window.mouse_wheel_lines != 3 || config.window.right_click != right_click_action::automatic ||
        config.mouse.scroll_multiplier != 3 || config.mouse.right_click != right_click_action::automatic ||
        !config.cursor.blink || config.cursor.blink_rate != 530 ||
        config.window.cursor_shape != cursor_style::automatic)
    {
        std::fprintf(stderr, "window defaults mismatch\n");
        return EXIT_FAILURE;
    }

    if (config.window.uia_enabled != default_uia_enabled || config.window.tsf_enabled != default_tsf_enabled ||
        config.accessibility.uia_enable != default_uia_enabled || config.ime.tsf_enabled != default_tsf_enabled)
    {
        std::fprintf(stderr, "accessibility/IME defaults mismatch\n");
        return EXIT_FAILURE;
    }

    if (config.color.mode != theme_mode::dark ||
        config.color.light.ansi[7].r != 0xB3 || config.color.light.ansi[7].g != 0xB3 ||
        config.color.light.ansi[7].b != 0xB3 || config.color.light.ansi[15].r != 0xDB ||
        config.color.light.ansi[15].g != 0xDB || config.color.light.ansi[15].b != 0xDB)
    {
        std::fprintf(stderr, "color defaults mismatch\n");
        return EXIT_FAILURE;
    }

    if (config.clipboard.read_policy != clipboard_policy::deny ||
        config.clipboard.write_policy != clipboard_policy::deny ||
        config.clipboard.selection_policy != clipboard_policy::deny || config.clipboard.paste_max_size != 0)
    {
        std::fprintf(stderr, "clipboard defaults mismatch\\n");
        return EXIT_FAILURE;
    }

    if (clipboard_read_allowed(config.clipboard) || clipboard_write_allowed(config.clipboard) ||
        !clipboard_paste_fits(config.clipboard, 20, 10) ||
        clipboard_paste_fits(config.clipboard, 21, 10))
    {
        std::fprintf(stderr, "clipboard policy helpers mismatch\n");
        return EXIT_FAILURE;
    }

    auto allowed_clipboard = config.clipboard;
    allowed_clipboard.read_policy = clipboard_policy::allow;
    allowed_clipboard.write_policy = clipboard_policy::allow;
    allowed_clipboard.paste_max_size = 4;
    if (!clipboard_read_allowed(allowed_clipboard) || !clipboard_write_allowed(allowed_clipboard) ||
        !clipboard_paste_fits(allowed_clipboard, 4, 0) ||
        clipboard_paste_fits(allowed_clipboard, 5, 0))
    {
        std::fprintf(stderr, "clipboard allow helpers mismatch\n");
        return EXIT_FAILURE;
    }

    if (config.font.family_fallback_list.count != 1 ||
        config.font.family_fallback_list.entries[0].family != L"Cascadia Mono" ||
        config.font.size != 16.0f || !config.font.color_emoji || config.font.ligatures ||
        config.font.variation[0].tag != corehost::uitext::font_axis_weight || config.font.variation[0].value != 400.0f ||
        config.font.variation_bold[0].value != 700.0f || config.font.variation_italic[2].value != 1.0f ||
        config.font.turning != font_turning::gamma_contrast ||
        config.font.anti_alias != corehost::uitext::font_anti_alias::grayscale ||
        config.font.rendering_mode != DWRITE_RENDERING_MODE_DEFAULT || config.mouse.selection_mode != mouse_selection_mode::line ||
        config.mouse.right_click != right_click_action::automatic || config.cursor.style != cursor_style::automatic ||
        !config.mouse.selection_double_click || !config.mouse.selection_triple_click ||
        config.mouse.shift_capture != mouse_shift_capture::shift ||
        config.mouse.selection_copy_on_select || config.mouse.selection_trim_block || config.mouse.middle_click_paste ||
        config.title.force_corehost || config.accessibility.reduce_motion ||
        config.window_behavior.launch != launch_mode::automatic ||
        config.window_behavior.blur != backdrop_mode::none || config.window_behavior.inherit_working_directory || !config.graphics.sixel || config.text.bold_is_bright || config.text.blink_enable || config.text.underline != text_underline_style::single ||
        config.text.strikethrough != text_strikethrough_style::single ||
        config.bell.mode != bell_mode_full::visual)
    {
        std::fprintf(stderr, "font/mouse defaults mismatch\n");
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
