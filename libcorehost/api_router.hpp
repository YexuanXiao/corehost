// ── conpty/api_router.hpp ──────────────────────────
// Layer 2: Console API 分派 (char32_t 版本)
//
// 与 conpty/api_router.hpp 的区别:
//   - namespace conpty
//   - 使用 conpty 的类型 (console_state, screen_buffer, input_buffer, io_state, pipe_bridge)
//   - switch_active_screen_buffer 中的 VT 同步使用 vt_flush
#pragma once
#include <windows.h>
#include "miniio/io_thread.hpp"
#include "os/Console/conmsgl1.h"
#include "api_handlers.hpp"
#include "screen_buffer.hpp"
#include "input_buffer.hpp"
#include "io_state.hpp"

namespace conpty
{

struct api_router
{
    console_state &state;
    screen_buffer &sb_main;
    screen_buffer &sb_alt;
    input_buffer &inp;
    io_state &io;
    pipe_bridge &bridge;
    bool alt_active = false;

    screen_buffer &active_screen_buffer() noexcept
    {
        return alt_active ? sb_alt : sb_main;
    }

    void switch_active_screen_buffer(bool alt)
    {
        if (alt == alt_active)
            return;

        alt_active = alt;

        if (alt)
            bridge.vt_append_str("\x1b[?1049h"sv);
        else
            bridge.vt_append_str("\x1b[?1049l"sv);
        bridge.vt_flush();
        vt_write_screen_snapshot();
    }

    void vt_write_screen_snapshot()
    {
        auto &sb = active_screen_buffer();
        bridge.vt_write_attr(state.default_attributes);
        for (SHORT y = 0; y < sb.size.Y && y < state.screen_buffer_size.Y; ++y)
        {
            bridge.vt_write_cup(y, 0);
            WORD last_attr = 0xFFFF;
            for (SHORT x = 0; x < sb.size.X && x < state.screen_buffer_size.X; ++x)
            {
                WORD cell_attr = sb.attr_at({x, y});
                if (cell_attr != last_attr)
                {
                    bridge.vt_write_attr(cell_attr);
                    last_attr = cell_attr;
                }
                bridge.vt_write_cell(sb.at_u32({x, y}));
            }
        }
        bridge.vt_write_cup(state.cursor.position.Y, state.cursor.position.X);
        bridge.vt_flush();
    }

    bool handle_user_defined(miniio::io_msg &msg)
    {
        auto *hdr = reinterpret_cast<CONSOLE_MSG_HEADER *>(msg.body);
        auto layer = hdr->ApiNumber >> 24;
        auto api = hdr->ApiNumber & 0xFFFFFF;

        switch (layer)
        {
        case 1:
            return dispatch_L1(msg, api);
        case 2:
            return dispatch_L2(msg, api);
        case 3:
            return dispatch_L3(msg, api);
        default:
            ucomplete(msg);
            return true;
        }
    }

    // ── L1 / L2 / L3 分发表 ──

    bool dispatch_L1(miniio::io_msg &msg, DWORD api)
    {
        auto &sb = active_screen_buffer();
        if (api != 4)
            LOG("[dispatch] L1 api=%lu", api);
        switch (api)
        {
        case 0:
            return api_get_cp(msg, state, sb, inp, bridge);
        case 1:
            return api_get_mode(msg, state, sb, inp, bridge);
        case 2:
            return api_set_mode(msg, state, sb, inp, bridge);
        case 3:
            return api_get_num_input(msg, state, sb, inp, bridge);
        case 4:
            return api_get_console_input(msg, state, sb, inp, bridge);
        case 5:
            return api_read_console(msg, state, sb, inp, bridge);
        case 6:
            return api_write_console(msg, state, sb, inp, bridge);
        case 7:
            return api_deprecated_l1(msg, state, sb, inp, bridge);
        case 8:
            return api_get_langid(msg, state, sb, inp, bridge);
        case 9:
            return api_deprecated_l1(msg, state, sb, inp, bridge);
        default:
            ucomplete(msg);
            return true;
        }
    }

    bool dispatch_L2(miniio::io_msg &msg, DWORD api)
    {
        auto &sb = active_screen_buffer();
        if (api != 7 && api != 13)
            LOG("[dispatch] L2 api=%lu", api);
        switch (api)
        {
        case 0:
            return api_fill_output(msg, state, sb, inp, bridge);
        case 1:
            return api_ctrl_event(msg, state, sb, inp, bridge);
        case 2:
            return api_set_active_sb(msg, state, sb, inp, bridge);
        case 3:
            return api_flush_input_buf(msg, state, sb, inp, bridge);
        case 4:
            return api_set_cp(msg, state, sb, inp, bridge);
        case 5:
            return api_get_cursor(msg, state, sb, inp, bridge);
        case 6:
            return api_set_cursor(msg, state, sb, inp, bridge);
        case 7:
            return api_get_sb_info(msg, state, sb, inp, bridge);
        case 8:
            return api_set_sb_info(msg, state, sb, inp, bridge);
        case 9:
            return api_set_sb_size(msg, state, sb, inp, bridge);
        case 10:
            return api_set_cursor_pos(msg, state, sb, inp, bridge);
        case 11:
            return api_largest_window(msg, state, sb, inp, bridge);
        case 12:
            return api_scroll_sb(msg, state, sb, inp, bridge);
        case 13:
            return api_set_text_attr(msg, state, sb, inp, bridge);
        case 14:
            return api_set_window_info(msg, state, sb, inp, bridge);
        case 15:
            return api_read_output_string(msg, state, sb, inp, bridge);
        case 16:
            return api_write_console_input(msg, state, sb, inp, bridge);
        case 17:
            return api_write_console_output(msg, state, sb, inp, bridge);
        case 18:
            return api_write_output_string(msg, state, sb, inp, bridge);
        case 19:
            return api_read_console_output(msg, state, sb, inp, bridge);
        case 20:
            return api_get_title(msg, state, sb, inp, bridge);
        case 21:
            return api_set_title(msg, state, sb, inp, bridge);
        default:
            ucomplete(msg);
            return true;
        }
    }

    bool dispatch_L3(miniio::io_msg &msg, DWORD api)
    {
        auto &sb = active_screen_buffer();
        if (api != 31 && api != 4)
            LOG("[dispatch] L3 api=%lu", api);
        switch (api)
        {
        // ── 第一类: 活跃 L3 API (20 个) ──
        case 1:
            return api_l3_get_mouse_info(msg, state, sb, inp, bridge);
        case 3:
            return api_l3_get_font_size(msg, state, sb, inp, bridge);
        case 4:
            return api_l3_get_current_font(msg, state, sb, inp, bridge);
        case 13:
            return api_l3_set_display_mode(msg, state, sb, inp, bridge);
        case 17:
            return api_l3_get_display_mode(msg, state, sb, inp, bridge);
        case 18:
            return api_l3_add_alias(msg, state, sb, inp, bridge);
        case 19:
            return api_l3_get_alias(msg, state, sb, inp, bridge);
        case 20:
            return api_l3_get_aliases_length(msg, state, sb, inp, bridge);
        case 21:
            return api_l3_get_alias_exes_length(msg, state, sb, inp, bridge);
        case 22:
            return api_l3_get_aliases(msg, state, sb, inp, bridge);
        case 23:
            return api_l3_get_alias_exes(msg, state, sb, inp, bridge);
        case 24:
            return api_l3_expunge_history(msg, state, sb, inp, bridge);
        case 25:
            return api_l3_set_num_commands(msg, state, sb, inp, bridge);
        case 26:
            return api_l3_get_history_length(msg, state, sb, inp, bridge);
        case 27:
            return api_l3_get_history(msg, state, sb, inp, bridge);
        case 31:
            return api_l3_get_console_window(msg, state, sb, inp, bridge);
        case 40:
            return api_l3_get_selection_info(msg, state, sb, inp, bridge);
        case 41:
            return api_l3_get_process_list(msg, state, sb, inp, bridge);
        case 42:
            return api_l3_get_history_info(msg, state, sb, inp, bridge);
        case 43:
            return api_l3_set_history_info(msg, state, sb, inp, bridge);
        case 44:
            return api_l3_set_current_font(msg, state, sb, inp, bridge);

        // ── 第二类: 废弃 L3 API (24 个) ──
        case 0:
        case 2:
        case 5:
        case 6:
        case 7:
        case 8:
        case 9:
        case 10:
        case 11:
        case 12:
        case 14:
        case 15:
        case 16:
        case 28:
        case 29:
        case 30:
        case 32:
        case 33:
        case 34:
        case 35:
        case 36:
        case 37:
        case 38:
        case 39:
            return api_l3_deprecated(msg, state, sb, inp, bridge);

        default:
            ucomplete(msg);
            return true;
        }
    }
};

} // namespace conpty
