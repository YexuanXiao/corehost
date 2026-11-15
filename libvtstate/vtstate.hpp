#pragma once

#include "vt_parser.hpp"

#include "console_state.hpp"
#include "screen_buffer.hpp"

#include <libunicode/convert.h>

#include <algorithm>
#include <iterator>
#include <string>

namespace corehost::vtstate
{

// Applies parser text messages to libscreen. The UTF-16 conversion buffer is
// retained across calls so contiguous VT text does not allocate per message.
class state_applier final
{
  public:
    void apply_text(const vtparser::vt_message &message,
                    screen::screen_buffer &buffer,
                    screen::console_state &state)
    {
        text_.clear();
        unicode::convert_to<char16_t>(message.payload.text, std::back_inserter(text_));

        auto cursor = screen::coord{
            static_cast<std::int32_t>(state.cursor().x),
            static_cast<std::int32_t>(state.cursor().y),
        };

        std::size_t consumed = 0;
        for (const auto cluster : screen::grapheme_range<screen::text_measurement::graphemes>{text_})
        {
            auto columns = cluster.width;
            if (columns <= 0)
            {
                columns = 1;
            }
            if (cursor.x >= 0 && static_cast<std::size_t>(cursor.x) + static_cast<std::size_t>(columns) >
                                                     state.width())
            {
                buffer.add_flags(static_cast<std::size_t>(cursor.y), screen::line_flag::wrapped);
                cursor.x = 0;
                ++cursor.y;
            }
            if (cursor.y >= 0 && static_cast<std::size_t>(cursor.y) >= state.height())
            {
                const auto top = state.scroll_top();
                const auto bottom = state.scroll_bottom();
                buffer.scroll_region_up(screen::rectangle{0, top, state.width(), bottom - top + 1},
                                        1, state.default_style);
                cursor.y = static_cast<std::int32_t>(bottom);
            }
            if (!buffer.write_glyph(cursor,
                                std::u16string_view{cluster.text.data(), cluster.text.size()},
                                columns, state.default_style, state.default_hyperlink))
            {
                break;
            }
            cursor.x += columns;
            consumed += cluster.text.size();
        }
        state.set_cursor(static_cast<std::size_t>(cursor.x),
                         static_cast<std::size_t>(cursor.y));
        last_consumed_ = consumed;
    }

    void apply_alternate_buffer(bool enabled,
                                screen::screen_buffer &buffer,
                                screen::console_state &state)
    {
        if (enabled)
        {
            state.save_alt_cursor();
            buffer.set_alternate(true);
        }
        else
        {
            buffer.set_alternate(false);
            (void)state.restore_alt_cursor();
        }
    }

    void apply_cursor_horizontal_absolute(const vtparser::vt_message &message,
                                         screen::console_state &state) noexcept
    {
        state.set_cursor(static_cast<std::size_t>(std::max<int>(message.payload.position.col - 1, 0)),
                         state.cursor().y);
    }

    void apply_cursor_vertical_absolute(const vtparser::vt_message &message,
                                       screen::console_state &state) noexcept
    {
        state.set_cursor(state.cursor().x,
                         static_cast<std::size_t>(std::max<int>(message.payload.position.row - 1, 0)));
    }

    void apply_cursor_relative(const vtparser::vt_message &message,
                             screen::console_state &state,
                             std::ptrdiff_t dx, std::ptrdiff_t dy) noexcept
    {
        const auto count = std::max<int>(message.payload.count.value, 1);
        const auto x = static_cast<std::ptrdiff_t>(state.cursor().x) + dx * count;
        const auto y = static_cast<std::ptrdiff_t>(state.cursor().y) + dy * count;
        state.set_cursor(static_cast<std::size_t>(std::max<std::ptrdiff_t>(x, 0)),
                         static_cast<std::size_t>(std::max<std::ptrdiff_t>(y, 0)));
    }

    void apply_cursor_visibility(bool visible,
                                  screen::console_state &state) noexcept
    {
        state.cursor().visible = visible;
    }

    void apply_save_cursor(screen::console_state &state) noexcept
    {
        state.save_cursor();
    }

    void apply_restore_cursor(screen::console_state &state) noexcept
    {
        (void)state.restore_cursor();
    }

    void apply_palette_color(const vtparser::vt_message &message,
                               screen::console_state &state)
    {
        const auto index = message.payload.palette.index;
        if (index < 0 || index >= 16)
        {
            return;
        }
        state.color_table[static_cast<std::size_t>(index)] =
            (static_cast<std::uint32_t>(message.payload.palette.r) << 16) |
            (static_cast<std::uint32_t>(message.payload.palette.g) << 8) |
            message.payload.palette.b;
    }

    void apply_insert_lines(const vtparser::vt_message &message,
                            screen::screen_buffer &buffer,
                            screen::console_state &state)
    {
        const auto count = static_cast<std::size_t>(std::max<int>(message.payload.count.value, 0));
        const auto y = state.cursor().y;
        const auto bottom = state.scroll_bottom();
        if (count == 0 || y >= bottom)
        {
            return;
        }
        buffer.scroll_region_down(screen::rectangle{0, y, state.width(), bottom - y + 1}, count,
                                 state.default_style);
    }

    void apply_delete_lines(const vtparser::vt_message &message,
                            screen::screen_buffer &buffer,
                            screen::console_state &state)
    {
        const auto count = static_cast<std::size_t>(std::max<int>(message.payload.count.value, 0));
        const auto y = state.cursor().y;
        const auto bottom = state.scroll_bottom();
        if (count == 0 || y >= bottom)
        {
            return;
        }
        buffer.scroll_region_up(screen::rectangle{0, y, state.width(), bottom - y + 1}, count,
                               state.default_style);
    }

    void apply_insert_characters(const vtparser::vt_message &message,
                                  screen::screen_buffer &buffer,
                                  screen::console_state &state)
    {
        const auto count = static_cast<std::size_t>(std::max<int>(message.payload.count.value, 0));
        const auto x = state.cursor().x;
        const auto y = state.cursor().y;
        const auto width = state.width();
        if (count == 0 || x >= width)
        {
            return;
        }
        buffer.copy_rect(screen::rectangle{x, y, width - x, 1}, screen::rectangle{}, false,
                         screen::coord{static_cast<std::int32_t>(x + count), static_cast<std::int32_t>(y)});
        buffer.fill_rect(screen::rectangle{x, y, std::min(count, width - x), 1},
                        screen::cell::blank(state.default_style, state.default_hyperlink));
    }

    void apply_delete_characters(const vtparser::vt_message &message,
                                  screen::screen_buffer &buffer,
                                  screen::console_state &state)
    {
        const auto count = static_cast<std::size_t>(std::max<int>(message.payload.count.value, 0));
        const auto x = state.cursor().x;
        const auto y = state.cursor().y;
        const auto width = state.width();
        if (count == 0 || x >= width)
        {
            return;
        }
        const auto removed = std::min(count, width - x);
        buffer.copy_rect(screen::rectangle{x + removed, y, width - x - removed, 1}, screen::rectangle{}, false,
                         screen::coord{static_cast<std::int32_t>(x), static_cast<std::int32_t>(y)});
        buffer.fill_rect(screen::rectangle{width - removed, y, removed, 1},
                        screen::cell::blank(state.default_style, state.default_hyperlink));
    }

    void apply_sgr(const vtparser::vt_message &message,
                    screen::screen_buffer &buffer,
                    screen::console_state &state)
    {
        const auto &payload = message.payload.sgr;
        auto style = buffer.styles().at(state.default_style);
        if (payload.has(vtparser::vt_sgr_flag::reset))
        {
            style = screen::text_style{};
            state.default_attributes = 0x07;
        }
        if (payload.has(vtparser::vt_sgr_flag::bold))
        {
            state.default_attributes |= 0x08;
        }
        if (payload.clears(vtparser::vt_sgr_flag::bold))
        {
            state.default_attributes &= static_cast<std::uint16_t>(~0x08);
        }
        if (payload.has(vtparser::vt_sgr_flag::underline))
        {
            state.default_attributes |= 0x8000;
        }
        if (payload.clears(vtparser::vt_sgr_flag::underline))
        {
            state.default_attributes &= static_cast<std::uint16_t>(~0x8000);
        }
        if (payload.fg.kind != vtparser::vt_sgr_color_kind::none)
        {
            if (payload.fg.is_default())
            {
                style.foreground = screen::color{};
                state.default_attributes = static_cast<std::uint16_t>((state.default_attributes & 0xFFF0) | 0x07);
            }
            else if (payload.fg.is_indexed())
            {
                style.foreground = screen::color{screen::color_kind::indexed, payload.fg.value};
                state.default_attributes = static_cast<std::uint16_t>((state.default_attributes & 0xFFF0) | ansi_to_bgri(payload.fg.value));
            }
            else
            {
                style.foreground = screen::color{screen::color_kind::rgb,
                                                 (static_cast<std::uint32_t>(payload.fg.value) << 16) |
                                                 (static_cast<std::uint32_t>(payload.fg.g) << 8) |
                                                 payload.fg.b};
            }
        }
        if (payload.bg.kind != vtparser::vt_sgr_color_kind::none)
        {
            if (payload.bg.is_default())
            {
                style.background = screen::color{};
            }
            else if (payload.bg.is_indexed())
            {
                style.background = screen::color{screen::color_kind::indexed, payload.bg.value};
                state.default_attributes = static_cast<std::uint16_t>((state.default_attributes & 0xFF0F) | (ansi_to_bgri(payload.bg.value) << 4));
            }
            else
            {
                style.background = screen::color{screen::color_kind::rgb,
                                                 (static_cast<std::uint32_t>(payload.bg.value) << 16) |
                                                 (static_cast<std::uint32_t>(payload.bg.g) << 8) |
                                                 payload.bg.b};
            }
        }
        style.legacy_attributes = state.default_attributes;
        state.default_style = buffer.styles().intern(style);
    }

    static std::uint16_t ansi_to_bgri(std::uint8_t index) noexcept
    {
        const auto low = static_cast<std::uint16_t>(index & 0x0F);
        return static_cast<std::uint16_t>((low & 0x01 ? 0x04 : 0) | (low & 0x02 ? 0x02 : 0) |
                                          (low & 0x04 ? 0x01 : 0) | (low & 0x08 ? 0x08 : 0));
    }

    void apply_semantic_prompt(const vtparser::vt_message &message,
                              screen::screen_buffer &buffer,
                              const screen::console_state &state)
    {
        const auto y = static_cast<std::size_t>(state.cursor().y);
        if (message.payload.semantic.kind == u'A')
        {
            buffer.add_flags(y, screen::line_flag::prompt_end);
            return;
        }
        if (message.payload.semantic.kind != u'D')
        {
            return;
        }

        buffer.add_flags(y, screen::line_flag::command_end);
        std::int32_t exit_code = 0;
        for (const char32_t ch : message.payload.semantic.argument)
        {
            if (ch < U'0' || ch > U'9')
            {
                break;
            }
            exit_code = exit_code * 10 + static_cast<std::int32_t>(ch - U'0');
        }
        buffer.set_command_exit_code(y, exit_code);
    }

    void apply_erase_in_display(const vtparser::vt_message &message,
                                screen::screen_buffer &buffer,
                                const screen::console_state &state)
    {
        const auto x = state.cursor().x;
        const auto y = state.cursor().y;
        const auto width = state.width();
        const auto height = state.height();
        const auto blank = screen::cell::blank(state.default_style, state.default_hyperlink);
        switch (message.payload.erase_mode)
        {
        case 0:
            buffer.fill_rect(screen::rectangle{x, y, width - x, 1}, blank);
            if (y + 1 < height)
            {
                buffer.fill_rect(screen::rectangle{0, y + 1, width, height - y - 1}, blank);
            }
            break;
        case 1:
            if (y != 0)
            {
                buffer.fill_rect(screen::rectangle{0, 0, width, y}, blank);
            }
            buffer.fill_rect(screen::rectangle{0, y, x + 1, 1}, blank);
            break;
        case 2:
        case 3:
            buffer.fill_rect(screen::rectangle{0, 0, width, height}, blank);
            break;
        default:
            break;
        }
    }

    void apply_erase_in_line(const vtparser::vt_message &message,
                             screen::screen_buffer &buffer,
                             const screen::console_state &state)
    {
        const auto x = state.cursor().x;
        const auto y = state.cursor().y;
        const auto width = state.width();
        std::size_t first = 0;
        std::size_t count = width;
        switch (message.payload.erase_mode)
        {
        case 0:
            first = x;
            count = width - x;
            break;
        case 1:
            count = x + 1;
            break;
        case 2:
            break;
        default:
            return;
        }
        buffer.fill_rect(screen::rectangle{first, y, count, 1},
                         screen::cell::blank(state.default_style, state.default_hyperlink));
    }

    void apply_hyperlink(const vtparser::vt_message &message,
                         screen::screen_buffer &buffer,
                         screen::console_state &state)
    {
        if (message.payload.hyperlink.uri.empty())
        {
            state.default_hyperlink = screen::no_cell_hyperlink;
            return;
        }

        uri_.clear();
        id_.clear();
        unicode::convert_to<char16_t>(message.payload.hyperlink.uri, std::back_inserter(uri_));
        unicode::convert_to<char16_t>(message.payload.hyperlink.id, std::back_inserter(id_));
        state.default_hyperlink = buffer.hyperlinks().intern(uri_, id_);
    }

    void apply_line_feed(screen::screen_buffer &buffer,
                           screen::console_state &state)
    {
        const auto x = state.cursor().x;
        const auto y = state.cursor().y;
        if (y + 1 < state.height())
        {
            state.set_cursor(x, y + 1);
            return;
        }

        const auto top = state.scroll_top();
        const auto bottom = state.scroll_bottom();
        buffer.scroll_region_up(
            screen::rectangle{0, top, state.width(), bottom - top + 1},
            1, state.default_style);
        state.set_cursor(x, bottom);
    }

    void apply_cursor_position(const vtparser::vt_message &message,
                              screen::console_state &state)
    {
        const auto row = static_cast<std::size_t>(std::max<int>(message.payload.position.row - 1, 0));
        const auto column = static_cast<std::size_t>(std::max<int>(message.payload.position.col - 1, 0));
        state.set_cursor(column, row);
    }

    void apply_line_rendition(const vtparser::vt_message &message,
                              screen::screen_buffer &buffer,
                              const screen::console_state &state)
    {
        const auto y = static_cast<std::size_t>(state.cursor().y);
        auto flags = buffer.flags(y);
        constexpr auto rendition_mask = static_cast<screen::line_flags>(
            screen::line_flag_ops::raw(screen::line_flag::double_width) |
            screen::line_flag_ops::raw(screen::line_flag::double_height_top) |
            screen::line_flag_ops::raw(screen::line_flag::double_height_bottom));
        flags &= ~rendition_mask;

        switch (message.payload.line_rendition)
        {
        case 3:
            flags |= screen::line_flag_ops::raw(screen::line_flag::double_height_top);
            break;
        case 4:
            flags |= screen::line_flag_ops::raw(screen::line_flag::double_height_bottom);
            break;
        case 6:
            flags |= screen::line_flag_ops::raw(screen::line_flag::double_width);
            break;
        case 5:
        default:
            break;
        }
        buffer.set_flags(y, flags);
    }

    [[nodiscard]] std::size_t last_consumed() const noexcept
    {
        return last_consumed_;
    }

  private:
    std::u16string text_;
    std::u16string uri_;
    std::u16string id_;
    std::size_t last_consumed_{};
};

} // namespace corehost::vtstate