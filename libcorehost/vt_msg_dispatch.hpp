// ── conpty/vt_msg_dispatch.hpp ─────────────────────
// vt_message 状态应用。
//
// 功能分解：
// 1. 光标、SGR、标题、tab、resize 等消息更新 console_state。
// 2. text、erase、scroll、insert/delete line 等消息更新 screen_buffer。
// 3. 不可识别的控制序列由 vt_parser 以 unknown_sequence 交给调用方决策。
//
// vt_msg_apply_state<id>(msg, state, sb):
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
//   set_window_title    → state.title = msg.payload.title
//   cursor_horiz_absolute→ state.cursor.position.X = col-1
//   cursor_up/down/...  → adjust cursor
//   其他               → 无状态影响
#pragma once
#include <windows.h>
#include "vt_parser.hpp"
#include "console_state.hpp"
#include "screen_buffer.hpp"
#include "char_convert.hpp"
#include "char_width.hpp"
#include "perf_diag.hpp"

namespace corehost::conpty
{

// 重置 Win32 属性到传统默认值；用于 SGR 0。
inline void reset_sgr_attributes(WORD &attr) noexcept
{
    // 0x07 是传统白前景/黑背景属性。
    attr = 0x07;
}

// 应用 SGR 39 到当前 Win32 属性。
inline void set_sgr_foreground_default(WORD &attr) noexcept
{
    attr &= 0xFFF0;
    attr |= 7;
}

// 应用 SGR 49 到当前 Win32 属性。
inline void set_sgr_background_default(WORD &attr) noexcept
{
    // 保持既有语义：默认背景索引为 7，而不是传统黑色 0。
    attr &= 0xFF0F;
    attr |= (7 << 4);
}

// 将 SGR 前景索引映射到 Win32 属性低 4 位；超出 16 色时忽略。
inline void set_sgr_foreground_index(WORD &attr, uint8_t index) noexcept
{
    if (index <= 15)
        attr = static_cast<WORD>((attr & 0xFFF0) | (index & 0x0F));
}

// 将 SGR 背景索引映射到 Win32 属性高 4 位；超出 16 色时忽略。
inline void set_sgr_background_index(WORD &attr, uint8_t index) noexcept
{
    if (index <= 15)
        attr = static_cast<WORD>((attr & 0xFF0F) | ((index & 0x0F) << 4));
}

// 在 Win32 属性中打开 COMMON_LVB_* 标志。
inline void set_sgr_flag(WORD &attr, WORD flag) noexcept
{
    attr |= flag;
}

// 在 Win32 属性中关闭 COMMON_LVB_* 标志。
inline void clear_sgr_flag(WORD &attr, WORD flag) noexcept
{
    attr &= static_cast<WORD>(~flag);
}

// 把 parser 产出的 SGR payload 合并到当前 Win32 属性字。
inline void apply_sgr_to_attributes(const vt_sgr_payload &sgr, WORD &attr) noexcept
{
    if (sgr.has_reset())
    {
        reset_sgr_attributes(attr);
    }

    if (sgr.fg.is_default())
    {
        set_sgr_foreground_default(attr);
    }
    else if (sgr.fg.is_indexed() && sgr.fg.value <= 15)
    {
        set_sgr_foreground_index(attr, sgr.fg.value);
    }

    if (sgr.bg.is_default())
    {
        set_sgr_background_default(attr);
    }
    else if (sgr.bg.is_indexed() && sgr.bg.value <= 15)
    {
        set_sgr_background_index(attr, sgr.bg.value);
    }

    if (sgr.clears(vt_sgr_flag::bold))
        clear_sgr_flag(attr, COMMON_LVB_GRID_HORIZONTAL);
    if (sgr.clears(vt_sgr_flag::underline))
        clear_sgr_flag(attr, COMMON_LVB_UNDERSCORE);
    if (sgr.clears(vt_sgr_flag::negative))
        clear_sgr_flag(attr, COMMON_LVB_REVERSE_VIDEO);
    if (sgr.has(vt_sgr_flag::bold))
        set_sgr_flag(attr, COMMON_LVB_GRID_HORIZONTAL);
    if (sgr.has(vt_sgr_flag::underline))
        set_sgr_flag(attr, COMMON_LVB_UNDERSCORE);
    if (sgr.has(vt_sgr_flag::negative))
        set_sgr_flag(attr, COMMON_LVB_REVERSE_VIDEO);
}

// 将一条已解析 VT 消息应用到本地 Console 状态模型；模板参数必须匹配 msg 的 id。
template <vt_message_id id>
inline void vt_msg_apply_state(const vt_message &msg, console_state &state, screen_buffer &sb)
{
    // 该函数只更新本地 Console 模型，不向宿主终端写 VT。需要实际显示变化的
    // 路径应同时调用 pipe_bridge 的 VT 输出接口。
    switch (id)
    {
    // ── 光标定位 ──
    case vt_message_id::cursor_position:
        // VT 坐标是 1-based；console_state 使用 0-based COORD。
        state.cursor.position.X = static_cast<SHORT>(msg.payload.position.col - 1);
        state.cursor.position.Y = static_cast<SHORT>(msg.payload.position.row - 1);
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
        state.cursor.position.X = static_cast<SHORT>(msg.payload.position.col - 1);
        if (state.cursor.position.X < 0)
            state.cursor.position.X = 0;
        if (state.cursor.position.X >= state.screen_buffer_size.X)
            state.cursor.position.X = state.screen_buffer_size.X - 1;
        break;

    case vt_message_id::cursor_vert_absolute:
        state.cursor.position.Y = static_cast<SHORT>(msg.payload.position.row - 1);
        if (state.cursor.position.Y < 0)
            state.cursor.position.Y = 0;
        if (state.cursor.position.Y >= state.screen_buffer_size.Y)
            state.cursor.position.Y = state.screen_buffer_size.Y - 1;
        break;

    case vt_message_id::cursor_up:
        // 相对移动全部钳制在当前 screen_buffer_size 内；这里不考虑滚动区域，
        // 因为该本地模型主要服务 Console API 查询。
        state.cursor.position.Y -= msg.payload.count.value;
        if (state.cursor.position.Y < 0)
            state.cursor.position.Y = 0;
        break;

    case vt_message_id::cursor_down:
        state.cursor.position.Y += msg.payload.count.value;
        if (state.cursor.position.Y >= state.screen_buffer_size.Y)
            state.cursor.position.Y = state.screen_buffer_size.Y - 1;
        break;

    case vt_message_id::cursor_forward:
        state.cursor.position.X += msg.payload.count.value;
        if (state.cursor.position.X >= state.screen_buffer_size.X)
            state.cursor.position.X = state.screen_buffer_size.X - 1;
        break;

    case vt_message_id::cursor_forward_tab:
        state.cursor.position.X = state.next_tab_stop(state.cursor.position.X);
        break;

    case vt_message_id::cursor_backward:
        state.cursor.position.X -= msg.payload.count.value;
        if (state.cursor.position.X < 0)
            state.cursor.position.X = 0;
        break;

    case vt_message_id::cursor_backward_tab:
        state.cursor.position.X = state.prev_tab_stop(state.cursor.position.X);
        break;

    case vt_message_id::cursor_next_line:
        state.cursor.position.X = 0;
        state.cursor.position.Y += msg.payload.count.value;
        if (state.cursor.position.Y >= state.screen_buffer_size.Y)
            state.cursor.position.Y = state.screen_buffer_size.Y - 1;
        break;

    case vt_message_id::cursor_prev_line:
        state.cursor.position.X = 0;
        state.cursor.position.Y -= msg.payload.count.value;
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
        // SGR payload 表示“本条序列显式改变了什么”，不是完整属性快照。
        // 合并到 default_attributes 后，后续 Console API 文本写入会使用新属性。
        apply_sgr_to_attributes(msg.payload.sgr, state.default_attributes);
        break;
    }

    // ── 文本输出 → screen_buffer（仅可打印字符，不含控制字符）──
    case vt_message_id::text: {
        COREHOST_PERF_SCOPE_AMOUNT(apply_text_state, msg.payload.text.size());
        // pos 是本地推进副本；完成后再写回 state.cursor.position。
        COORD pos = state.cursor.position;
        auto mode = state.text_measurement;
        for (char32_t ch : msg.payload.text)
        {
            // 文本写入只处理当前缓冲区可见范围；到达底部后停止，不在本地模型
            // 中模拟 scrollback。
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
                // 宽字符放不下当前行时先换行，再写入下一行。
                pos.X = 0;
                pos.Y++;
                if (pos.Y >= state.screen_buffer_size.Y)
                {
                    pos.Y = state.screen_buffer_size.Y - 1;
                    break;
                }
            }

            sb.set_u32(pos, ch, state.default_attributes);
            // 光标推进按显示列宽，而不是输入 codepoint 数量。
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
        // DEC/ANSI 保存光标在当前实现中共用一个槽；只保存 Console API 可见的
        // 位置和属性。
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
        // 从当前光标行到缓冲区底部滚动；当前实现未应用 DECSTBM scroll margins。
        SMALL_RECT sr{0, state.cursor.position.Y, static_cast<SHORT>(state.screen_buffer_size.X - 1),
                      static_cast<SHORT>(state.screen_buffer_size.Y - 1)};
        COORD dest{sr.Left, static_cast<SHORT>(sr.Top - msg.payload.count.value)};
        sb.scroll(sr, sr, false, dest, U' ', state.default_attributes);
        break;
    }

    case vt_message_id::scroll_down: {
        SMALL_RECT sr{0, state.cursor.position.Y, static_cast<SHORT>(state.screen_buffer_size.X - 1),
                      static_cast<SHORT>(state.screen_buffer_size.Y - 1)};
        COORD dest{sr.Left, static_cast<SHORT>(sr.Top + msg.payload.count.value)};
        sb.scroll(sr, sr, false, dest, U' ', state.default_attributes);
        break;
    }

    case vt_message_id::insert_lines: {
        SMALL_RECT sr{0, state.cursor.position.Y, static_cast<SHORT>(state.screen_buffer_size.X - 1),
                      static_cast<SHORT>(state.screen_buffer_size.Y - 1)};
        COORD dest{sr.Left, static_cast<SHORT>(sr.Top + msg.payload.count.value)};
        sb.scroll(sr, sr, false, dest, U' ', state.default_attributes);
        break;
    }

    case vt_message_id::delete_lines: {
        SMALL_RECT sr{0, state.cursor.position.Y, static_cast<SHORT>(state.screen_buffer_size.X - 1),
                      static_cast<SHORT>(state.screen_buffer_size.Y - 1)};
        COORD dest{sr.Left, static_cast<SHORT>(sr.Top - msg.payload.count.value)};
        sb.scroll(sr, sr, false, dest, U' ', state.default_attributes);
        break;
    }

    // ── 擦除 ──
    case vt_message_id::erase_in_display: {
        // ED/EL 使用当前 default_attributes 清除，和 conhost 的属性继承行为一致。
        switch (msg.payload.erase_mode)
        {
        case 0: // ED0: 光标到屏幕尾
            for (SHORT x = state.cursor.position.X; x < state.screen_buffer_size.X; ++x)
                sb.clear_cell({x, state.cursor.position.Y}, state.default_attributes);
            for (SHORT y = state.cursor.position.Y + 1; y < state.screen_buffer_size.Y; ++y)
                for (SHORT x = 0; x < state.screen_buffer_size.X; ++x)
                    sb.clear_cell({x, y}, state.default_attributes);
            break;
        case 1: // ED1: 屏幕头到光标
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
        switch (msg.payload.erase_mode)
        {
        case 0: // EL0: 光标到行尾
            for (SHORT x = state.cursor.position.X; x < state.screen_buffer_size.X; ++x)
                sb.clear_cell({x, state.cursor.position.Y}, state.default_attributes);
            break;
        case 1: // EL1: 行首到光标
            for (SHORT x = 0; x <= state.cursor.position.X; ++x)
                sb.clear_cell({x, state.cursor.position.Y}, state.default_attributes);
            break;
        case 2: // EL2: 整行
            for (SHORT x = 0; x < state.screen_buffer_size.X; ++x)
                sb.clear_cell({x, state.cursor.position.Y}, state.default_attributes);
            break;
        }
        break;
    }

    // ── 标题 ──
    case vt_message_id::set_window_title:
        // msg.payload.title 是 parser 内部缓冲视图；状态层必须复制保存。
        state.title.clear();
        state.title.append(msg.payload.title.data(), msg.payload.title.size());
        // original_title 由 api_set_title 首次设置时保存, 这里不处理
        break;

    // ── 滚动区域 ──
    case vt_message_id::set_scrolling_region:
        // 当前本地模型不保存 scroll margins；实际终端端的滚动区域由 VT 透传
        // 处理，Console API 查询暂不暴露该状态。
        break;

    // ── 交替缓冲区 ──
    case vt_message_id::use_alternate_buffer:
    case vt_message_id::use_main_buffer:
        // active screen buffer 由 api_router::switch_active_screen_buffer 切换。
        // 这里不修改 sb，避免一个消息在 router 和 state 层重复切换。
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
        // HTS/TBC 作用于 console_state 的动态 tab 表；screen_buffer 不保存 tab
        // stop，因为它不是格子内容。
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
        SHORT rows = msg.payload.resize.rows;
        SHORT cols = msg.payload.resize.cols;
        if (rows > 0 && cols > 0)
        {
            // 终端 resize 同时改变 buffer/window/max 三个尺寸字段。当前实现
            // 没有独立 scrollback，因此三者保持一致。
            state.screen_buffer_size = {cols, rows};
            state.max_window_size = {cols, rows};
            sb.viewport.reset_to_buffer({cols, rows});
            state.scroll_region_top = 1;
            state.scroll_region_bottom = 0;
            state.clear_all_tab_stops();
            state.init_tab_stops();
            sb.resize({cols, rows});
            // resize 后光标必须仍在新缓冲区范围内。
            if (state.cursor.position.X >= cols)
                state.cursor.position.X = cols - 1;
            if (state.cursor.position.Y >= rows)
                state.cursor.position.Y = rows - 1;
        }
        break;
    }

    // ── 其他非状态消息 ──
    default:
        // 键盘消息、终端查询响应和当前不建模的模式切换不会改变本地 Console
        // API 状态；输出方向的 VT 序列化由 pipe_bridge::vt_msg_send 负责。
        break;
    }
}

} // namespace corehost::conpty
