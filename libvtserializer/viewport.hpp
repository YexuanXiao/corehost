#pragma once

#include "attributes.hpp"
#include "sgr.hpp"
#include "control.hpp"
#include "text.hpp"

#include "screen_buffer.hpp"
#include "console_state.hpp"

#include <algorithm>
#include <cstddef>

namespace corehost::vtserializer
{

// Repaints only rows/columns marked damaged by libscreen. Each row emits one
// CUP, SGR only on attribute changes, and glyph runs are UTF-8 encoded directly
// into the shared serializer buffer.
inline void render_damage(byte_buffer &output, const screen::screen_buffer &buffer, screen::coord cursor,
                          const screen::console_state *state = nullptr,
                          std::size_t trim_trailing_blanks_from = static_cast<std::size_t>(-1),
                          bool include_cursor_visibility = true)
{
    const auto origin = buffer.viewport_origin();
    const auto size = buffer.viewport_size();
    const auto buffer_width = static_cast<std::size_t>(buffer.buffer_size().x);
    const auto buffer_height = buffer.height();

    auto current_hyperlink = screen::no_cell_hyperlink;
    screen::cell_style current_style = static_cast<screen::cell_style>(-1);
    unsigned current_rendition = 0;
    std::int32_t last_rendered_row = -1;
    std::size_t last_rendered_end = 0;
    bool first_rendered_row = true;

    for (std::int32_t row = 0; row < size.y; ++row)
    {
        const auto y = static_cast<std::size_t>(origin.y + row);
        if (y >= buffer_height)
        {
            break;
        }

        auto begin = buffer.damage_begin(y);
        auto end = buffer.damage_end(y);
        begin = std::max(begin, static_cast<std::size_t>(origin.x));
        end = std::min(end, static_cast<std::size_t>(origin.x) + static_cast<std::size_t>(size.x));
        end = std::min(end, buffer_width);
        if (begin >= end)
        {
            continue;
        }

        unsigned rendition = 5;
        if (buffer.is_double_height_top(y))
        {
            rendition = 3;
        }
        else if (buffer.is_double_height_bottom(y))
        {
            rendition = 4;
        }
        else if (buffer.is_double_width(y))
        {
            rendition = 6;
        }
        if (rendition != 5 || current_rendition != 5)
        {
            append_line_rendition(output, rendition);
            current_rendition = rendition;
        }

        const auto column = static_cast<int>(begin - static_cast<std::size_t>(origin.x));
        if (first_rendered_row && state != nullptr && state->terminal_cursor_valid &&
            state->terminal_cursor_y == row && state->terminal_cursor_x == column)
        {
            // already at the first damage cell
        }
        else if (first_rendered_row && state != nullptr && state->terminal_cursor_valid &&
                 state->terminal_cursor_y == row && column == 0)
        {
            output.append_text(u8"\r");
        }
        else
        {
            append_cup(output, static_cast<unsigned>(row), static_cast<unsigned>(column));
        }
        first_rendered_row = false;

        const auto cells = buffer.row_cells(y);
        if (y >= trim_trailing_blanks_from)
        {
            while (end > begin)
            {
                const auto &value = cells[end - 1];
                if (value.is_continuation() || value.primary() != u' ')
                    break;
                --end;
            }
            if (begin >= end)
                continue;
        }
        last_rendered_row = static_cast<std::int32_t>(y);
        last_rendered_end = end;
        for (auto x = begin; x < end; ++x)
        {
            const auto &value = cells[x];
            if (value.is_continuation())
            {
                continue;
            }

            const auto hyperlink = value.hyperlink();
            if (hyperlink != current_hyperlink)
            {
                if (current_hyperlink != screen::no_cell_hyperlink)
                {
                    append_hyperlink_close(output);
                }
                if (hyperlink != screen::no_cell_hyperlink)
                {
                    const auto &link = buffer.hyperlinks().at(hyperlink);
                    append_hyperlink_open_id(output, link.id, link.uri);
                }
                current_hyperlink = hyperlink;
            }

            const auto style = value.style();
            if (style != current_style)
            {
                append_sgr_style(output, buffer.styles().at(style));
                current_style = style;
            }

            if (value.has_cluster())
            {
                const auto cluster = buffer.cluster_of(value);
                append_utf16(output, std::u16string_view{cluster.data(), cluster.size()});
            }
            else
            {
                append_utf16_unit(output, value.primary());
            }
        }
    }

    if (current_hyperlink != screen::no_cell_hyperlink)
    {
        append_hyperlink_close(output);
    }

    if (cursor.y != last_rendered_row || static_cast<std::size_t>(cursor.x) != last_rendered_end)
        append_cup(output, static_cast<unsigned>(cursor.y - origin.y), static_cast<unsigned>(cursor.x - origin.x));
    if (state != nullptr && include_cursor_visibility)
    {
        append_cursor_visibility(output, state->cursor().visible);
    }
}

// Full viewport repaint used when the terminal may have lost local content,
// notably after switching to/from the alternate buffer.
inline void render_viewport(byte_buffer &output, const screen::screen_buffer &buffer, screen::coord cursor,
                            const screen::console_state *state = nullptr, bool include_cursor_visibility = true)
{
    const auto origin = buffer.viewport_origin();
    const auto size = buffer.viewport_size();
    const auto buffer_width = buffer.width();
    const auto buffer_height = buffer.height();
    append_clear_screen(output);

    auto current_hyperlink = screen::no_cell_hyperlink;
    screen::cell_style current_style = static_cast<screen::cell_style>(-1);
    unsigned current_rendition = 0;

    for (std::int32_t row = 0; row < size.y; ++row)
    {
        const auto y = static_cast<std::size_t>(origin.y + row);
        if (y >= buffer_height)
        {
            break;
        }

        unsigned rendition = 5;
        if (buffer.is_double_height_top(y))
        {
            rendition = 3;
        }
        else if (buffer.is_double_height_bottom(y))
        {
            rendition = 4;
        }
        else if (buffer.is_double_width(y))
        {
            rendition = 6;
        }
        if (rendition != 5 || current_rendition != 5)
        {
            append_line_rendition(output, rendition);
            current_rendition = rendition;
        }
        append_cup(output, static_cast<unsigned>(row), 0);

        const auto cells = buffer.row_cells(y);
        const auto origin_x = static_cast<std::size_t>(origin.x);
        const auto columns = std::min<std::size_t>(
            static_cast<std::size_t>(size.x), buffer_width > origin_x ? buffer_width - origin_x : 0);
        for (std::size_t column = 0; column < columns; ++column)
        {
            const auto x = origin_x + column;
            const auto &value = cells[x];
            if (value.is_continuation())
            {
                continue;
            }

            const auto hyperlink = value.hyperlink();
            if (hyperlink != current_hyperlink)
            {
                if (current_hyperlink != screen::no_cell_hyperlink)
                {
                    append_hyperlink_close(output);
                }
                if (hyperlink != screen::no_cell_hyperlink)
                {
                    const auto &link = buffer.hyperlinks().at(hyperlink);
                    append_hyperlink_open_id(output, link.id, link.uri);
                }
                current_hyperlink = hyperlink;
            }

            const auto style = value.style();
            if (style != current_style)
            {
                append_sgr_style(output, buffer.styles().at(style));
                current_style = style;
            }

            if (value.has_cluster())
            {
                const auto cluster = buffer.cluster_of(value);
                append_utf16(output, std::u16string_view{cluster.data(), cluster.size()});
            }
            else
            {
                append_utf16_unit(output, value.primary());
            }
        }
    }

    if (current_hyperlink != screen::no_cell_hyperlink)
    {
        append_hyperlink_close(output);
    }
    append_cup(output, static_cast<unsigned>(cursor.y - origin.y), static_cast<unsigned>(cursor.x - origin.x));
    if (state != nullptr && include_cursor_visibility)
    {
        append_cursor_visibility(output, state->cursor().visible);
    }
}
} // namespace corehost::vtserializer