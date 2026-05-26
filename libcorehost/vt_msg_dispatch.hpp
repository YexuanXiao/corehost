// ── conpty/vt_msg_dispatch.hpp ─────────────────────
// vt_message 分发: 状态应用 (console_state + screen_buffer)
//
// vt_msg_apply_state(id, msg, state, sb):
//   将 vt_message 反映到控制台状态和屏幕缓冲区中。
//   这是 "VT 消息双向驱动" 的一半——PTY 输出侧由 pipe_bridge::vt_msg_send() 负责。
//
// 映射表:
//   cursor_position     → state.cursor.position = (col-1, row-1)
//   cursor_show/hide    → state.cursor.visible
//   sgr                 → state.default_attributes (bool→WORD 反向映射)
//   text               → sb.write at cursor, advance cursor
//   save_cursor         → snapshot cursor → state.decsc_cursor
//   restore_cursor      → state.decsc_cursor → cursor
//   set_scrolling_region→ store margins
//   insert_lines        → sb scroll down at cursor
//   delete_lines        → sb scroll up at cursor
//   scroll_up/down      → sb scroll
//   erase_in_display    → sb clear region
//   erase_in_line       → sb clear line
//   set_window_title    → state.title = msg.title
//   cursor_horiz_absolute→ state.cursor.position.X = col-1
//   cursor_up/down/...  → adjust cursor
//   其他               → 无状态影响
#pragma once
#include <windows.h>
#include "conpty_vt_parser.hpp"
#include "console_state.hpp"
#include "screen_buffer.hpp"
#include "char_convert.hpp"
#include "char_width.hpp"

namespace conpty
{

inline void vt_msg_apply_state(vt_message_id id, const vt_message &msg, console_state &state, screen_buffer &sb)
{
    switch (id)
    {
    // ── 光标定位 ──
    case vt_message_id::cursor_position:
        state.cursor.position.X = static_cast<SHORT>(msg.col - 1);
        state.cursor.position.Y = static_cast<SHORT>(msg.row - 1);
        if (state.cursor.position.X < 0)
            state.cursor.position.X = 0;
        if (state.cursor.position.X >= state.screen_buffer_size.X)
            state.cursor.position.X = state.screen_buffer_size.X - 1;
        if (state.cursor.position.Y < 0)
            state.cursor.position.Y = 0;
        if (state.cursor.position.Y >= state.screen_buffer_size.Y)
            state.cursor.position.Y = state.screen_buffer_size.Y - 1;
        break;

    case vt_message_id::cursor_horiz_absolute:
        state.cursor.position.X = static_cast<SHORT>(msg.col - 1);
        if (state.cursor.position.X < 0)
            state.cursor.position.X = 0;
        if (state.cursor.position.X >= state.screen_buffer_size.X)
            state.cursor.position.X = state.screen_buffer_size.X - 1;
        break;

    case vt_message_id::cursor_vert_absolute:
        state.cursor.position.Y = static_cast<SHORT>(msg.row - 1);
        if (state.cursor.position.Y < 0)
            state.cursor.position.Y = 0;
        if (state.cursor.position.Y >= state.screen_buffer_size.Y)
            state.cursor.position.Y = state.screen_buffer_size.Y - 1;
        break;

    case vt_message_id::cursor_up:
        state.cursor.position.Y -= msg.count;
        if (state.cursor.position.Y < 0)
            state.cursor.position.Y = 0;
        break;

    case vt_message_id::cursor_down:
        state.cursor.position.Y += msg.count;
        if (state.cursor.position.Y >= state.screen_buffer_size.Y)
            state.cursor.position.Y = state.screen_buffer_size.Y - 1;
        break;

    case vt_message_id::cursor_forward:
        state.cursor.position.X += msg.count;
        if (state.cursor.position.X >= state.screen_buffer_size.X)
            state.cursor.position.X = state.screen_buffer_size.X - 1;
        break;

    case vt_message_id::cursor_forward_tab:
        state.cursor.position.X = state.next_tab_stop(state.cursor.position.X);
        break;

    case vt_message_id::cursor_backward:
        state.cursor.position.X -= msg.count;
        if (state.cursor.position.X < 0)
            state.cursor.position.X = 0;
        break;

    case vt_message_id::cursor_backward_tab:
        state.cursor.position.X = state.prev_tab_stop(state.cursor.position.X);
        break;

    case vt_message_id::cursor_next_line:
        state.cursor.position.X = 0;
        state.cursor.position.Y += msg.count;
        if (state.cursor.position.Y >= state.screen_buffer_size.Y)
            state.cursor.position.Y = state.screen_buffer_size.Y - 1;
        break;

    case vt_message_id::cursor_prev_line:
        state.cursor.position.X = 0;
        state.cursor.position.Y -= msg.count;
        if (state.cursor.position.Y < 0)
            state.cursor.position.Y = 0;
        break;

    // ── 光标显示 ──
    case vt_message_id::cursor_show:
        state.cursor.visible = true;
        break;
    case vt_message_id::cursor_hide:
        state.cursor.visible = false;
        break;
    case vt_message_id::cursor_enable_blinking:
        // 对标: blinking 仅影响终端渲染, state 无对应字段
        break;
    case vt_message_id::cursor_disable_blinking:
        break;

    // ── SGR 属性 → state.default_attributes ──
    case vt_message_id::sgr: {
        if (msg.sgr_reset)
        {
            state.default_attributes = 0x07;
            break;
        }

        WORD &attr = state.default_attributes;

        // 前景色
        if (msg.fg_is_default)
        {
            attr &= 0xFFF0; /* fg = 7 (white) */
            attr |= 7;
        }
        else if (msg.fg_is_rgb)
        { /* 真彩色无法映射到 16 色, 忽略 */
        }
        else if (msg.fg_color >= 0 && msg.fg_color <= 15)
        {
            attr = static_cast<WORD>((attr & 0xFFF0) | (msg.fg_color & 0x0F));
        }

        // 背景色
        if (msg.bg_is_default)
        {
            attr &= 0xFF0F;
            attr |= (7 << 4);
        }
        else if (msg.bg_is_rgb)
        { /* 忽略 */
        }
        else if (msg.bg_color >= 0 && msg.bg_color <= 15)
        {
            attr = static_cast<WORD>((attr & 0xFF0F) | ((msg.bg_color & 0x0F) << 4));
        }

        // 属性标志 (对标 COMMON_LVB_*)
        if (msg.bold)
            attr |= COMMON_LVB_GRID_HORIZONTAL; // 简化映射
        if (msg.underline)
            attr |= COMMON_LVB_UNDERSCORE;
        if (msg.negative)
            attr |= COMMON_LVB_REVERSE_VIDEO;
        break;
    }

    // ── 文本输出 → screen_buffer（仅可打印字符，不含控制字符）──
    case vt_message_id::text: {
        COORD pos = state.cursor.position;
        auto mode = state.text_measurement;
        for (char32_t ch : msg.text)
        {
            if (pos.X >= state.screen_buffer_size.X)
                break;
            if (pos.Y >= state.screen_buffer_size.Y)
                break;

            int cw = char_width_for_mode(ch, mode, state.ambiguous_is_wide);
            if (cw < 1)
                cw = 1;

            SHORT screen_w = state.screen_buffer_size.X;
            if (pos.X + cw > screen_w)
            {
                pos.X = 0;
                pos.Y++;
                if (pos.Y >= state.screen_buffer_size.Y)
                {
                    pos.Y = state.screen_buffer_size.Y - 1;
                    break;
                }
            }

            sb.set_u32(pos, ch, state.default_attributes);
            pos.X += static_cast<SHORT>(cw);
            if (pos.X >= screen_w)
            {
                pos.X = 0;
                pos.Y++;
            }
        }
        if (pos.X >= state.screen_buffer_size.X)
            pos.X = state.screen_buffer_size.X - 1;
        if (pos.Y >= state.screen_buffer_size.Y)
            pos.Y = state.screen_buffer_size.Y - 1;
        if (pos.X < 0)
            pos.X = 0;
        if (pos.Y < 0)
            pos.Y = 0;
        state.cursor.position = pos;
        break;
    }

    // ── 回车：X 归零 ──
    case vt_message_id::carriage_return:
        state.cursor.position.X = 0;
        break;

    // ── 换行：Windows 语义 X=0 + Y++ ──
    case vt_message_id::line_feed:
        state.cursor.position.X = 0;
        state.cursor.position.Y++;
        if (state.cursor.position.Y >= state.screen_buffer_size.Y)
            state.cursor.position.Y = state.screen_buffer_size.Y - 1;
        break;

    // ── 光标保存/恢复 ──
    case vt_message_id::save_cursor:
    case vt_message_id::ansi_save_cursor:
        state.decsc_cursor.position = state.cursor.position;
        state.decsc_cursor.attributes = state.default_attributes;
        state.decsc_cursor.has_state = true;
        break;

    case vt_message_id::restore_cursor:
    case vt_message_id::ansi_restore_cursor:
        if (state.decsc_cursor.has_state)
        {
            state.cursor.position = state.decsc_cursor.position;
            state.default_attributes = state.decsc_cursor.attributes;
        }
        break;

    // ── 滚动 ──
    case vt_message_id::scroll_up: {
        SMALL_RECT sr{0, state.cursor.position.Y, static_cast<SHORT>(state.screen_buffer_size.X - 1),
                      static_cast<SHORT>(state.screen_buffer_size.Y - 1)};
        COORD dest{sr.Left, static_cast<SHORT>(sr.Top - msg.count)};
        sb.scroll(sr, sr, false, dest, U' ', state.default_attributes);
        break;
    }

    case vt_message_id::scroll_down: {
        SMALL_RECT sr{0, state.cursor.position.Y, static_cast<SHORT>(state.screen_buffer_size.X - 1),
                      static_cast<SHORT>(state.screen_buffer_size.Y - 1)};
        COORD dest{sr.Left, static_cast<SHORT>(sr.Top + msg.count)};
        sb.scroll(sr, sr, false, dest, U' ', state.default_attributes);
        break;
    }

    case vt_message_id::insert_lines: {
        SMALL_RECT sr{0, state.cursor.position.Y, static_cast<SHORT>(state.screen_buffer_size.X - 1),
                      static_cast<SHORT>(state.screen_buffer_size.Y - 1)};
        COORD dest{sr.Left, static_cast<SHORT>(sr.Top + msg.count)};
        sb.scroll(sr, sr, false, dest, U' ', state.default_attributes);
        break;
    }

    case vt_message_id::delete_lines: {
        SMALL_RECT sr{0, state.cursor.position.Y, static_cast<SHORT>(state.screen_buffer_size.X - 1),
                      static_cast<SHORT>(state.screen_buffer_size.Y - 1)};
        COORD dest{sr.Left, static_cast<SHORT>(sr.Top - msg.count)};
        sb.scroll(sr, sr, false, dest, U' ', state.default_attributes);
        break;
    }

    // ── 擦除 ──
    case vt_message_id::erase_in_display: {
        switch (msg.erase_mode)
        {
        case 0: // 光标到屏幕尾
            for (SHORT x = state.cursor.position.X; x < state.screen_buffer_size.X; ++x)
                sb.clear_cell({x, state.cursor.position.Y}, state.default_attributes);
            for (SHORT y = state.cursor.position.Y + 1; y < state.screen_buffer_size.Y; ++y)
                for (SHORT x = 0; x < state.screen_buffer_size.X; ++x)
                    sb.clear_cell({x, y}, state.default_attributes);
            break;
        case 1: // 屏幕头到光标
            for (SHORT y = 0; y < state.cursor.position.Y; ++y)
                for (SHORT x = 0; x < state.screen_buffer_size.X; ++x)
                    sb.clear_cell({x, y}, state.default_attributes);
            for (SHORT x = 0; x <= state.cursor.position.X; ++x)
                sb.clear_cell({x, state.cursor.position.Y}, state.default_attributes);
            break;
        case 2:
        case 3:
            sb.clear(state.default_attributes);
            break;
        }
        break;
    }

    case vt_message_id::erase_in_line: {
        switch (msg.erase_mode)
        {
        case 0:
            for (SHORT x = state.cursor.position.X; x < state.screen_buffer_size.X; ++x)
                sb.clear_cell({x, state.cursor.position.Y}, state.default_attributes);
            break;
        case 1:
            for (SHORT x = 0; x <= state.cursor.position.X; ++x)
                sb.clear_cell({x, state.cursor.position.Y}, state.default_attributes);
            break;
        case 2:
            for (SHORT x = 0; x < state.screen_buffer_size.X; ++x)
                sb.clear_cell({x, state.cursor.position.Y}, state.default_attributes);
            break;
        }
        break;
    }

    // ── 标题 ──
    case vt_message_id::set_window_title:
        state.title.assign(msg.title.data(), msg.title.size());
        // original_title 由 api_set_title 首次设置时保存, 这里不处理
        break;

    // ── 滚动区域 ──
    case vt_message_id::set_scrolling_region:
        // 存储以备后用 (当前不实现完整滚动区域语义)
        break;

    // ── 交替缓冲区 ──
    case vt_message_id::use_alternate_buffer:
    case vt_message_id::use_main_buffer:
        // 由 api_router::switch_active_screen_buffer 处理
        break;

    // ── 字符集 ──
    case vt_message_id::designate_charset_line_drawing:
        state.dec_line_drawing_mode = true;
        break;
    case vt_message_id::designate_charset_ascii:
        state.dec_line_drawing_mode = false;
        break;

    // ── Tab 操作 ──
    case vt_message_id::horizontal_tab_set:
        state.set_tab_stop(state.cursor.position.X);
        break;
    case vt_message_id::tab_clear_current:
        state.clear_tab_stop(state.cursor.position.X);
        break;
    case vt_message_id::tab_clear_all:
        state.clear_all_tab_stops();
        break;

    // ── 窗口 resize ──
    case vt_message_id::resize_window: {
        SHORT rows = msg.resize_rows;
        SHORT cols = msg.resize_cols;
        if (rows > 0 && cols > 0)
        {
            state.screen_buffer_size = {cols, rows};
            state.current_window_size = {cols, rows};
            state.max_window_size = {cols, rows};
            state.clear_all_tab_stops();
            state.init_tab_stops();
            sb.resize({cols, rows});
            // Clamp cursor if out of bounds
            if (state.cursor.position.X >= cols)
                state.cursor.position.X = cols - 1;
            if (state.cursor.position.Y >= rows)
                state.cursor.position.Y = rows - 1;
        }
        break;
    }

    // ── 其他非状态消息 ──
    default:
        break;
    }
}

// ── filter_osc_sequences ───────────────────────────────
// 原地移除 ESC ] Ps ; Pt ST 序列（如 OSC 9001 ConDrv 专有标记）。
// 终端不认识这些 OSC 代码时会吞掉整个序列不显示，
// 导致 cmd.exe 输出中出现空白。
inline void filter_osc_sequences(std::u32string &s)
{
    size_t w = 0;
    for (size_t r = 0; r < s.size();)
    {
        if (s[r] == 0x1B && r + 1 < s.size() && s[r + 1] == U']')
        {
            size_t j = r + 2;
            while (j < s.size() && s[j] != 0x07 && s[j] != 0x1B)
                ++j;
            if (j + 1 < s.size() && s[j] == 0x1B && s[j + 1] == U'\\')
                r = j + 2; // skip OSC ... ESC ST
            else if (j < s.size() && s[j] == 0x07)
                r = j + 1; // skip OSC ... BEL
            else
            {
                s[w++] = s[r++];
            } // unterminated: keep
            continue;
        }
        s[w++] = s[r++];
    }
    s.resize(w);
}

} // namespace conpty
