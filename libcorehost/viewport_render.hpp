#pragma once
#include "console_state.hpp"
#include "pipe_bridge.hpp"
#include "screen_buffer.hpp"

namespace conpty
{

// 将当前 active screen buffer 的可见 viewport 完整重绘到终端。
// 该函数用于 alternate-buffer 切换等“终端内容可能已丢失”的路径；它不改变
// screen_buffer 内容，只发 VT 清屏、属性、文本和最终光标位置。
inline void render_visible_viewport(console_state &state, screen_buffer &sb, pipe_bridge &bridge)
{
    // 重绘前先把 viewport/cursor 钳制到当前 buffer，避免生成越界 CUP。
    sb.viewport.clamp_to_buffer(sb.size);
    state.clamp_cursor_to_buffer();

    bridge.vt_write_clear_screen();
    bridge.vt_write_attr(state.default_attributes);

    WORD last_attr = 0xFFFF;
    const auto view = sb.viewport.rect();
    for (SHORT y = view.Top; y <= view.Bottom && y < sb.size.Y; ++y)
    {
        // 每行显式移动到 viewport-relative 行首，避免依赖终端自动换行状态。
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

    // 还原到 Console API 认为的当前 cursor，而不是停在 snapshot 最后一格。
    bridge.vt_write_cup_buffer(state.cursor.position);
    bridge.vt_flush();
}

} // namespace conpty
