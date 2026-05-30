#pragma once
#include "console_state.hpp"
#include "pipe_bridge.hpp"
#include "screen_buffer.hpp"

namespace conpty
{

inline void render_visible_viewport(console_state &state, screen_buffer &sb, pipe_bridge &bridge)
{
    sb.viewport.clamp_to_buffer(sb.size);
    state.clamp_cursor_to_buffer();

    bridge.vt_write_clear_screen();
    bridge.vt_write_attr(state.default_attributes);

    WORD last_attr = 0xFFFF;
    const auto view = sb.viewport.rect();
    for (SHORT y = view.Top; y <= view.Bottom && y < sb.size.Y; ++y)
    {
        bridge.vt_write_cup(static_cast<SHORT>(y - view.Top), 0);
        for (SHORT x = view.Left; x <= view.Right && x < sb.size.X; ++x)
        {
            const WORD cell_attr = sb.attr_at({x, y});
            if (cell_attr != last_attr)
            {
                bridge.vt_write_attr(cell_attr);
                last_attr = cell_attr;
            }
            bridge.vt_write_cell(sb.at_u32({x, y}));
        }
    }

    bridge.vt_write_cup_buffer(state.cursor.position);
    bridge.vt_flush();
}

} // namespace conpty
