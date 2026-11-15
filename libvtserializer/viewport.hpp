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
                          const screen::console_state *state = nullptr)
{
    const auto origin = buffer.viewport_origin();
    const auto size = buffer.viewport_size();
    const auto buffer_width = static_cast<std::size_t>(buffer.buffer_size().x);

    auto current_hyperlink = screen::no_cell_hyperlink;

    for (std::int32_t row = 0; row < size.y; ++row)
    {
        const auto y = static_cast<std::size_t>(origin.y + row);
        if (y >= buffer.height())
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
        append_line_rendition(output, rendition);

        append_cup(output, static_cast<unsigned>(row),
                   static_cast<unsigned>(begin - static_cast<std::size_t>(origin.x)));

        screen::cell_style current_style = static_cast<screen::cell_style>(-1);
        for (auto x = begin; x < end; ++x)
        {
            const auto &value = buffer.at(x, y);
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
                const char16_t primary = value.primary();
                append_utf16(output, std::u16string_view{&primary, 1});
            }
        }
    }

    if (current_hyperlink != screen::no_cell_hyperlink)
    {
        append_hyperlink_close(output);
    }

    append_cup(output, static_cast<unsigned>(cursor.y - origin.y), static_cast<unsigned>(cursor.x - origin.x));
    if (state != nullptr)
    {
        append_cursor_visibility(output, state->cursor().visible);
        append_cursor_shape(output, static_cast<unsigned>(state->cursor().style), false);
    }
}

// Full viewport repaint used when the terminal may have lost local content,
// notably after switching to/from the alternate buffer.
inline void render_viewport(byte_buffer &output, const screen::screen_buffer &buffer, screen::coord cursor,
                            const screen::console_state *state = nullptr)
{
    const auto origin = buffer.viewport_origin();
    const auto size = buffer.viewport_size();
    append_clear_screen(output);

    auto current_hyperlink = screen::no_cell_hyperlink;
    screen::cell_style current_style = static_cast<screen::cell_style>(-1);

    for (std::int32_t row = 0; row < size.y; ++row)
    {
        const auto y = static_cast<std::size_t>(origin.y + row);
        if (y >= buffer.height())
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
        append_line_rendition(output, rendition);
        append_cup(output, static_cast<unsigned>(row), 0);

        for (std::int32_t column = 0; column < size.x; ++column)
        {
            const auto x = static_cast<std::size_t>(origin.x + column);
            if (x >= buffer.width())
            {
                break;
            }
            const auto &value = buffer.at(x, y);
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
                const char16_t primary = value.primary();
                append_utf16(output, std::u16string_view{&primary, 1});
            }
        }
    }

    if (current_hyperlink != screen::no_cell_hyperlink)
    {
        append_hyperlink_close(output);
    }
    append_cup(output, static_cast<unsigned>(cursor.y - origin.y), static_cast<unsigned>(cursor.x - origin.x));
    if (state != nullptr)
    {
        append_cursor_visibility(output, state->cursor().visible);
        append_cursor_shape(output, static_cast<unsigned>(state->cursor().style), false);
    }
}
} // namespace corehost::vtserializer