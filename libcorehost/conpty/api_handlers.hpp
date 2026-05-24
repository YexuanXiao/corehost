// ── conpty/api_handlers.hpp ────────────────────────
// Layer 3: Console API handler 函数 (char32_t 版本)
//
// 与 conpty/api_handlers.hpp 的区别:
//   - namespace conpty
//   - WriteConsole/FillOutput 使用 char32_t 路径
//   - title 字段为 std::u32string
//   - VT 输出使用 vt_append_* + vt_flush (无 snprintf)
#pragma once
#include <windows.h>
#include <cstring>
#include <array>
#include "miniio/io_thread.hpp"
#include "os/Console/conmsgl1.h"
#include "os/Console/conmsgl2.h"
#include "os/Console/conmsgl3.h"
#include "console_state.hpp"
#include "screen_buffer.hpp"
#include "input_buffer.hpp"
#include "pipe_bridge.hpp"
#include "conpty_vt_parser.hpp"
#include "vt_msg_dispatch.hpp"
#include "char_convert.hpp"
#include "char_width.hpp"
#include "utility/log.hpp"
#include "ntapi/consolenslmode.hpp"

namespace conpty
{

// ── 前向声明 ──
struct pipe_bridge;

// Win32 控制台属性低 4 位使用 BGRI 顺序：
//   bit0=BLUE, bit1=GREEN, bit2=RED, bit3=INTENSITY
// ANSI SGR 30-37/90-97 使用 RGB 顺序：
//   0=black, 1=red, 2=green, 3=yellow, 4=blue, ...
// 不能直接把 attr&0x0F 透传给 SGR，否则红/蓝会互换。
inline short win32_attr_color_to_sgr_index(WORD color) noexcept
{
    short sgr = 0;
    if (color & FOREGROUND_RED)
        sgr |= 1;
    if (color & FOREGROUND_GREEN)
        sgr |= 2;
    if (color & FOREGROUND_BLUE)
        sgr |= 4;
    if (color & FOREGROUND_INTENSITY)
        sgr |= 8;
    return sgr;
}

inline void set_sgr_from_win32_attr(vt_message &m, WORD attr) noexcept
{
    m.fg_color = win32_attr_color_to_sgr_index(attr & 0x0F);
    m.bg_color = win32_attr_color_to_sgr_index((attr >> 4) & 0x0F);

    auto fl = (attr >> 8) & 0xFF;
    if (fl & COMMON_LVB_UNDERSCORE)
        m.underline = true;
    if (fl & COMMON_LVB_REVERSE_VIDEO)
        m.negative = true;
}

// ── completion 辅助 ──

inline void ucomplete(miniio::io_msg &msg)
{
    auto &c = miniio::prepare_completion(msg, 0, 0);
    c.Write.Data = msg.body + sizeof(CONSOLE_MSG_HEADER);
    c.Write.Size = 0;
}

inline void ucomplete_sz(miniio::io_msg &msg, ULONG sz)
{
    auto &c = miniio::prepare_completion(msg, 0, sz);
    c.Write.Data = msg.body + sizeof(CONSOLE_MSG_HEADER);
    c.Write.Size = sz;
}

// ════════════════════════════════════════════════════════
// L1 API handlers
// ════════════════════════════════════════════════════════

inline bool api_get_cp(miniio::io_msg &msg, console_state &state, screen_buffer *, input_buffer *, pipe_bridge *)
{
    auto *r = reinterpret_cast<CONSOLE_GETCP_MSG *>(msg.body + sizeof(CONSOLE_MSG_HEADER));
    r->CodePage = r->Output ? state.output_code_page : state.input_code_page;
    ucomplete_sz(msg, sizeof(CONSOLE_GETCP_MSG));
    return true;
}

inline bool api_get_mode(miniio::io_msg &msg, console_state &state, screen_buffer *, input_buffer *, pipe_bridge *)
{
    auto *r = reinterpret_cast<CONSOLE_MODE_MSG *>(msg.body + sizeof(CONSOLE_MSG_HEADER));
    r->Mode = state.input_mode;
    ucomplete_sz(msg, sizeof(CONSOLE_MODE_MSG));
    return true;
}

inline bool api_set_mode(miniio::io_msg &msg, console_state &state, screen_buffer *, input_buffer *, pipe_bridge *)
{
    auto *r = reinterpret_cast<CONSOLE_MODE_MSG *>(msg.body + sizeof(CONSOLE_MSG_HEADER));
    state.input_mode = r->Mode;
    state.output_mode = r->Mode;
    ucomplete(msg);
    return true;
}

inline bool api_get_num_input(miniio::io_msg &msg, console_state &, screen_buffer *, input_buffer *inp,
                              pipe_bridge *bridge)
{
    auto *r = reinterpret_cast<CONSOLE_GETNUMBEROFINPUTEVENTS_MSG *>(msg.body + sizeof(CONSOLE_MSG_HEADER));
    r->ReadyEvents = inp ? static_cast<DWORD>(inp->available()) : 0;
    ucomplete_sz(msg, sizeof(CONSOLE_GETNUMBEROFINPUTEVENTS_MSG));
    return true;
}

inline bool api_get_console_input(miniio::io_msg &msg, console_state &, screen_buffer *, input_buffer *inp,
                                  pipe_bridge *)
{
    auto *r = reinterpret_cast<CONSOLE_GETCONSOLEINPUT_MSG *>(msg.body + sizeof(CONSOLE_MSG_HEADER));
    auto *out =
        reinterpret_cast<INPUT_RECORD *>(msg.body + sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_GETCONSOLEINPUT_MSG));
    // 按客户端 buffer 大小限制返回记录数（关键修复：PSReadLine ReadKey(1) 要求一次只读 1 条）
    auto clientBuf = msg.descriptor.OutputSize;
    auto hdrSize = sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_GETCONSOLEINPUT_MSG);
    auto maxc = clientBuf > hdrSize ? (clientBuf - hdrSize) / sizeof(INPUT_RECORD) : 0;
    if (maxc == 0)
        maxc = 1; // 至少允许 1 条
    size_t n = 0;
    if (inp)
    {
        if (r->Flags & 0x0001)
            n = inp->peek(out, maxc);
        else
            n = inp->read(out, maxc);
    }
    r->NumRecords = static_cast<ULONG>(n);
    ucomplete_sz(msg, sizeof(CONSOLE_GETCONSOLEINPUT_MSG) + static_cast<ULONG>(n * sizeof(INPUT_RECORD)));
    return true;
}

inline bool api_get_langid(miniio::io_msg &msg, console_state &state, screen_buffer *, input_buffer *, pipe_bridge *)
{
    auto *r = reinterpret_cast<CONSOLE_LANGID_MSG *>(msg.body + sizeof(CONSOLE_MSG_HEADER));
    r->LangId = state.lang_id;
    ucomplete_sz(msg, sizeof(CONSOLE_LANGID_MSG));
    return true;
}

// ── WriteConsole: UTF-16/ANSI → char32_t → vt_message 驱动 ──
inline bool api_write_console(miniio::io_msg &msg, console_state &state, screen_buffer *sb, input_buffer *,
                              pipe_bridge *bridge)
{
    auto *req = reinterpret_cast<CONSOLE_WRITECONSOLE_MSG *>(msg.body + sizeof(CONSOLE_MSG_HEADER));
    bool uni = req->Unicode != 0;
    auto sd = msg.body + sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_WRITECONSOLE_MSG);
    auto sbytes = msg.descriptor.InputSize - sizeof(CONSOLE_MSG_HEADER) - sizeof(CONSOLE_WRITECONSOLE_MSG);

    if (sbytes > 0)
    {
        auto &u32s = bridge->conv_u32();
        if (uni)
        {
            auto *ws = reinterpret_cast<const wchar_t *>(sd);
            auto wl = sbytes / sizeof(wchar_t);
            convert_utf16_to_u32(std::wstring_view{ws, wl}, u32s);
        }
        else
        {
            convert_ansi_to_u32(reinterpret_cast<const char *>(sd), sbytes,
                                state.output_code_page ? state.output_code_page : CP_ACP, u32s, bridge->conv_wstr());
        }

        if (!u32s.empty())
        {
            // ── 钳制光标到屏幕缓冲区范围内（防止 text 溢出导致越界）──
            if (state.cursor.position.X < 0)
                state.cursor.position.X = 0;
            if (state.cursor.position.X >= state.screen_buffer_size.X)
                state.cursor.position.X = state.screen_buffer_size.X - 1;
            if (state.cursor.position.Y < 0)
                state.cursor.position.Y = 0;
            if (state.cursor.position.Y >= state.screen_buffer_size.Y)
                state.cursor.position.Y = state.screen_buffer_size.Y - 1;

            // WriteConsole 必须使用 state.cursor.position —— cmd.exe 的
            // 文本输出（含 \r\n）依赖此位置。echo 过程中终端光标独立移动，
            // 但 CUP 会把终端光标拉回到 state 位置，保证后续 \r\n 正确换行。
            COORD start_pos = state.cursor.position;
            LOG("[api_write_console] start: u32s_len=%zu sbytes=%lu start=(%d,%d) size=(%d,%d) win=(%d,%d)",
                u32s.size(), static_cast<unsigned long>(sbytes), static_cast<int>(start_pos.X),
                static_cast<int>(start_pos.Y), static_cast<int>(state.screen_buffer_size.X),
                static_cast<int>(state.screen_buffer_size.Y), static_cast<int>(state.current_window_size.X),
                static_cast<int>(state.current_window_size.Y));

            // ── DEC 行绘制预处理 ──
            if (state.dec_line_drawing_mode)
            {
                for (auto &ch : u32s)
                    if (ch >= 0x5f && ch <= 0x7e)
                        ch = console_state::dec_to_unicode(static_cast<unsigned char>(ch));
            }

            if (bridge && bridge->vt_out.valid())
            {
                vt_message m{};

                // 1) 光标定位到写入起始位置 — 仅在 Enter 后换行时发送 CUP，
                //    终端已通过 DECAWM / 上一条命令维护正确光标，不需要每次写都
                //    强制移动（宽度计算可能与终端字体渲染不一致，错误 CUP 产生空行）
                bool need_cup = bridge && bridge->consume_enter_newline();
                if (need_cup)
                {
                    COORD nl_pos = bridge->get_term_cursor();
                    m.row = static_cast<short>(nl_pos.Y + 1);
                    m.col = static_cast<short>(nl_pos.X + 1);
                    bridge->vt_msg_send(vt_message_id::cursor_position, m);
                    vt_msg_apply_state(vt_message_id::cursor_position, m, state, *sb);
                    start_pos = state.cursor.position;
                }

                // 2) SGR 属性
                WORD attr = state.default_attributes;
                m = vt_message{};
                set_sgr_from_win32_attr(m, attr);
                bridge->vt_msg_send(vt_message_id::sgr, m);
                vt_msg_apply_state(vt_message_id::sgr, m, state, *sb);

                // 3) 文本输出（过滤内嵌的 ConDrv OSC 序列如 OSC 9001）
                filter_osc_sequences(u32s);
                m = vt_message{};
                m.text = u32s;
                bridge->vt_msg_send(vt_message_id::text, m);
                vt_msg_apply_state(vt_message_id::text, m, state, *sb);

                // 4) 不再发送最终 CUP 到终端——终端已通过 DECAWM 自动追踪光标，
                //    宽度计算（libunicode::width）可能与终端实际字体渲染不一致，
                //    错误的 CUP 会把光标拉到未渲染的空行。
                //    仅更新 bridge 内部追踪，不干扰终端光标。
                bridge->vt_flush();
                bridge->sync_cursor_after_write(state.cursor.position);

                LOG("[api_write_console] done: u32s_len=%zu sbytes=%lu end_cursor=(%d,%d) synced", u32s.size(),
                    static_cast<unsigned long>(sbytes), static_cast<int>(state.cursor.position.X),
                    static_cast<int>(state.cursor.position.Y));
            }
            else if (sb)
            {
                // 无 bridge: 仅更新 screen_buffer
                vt_message m{};
                m.text = u32s;
                vt_msg_apply_state(vt_message_id::text, m, state, *sb);
            }
            else
            {
                // 无 sb 也无 bridge: raw write
                bridge->raw_write(uni, sd, sbytes);
            }
        }
    }

    req->NumBytes = sbytes;
    ucomplete_sz(msg, sizeof(CONSOLE_WRITECONSOLE_MSG));
    return true;
}

// ── ReadConsole ──
inline bool api_read_console(miniio::io_msg &msg, console_state &state, screen_buffer *, input_buffer *,
                             pipe_bridge *bridge)
{
    auto *req = reinterpret_cast<CONSOLE_READCONSOLE_MSG *>(msg.body + sizeof(CONSOLE_MSG_HEADER));
    bool proc_z = req->ProcessControlZ && (state.input_mode & ENABLE_PROCESSED_INPUT);

    auto initial_bytes = req->InitialNumBytes;
    const BYTE *init_data = nullptr;
    if (initial_bytes > 0)
    {
        init_data = msg.body + sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_READCONSOLE_MSG);
        if (req->ExeNameLength > 0)
        {
            ULONG max_xn = static_cast<ULONG>(msg.descriptor.InputSize - sizeof(CONSOLE_MSG_HEADER) -
                                              sizeof(CONSOLE_READCONSOLE_MSG));
            ULONG skip = req->ExeNameLength < max_xn ? req->ExeNameLength : max_xn;
            init_data += skip;
            initial_bytes = (initial_bytes > skip) ? (initial_bytes - skip) : 0;
        }
    }

    if (bridge)
        return bridge->handle_console_read(msg, proc_z, init_data, initial_bytes);

    req->NumBytes = 0;
    req->ControlKeyState = 0;
    auto sz = static_cast<ULONG>(sizeof(CONSOLE_READCONSOLE_MSG));
    miniio::prepare_completion(msg, 0, sz);
    msg.complete.Write.Data = msg.body + sizeof(CONSOLE_MSG_HEADER);
    msg.complete.Write.Size = sz;
    return true;
}

inline bool api_deprecated_l1(miniio::io_msg &msg, console_state &, screen_buffer *, input_buffer *, pipe_bridge *)
{
    ucomplete(msg);
    return true;
}

// ════════════════════════════════════════════════════════
// L2 API helpers
// ════════════════════════════════════════════════════════

// 计算对客户端公开的有效格式化宽度。
// WT 的字符网格列数（如 120）对应纯 ASCII 文本的最大可容纳列数，
// 但 ConPTY 模式下 clients（如 PowerShell）按此宽度预格式化的输出
// 可能因终端字体/渲染差异在 WT 中溢出到下一行。
// 对每个需要跨行预格式化的 client，预留 1 列安全边界。
// 原始 OpenConsole 的策略等价：defterm/headless 路径传入的 --width
// 由 ControlCore::Initialize 通过像素宽度计算，自然减小了约 1 列。
inline SHORT effective_formatting_width(SHORT physical_width, const console_state &state) noexcept
{
    if (state.text_measurement == text_measurement_mode::graphemes && state.ambiguous_is_wide)
    {
        return physical_width > 1 ? physical_width - 1 : physical_width;
    }
    return physical_width;
}

// ════════════════════════════════════════════════════════
// L2 API handlers
// ════════════════════════════════════════════════════════

inline bool api_fill_output(miniio::io_msg &msg, console_state &state, screen_buffer *sb, input_buffer *,
                            pipe_bridge *bridge)
{
    auto *r = reinterpret_cast<CONSOLE_FILLCONSOLEOUTPUT_MSG *>(msg.body + sizeof(CONSOLE_MSG_HEADER));
    if (r->ElementType != CONSOLE_ASCII && r->ElementType != CONSOLE_REAL_UNICODE &&
        r->ElementType != CONSOLE_FALSE_UNICODE && r->ElementType != CONSOLE_ATTRIBUTE)
    {
        ucomplete(msg);
        return true;
    }

    // 保存原始 Length — sb->fill_* 只填单行会缩小 r->Length,
    // 导致 is_fullscreen_space 判据失效 (120 < 3600 → ED2 不发)
    ULONG orig_length = r->Length;

    if (sb)
    {
        if (r->ElementType == CONSOLE_ATTRIBUTE)
        {
            auto res = sb->fill_attr(static_cast<WORD>(r->Element), r->WriteCoord, r->Length);
            r->Length = res.cells_modified;
        }
        else
        {
            auto res = sb->fill_char(static_cast<char32_t>(r->Element), r->WriteCoord, r->Length);
            r->Length = res.cells_modified;
        }
    }
    else
    {
        r->Length = 0;
    }

    bool is_fullscreen_space =
        (r->ElementType != CONSOLE_ATTRIBUTE && r->WriteCoord.X == 0 && r->WriteCoord.Y == 0 &&
         static_cast<wchar_t>(r->Element) == L' ' &&
         orig_length >= static_cast<ULONG>(effective_formatting_width(state.screen_buffer_size.X, state) *
                                           state.screen_buffer_size.Y));

    if (bridge && bridge->vt_out.valid())
    {
        if (is_fullscreen_space)
        {
            // ── VT 消息驱动: ED2 清屏 ──
            // 全屏空格填充说明 shell 在执行 Clear-Host / cls，
            // 光标已由 api_set_cursor_pos(0,0) 或 shell 管理。
            // 清除 Enter 残留的换行标志，防止后续 prompt 的 CUP 覆盖清屏。
            bridge->reset_enter_newline();
            vt_message m{};
            m.erase_mode = 2;
            bridge->vt_msg_send(vt_message_id::erase_in_display, m);
            vt_msg_apply_state(vt_message_id::erase_in_display, m, state, *sb);
            bridge->vt_flush();
        }
        else
        {
            // ── DECSC → CUP → (SGR|text) → DECRC ──
            vt_message msg_save{};
            bridge->vt_msg_send(vt_message_id::save_cursor, msg_save);
            vt_msg_apply_state(vt_message_id::save_cursor, msg_save, state, *sb);

            vt_message msg_cup{};
            msg_cup.row = static_cast<short>(r->WriteCoord.Y + 1);
            msg_cup.col = static_cast<short>(r->WriteCoord.X + 1);
            bridge->vt_msg_send(vt_message_id::cursor_position, msg_cup);
            vt_msg_apply_state(vt_message_id::cursor_position, msg_cup, state, *sb);

            if (r->ElementType == CONSOLE_ATTRIBUTE)
            {
                WORD fill_attr = static_cast<WORD>(r->Element);
                vt_message m_sgr{};
                set_sgr_from_win32_attr(m_sgr, fill_attr);
                bridge->vt_msg_send(vt_message_id::sgr, m_sgr);
                vt_msg_apply_state(vt_message_id::sgr, m_sgr, state, *sb);
            }
            else
            {
                // text fill — 复用 bridge 持久缓冲
                auto &fill_text = bridge->conv_u32();
                fill_text.assign(static_cast<size_t>(r->Length), static_cast<char32_t>(r->Element));
                vt_message m_text{};
                m_text.text = fill_text;
                bridge->vt_msg_send(vt_message_id::text, m_text);
                vt_msg_apply_state(vt_message_id::text, m_text, state, *sb);
            }

            vt_message msg_restore{};
            bridge->vt_msg_send(vt_message_id::restore_cursor, msg_restore);
            vt_msg_apply_state(vt_message_id::restore_cursor, msg_restore, state, *sb);
            bridge->vt_flush();
        }
    }

    ucomplete_sz(msg, sizeof(CONSOLE_FILLCONSOLEOUTPUT_MSG));
    return true;
}

inline bool api_ctrl_event(miniio::io_msg &msg, console_state &, screen_buffer *, input_buffer *, pipe_bridge *bridge)
{
    auto *r = reinterpret_cast<CONSOLE_CTRLEVENT_MSG *>(msg.body + sizeof(CONSOLE_MSG_HEADER));
    ::GenerateConsoleCtrlEvent(r->CtrlEvent, r->ProcessGroupId);
    if (bridge && bridge->vt_out.valid())
    {
        if (r->CtrlEvent == CTRL_C_EVENT)
        {
            bridge->vt_append_char('\x03');
            bridge->vt_flush();
        }
    }
    ucomplete(msg);
    return true;
}

inline bool api_set_active_sb(miniio::io_msg &msg, console_state &, screen_buffer *, input_buffer *, pipe_bridge *)
{
    ucomplete(msg);
    return true;
}

inline bool api_flush_input_buf(miniio::io_msg &msg, console_state &, screen_buffer *, input_buffer *inp,
                                pipe_bridge *bridge)
{
    if (inp)
        inp->flush();
    if (bridge)
        bridge->cancel_pending_read();
    ucomplete(msg);
    return true;
}

inline bool api_set_cp(miniio::io_msg &msg, console_state &state, screen_buffer *, input_buffer *, pipe_bridge *)
{
    auto *r = reinterpret_cast<CONSOLE_SETCP_MSG *>(msg.body + sizeof(CONSOLE_MSG_HEADER));
    if (r->Output)
        state.output_code_page = r->CodePage;
    else
        state.input_code_page = r->CodePage;
    ucomplete(msg);
    return true;
}

inline bool api_get_cursor(miniio::io_msg &msg, console_state &state, screen_buffer *, input_buffer *, pipe_bridge *)
{
    auto *r = reinterpret_cast<CONSOLE_GETCURSORINFO_MSG *>(msg.body + sizeof(CONSOLE_MSG_HEADER));
    r->CursorSize = state.cursor.size;
    r->Visible = state.cursor.visible ? TRUE : FALSE;
    ucomplete_sz(msg, sizeof(CONSOLE_GETCURSORINFO_MSG));
    return true;
}

inline bool api_set_cursor(miniio::io_msg &msg, console_state &state, screen_buffer *sb, input_buffer *,
                           pipe_bridge *bridge)
{
    auto *r = reinterpret_cast<CONSOLE_SETCURSORINFO_MSG *>(msg.body + sizeof(CONSOLE_MSG_HEADER));
    state.cursor.size = r->CursorSize;
    state.cursor.visible = r->Visible != FALSE;
    if (bridge && bridge->vt_out.valid())
    {
        vt_message m{};
        bridge->vt_msg_send(state.cursor.visible ? vt_message_id::cursor_show : vt_message_id::cursor_hide, m);
        vt_msg_apply_state(state.cursor.visible ? vt_message_id::cursor_show : vt_message_id::cursor_hide, m, state,
                           *sb);
        bridge->vt_flush();
    }
    ucomplete(msg);
    return true;
}

inline bool api_get_sb_info(miniio::io_msg &msg, console_state &state, screen_buffer *, input_buffer *, pipe_bridge *)
{
    auto *r = reinterpret_cast<CONSOLE_SCREENBUFFERINFO_MSG *>(msg.body + sizeof(CONSOLE_MSG_HEADER));
    std::memset(r, 0, sizeof(*r));
    if (state.cursor_position_dirty)
        state.cursor_position_dirty = false;
    r->Size = state.screen_buffer_size;
    r->Size.X = effective_formatting_width(r->Size.X, state);
    r->CursorPosition = state.cursor.position;
    r->ScrollPosition.X = 0;
    r->ScrollPosition.Y = 0;
    r->Attributes = state.default_attributes;
    r->CurrentWindowSize = state.current_window_size;
    r->CurrentWindowSize.X = effective_formatting_width(r->CurrentWindowSize.X, state);
    r->MaximumWindowSize = state.max_window_size;
    r->MaximumWindowSize.X = effective_formatting_width(r->MaximumWindowSize.X, state);
    r->PopupAttributes = state.popup_attributes;
    r->FullscreenSupported = FALSE;
    std::memcpy(r->ColorTable, state.color_table, sizeof(state.color_table));
    LOG("[api_get_sb_info] Size=(%d,%d) Win=(%d,%d) Max=(%d,%d) Cursor=(%d,%d)", r->Size.X, r->Size.Y,
        r->CurrentWindowSize.X, r->CurrentWindowSize.Y, r->MaximumWindowSize.X, r->MaximumWindowSize.Y,
        r->CursorPosition.X, r->CursorPosition.Y);
    ucomplete_sz(msg, sizeof(CONSOLE_SCREENBUFFERINFO_MSG));
    return true;
}

inline bool api_set_sb_info(miniio::io_msg &msg, console_state &state, screen_buffer *sb, input_buffer *, pipe_bridge *)
{
    auto *r = reinterpret_cast<CONSOLE_SCREENBUFFERINFO_MSG *>(msg.body + sizeof(CONSOLE_MSG_HEADER));
    state.screen_buffer_size = r->Size;
    state.cursor.position = r->CursorPosition;
    if (state.cursor.position.X < 0)
        state.cursor.position.X = 0;
    if (state.cursor.position.X >= state.screen_buffer_size.X)
        state.cursor.position.X = state.screen_buffer_size.X - 1;
    if (state.cursor.position.Y < 0)
        state.cursor.position.Y = 0;
    if (state.cursor.position.Y >= state.screen_buffer_size.Y)
        state.cursor.position.Y = state.screen_buffer_size.Y - 1;
    state.default_attributes = r->Attributes;
    state.current_window_size = r->CurrentWindowSize;
    state.max_window_size = r->MaximumWindowSize;
    state.popup_attributes = r->PopupAttributes;
    std::memcpy(state.color_table, r->ColorTable, sizeof(state.color_table));
    if (sb)
        sb->resize(r->Size);
    ucomplete(msg);
    return true;
}

inline bool api_set_sb_size(miniio::io_msg &msg, console_state &state, screen_buffer *sb, input_buffer *, pipe_bridge *)
{
    auto *r = reinterpret_cast<CONSOLE_SETSCREENBUFFERSIZE_MSG *>(msg.body + sizeof(CONSOLE_MSG_HEADER));
    COORD new_size = r->Size;
    if (new_size.X < 1)
        new_size.X = 1;
    if (new_size.Y < 1)
        new_size.Y = 1;
    if (new_size.X < state.current_window_size.X)
        new_size.X = state.current_window_size.X;
    if (new_size.Y < state.current_window_size.Y)
        new_size.Y = state.current_window_size.Y;
    state.screen_buffer_size = new_size;
    if (state.cursor.position.X >= new_size.X)
        state.cursor.position.X = new_size.X - 1;
    if (state.cursor.position.Y >= new_size.Y)
        state.cursor.position.Y = new_size.Y - 1;
    if (state.cursor.position.X < 0)
        state.cursor.position.X = 0;
    if (state.cursor.position.Y < 0)
        state.cursor.position.Y = 0;
    if (sb)
        sb->resize(new_size);
    ucomplete(msg);
    return true;
}

inline bool api_set_cursor_pos(miniio::io_msg &msg, console_state &state, screen_buffer *sb, input_buffer *,
                               pipe_bridge *bridge)
{
    auto *r = reinterpret_cast<CONSOLE_SETCURSORPOSITION_MSG *>(msg.body + sizeof(CONSOLE_MSG_HEADER));
    COORD new_pos = r->CursorPosition;
    if (new_pos.X < 0)
        new_pos.X = 0;
    if (new_pos.X >= state.screen_buffer_size.X)
        new_pos.X = state.screen_buffer_size.X - 1;
    if (new_pos.Y < 0)
        new_pos.Y = 0;
    if (new_pos.Y >= state.screen_buffer_size.Y)
        new_pos.Y = state.screen_buffer_size.Y - 1;

    if (bridge && bridge->vt_out.valid())
    {
        // 仅当光标被显式移到 (0,0) 时才清除 Enter 换行标志。
        // PSReadLine 逐字渲染时也会调用 SetConsoleCursorPosition 来
        // 重置光标到输入行起始列 (如 17,5)，此时不能清标志。
        // clear/cls 才是唯一将光标归零的场景。
        if (new_pos.X == 0 && new_pos.Y == 0)
            bridge->reset_enter_newline();
        vt_message m{};
        m.row = static_cast<short>(new_pos.Y + 1);
        m.col = static_cast<short>(new_pos.X + 1);
        bridge->vt_msg_send(vt_message_id::cursor_position, m);
        vt_msg_apply_state(vt_message_id::cursor_position, m, state, *sb);
        bridge->vt_flush();
    }
    else
    {
        state.cursor.position = new_pos;
    }
    ucomplete(msg);
    return true;
}

inline bool api_largest_window(miniio::io_msg &msg, console_state &state, screen_buffer *, input_buffer *,
                               pipe_bridge *)
{
    auto *r = reinterpret_cast<CONSOLE_GETLARGESTWINDOWSIZE_MSG *>(msg.body + sizeof(CONSOLE_MSG_HEADER));
    r->Size = state.max_window_size;
    ucomplete_sz(msg, sizeof(CONSOLE_GETLARGESTWINDOWSIZE_MSG));
    return true;
}

inline bool api_scroll_sb(miniio::io_msg &msg, console_state &state, screen_buffer *sb, input_buffer *,
                          pipe_bridge *bridge)
{
    auto *r = reinterpret_cast<CONSOLE_SCROLLSCREENBUFFER_MSG *>(msg.body + sizeof(CONSOLE_MSG_HEADER));
    auto &sr = r->ScrollRectangle;
    LOG("[api_scroll_sb] sr=(%d,%d,%d,%d) clip=(%d,%d,%d,%d) dest=(%d,%d) fill_char=0x%X fill_attr=0x%X", sr.Left,
        sr.Top, sr.Right, sr.Bottom, r->ClipRectangle.Left, r->ClipRectangle.Top, r->ClipRectangle.Right,
        r->ClipRectangle.Bottom, r->DestinationOrigin.X, r->DestinationOrigin.Y, r->Fill.Char.UnicodeChar,
        r->Fill.Attributes);
    if (sr.Left > sr.Right || sr.Top > sr.Bottom)
    {
        ucomplete(msg);
        return true;
    }
    if (sb)
        sb->scroll(r->ScrollRectangle, r->ClipRectangle, r->Clip != FALSE, r->DestinationOrigin,
                   static_cast<char32_t>(r->Fill.Char.UnicodeChar), r->Fill.Attributes);

    if (bridge && bridge->vt_out.valid())
    {
        // ── 检测全屏清屏: 滚动矩形覆盖整个 buffer 高度 → 发送 ED2 清屏而非 IL/DL ──
        SHORT buf_height = sb ? sb->size.Y : state.screen_buffer_size.Y;
        bool full_screen_scroll = (sr.Top == 0 && sr.Bottom >= buf_height - 1);
        LOG("[api_scroll_sb] full_screen=%d buf_h=%d sr.Bottom=%d fill_char=0x%X", full_screen_scroll, buf_height,
            sr.Bottom, r->Fill.Char.UnicodeChar);
        if (full_screen_scroll && r->Fill.Char.UnicodeChar == L' ')
        {
            // cls 清屏: 发送 ED2(清屏) + CUP(1,1) 替代 IL/DL 序列
            bridge->vt_append_str("\x1b[2J\x1b[H");
            bridge->vt_flush();
            // ── 同步 state 光标和终端光标到 (0,0) ──
            state.cursor.position = {0, 0};
            bridge->sync_cursor_after_write({0, 0});
            LOG("[api_scroll_sb] cls: sent ED2+H, cursor->(0,0)");
        }
        else
        {
            // ── VT 消息驱动: DECSC → set_scrolling_region → CUP → IL/DL → reset_region → DECRC ──

            // 1) 保存光标
            vt_message m_save{};
            bridge->vt_msg_send(vt_message_id::save_cursor, m_save);

            // 2) 设置滚动区域
            SMALL_RECT cr = r->Clip ? r->ClipRectangle
                                    : SMALL_RECT{0, 0, static_cast<SHORT>(sb ? sb->size.X - 1 : 0),
                                                 static_cast<SHORT>(sb ? sb->size.Y - 1 : 0)};
            vt_message m_region{};
            m_region.scroll_top = static_cast<short>(cr.Top + 1);
            m_region.scroll_bottom = static_cast<short>(cr.Bottom + 1);
            bridge->vt_msg_send(vt_message_id::set_scrolling_region, m_region);
            vt_msg_apply_state(vt_message_id::set_scrolling_region, m_region, state, *sb);

            // 3) 光标移到底部
            vt_message m_cup{};
            m_cup.row = static_cast<short>(sr.Bottom + 1);
            m_cup.col = static_cast<short>(sr.Left + 1);
            bridge->vt_msg_send(vt_message_id::cursor_position, m_cup);

            // 4) 插入/删除行
            SHORT dy = r->DestinationOrigin.Y - sr.Top;
            if (dy < 0)
            {
                vt_message m_il{};
                m_il.count = -dy;
                bridge->vt_msg_send(vt_message_id::insert_lines, m_il);
                vt_msg_apply_state(vt_message_id::insert_lines, m_il, state, *sb);
            }
            else if (dy > 0)
            {
                vt_message m_dl{};
                m_dl.count = dy;
                bridge->vt_msg_send(vt_message_id::delete_lines, m_dl);
                vt_msg_apply_state(vt_message_id::delete_lines, m_dl, state, *sb);
            }

            // 5) 重置滚动区域
            vt_message m_reset{};
            m_reset.scroll_top = 1;
            m_reset.scroll_bottom = 0;
            bridge->vt_msg_send(vt_message_id::set_scrolling_region, m_reset);

            // 6) 恢复光标
            vt_message m_restore{};
            bridge->vt_msg_send(vt_message_id::restore_cursor, m_restore);
            bridge->vt_flush();
        } // end else (!full_screen_scroll)
    }

    ucomplete(msg);
    return true;
}

inline bool api_set_text_attr(miniio::io_msg &msg, console_state &state, screen_buffer *sb, input_buffer *,
                              pipe_bridge *bridge)
{
    auto *r = reinterpret_cast<CONSOLE_SETTEXTATTRIBUTE_MSG *>(msg.body + sizeof(CONSOLE_MSG_HEADER));
    state.default_attributes = r->Attributes;
    if (bridge && bridge->vt_out.valid())
    {
        vt_message m{};
        WORD attr = r->Attributes;
        set_sgr_from_win32_attr(m, attr);
        bridge->vt_msg_send(vt_message_id::sgr, m);
        vt_msg_apply_state(vt_message_id::sgr, m, state, *sb);
        bridge->vt_flush();
    }
    ucomplete(msg);
    return true;
}

inline bool api_set_window_info(miniio::io_msg &msg, console_state &state, screen_buffer *sb, input_buffer *,
                                pipe_bridge *)
{
    auto *r = reinterpret_cast<CONSOLE_SETWINDOWINFO_MSG *>(msg.body + sizeof(CONSOLE_MSG_HEADER));
    if (r->Absolute)
    {
        state.current_window_size.X = static_cast<SHORT>(r->Window.Right - r->Window.Left + 1);
        state.current_window_size.Y = static_cast<SHORT>(r->Window.Bottom - r->Window.Top + 1);
        state.screen_buffer_size = state.current_window_size;
    }
    else
    {
        state.current_window_size.X += static_cast<SHORT>(r->Window.Right - r->Window.Left);
        state.current_window_size.Y += static_cast<SHORT>(r->Window.Bottom - r->Window.Top);
        if (state.current_window_size.X < 1)
            state.current_window_size.X = 1;
        if (state.current_window_size.Y < 1)
            state.current_window_size.Y = 1;
        // ConPTY 模式无滚动缓冲区: buffer size 始终与窗口大小一致
        state.screen_buffer_size = state.current_window_size;
    }
    if (sb)
        sb->resize(state.current_window_size);
    ucomplete(msg);
    return true;
}

inline bool api_read_output_string(miniio::io_msg &msg, console_state &, screen_buffer *sb, input_buffer *,
                                   pipe_bridge *)
{
    auto *r = reinterpret_cast<CONSOLE_READCONSOLEOUTPUTSTRING_MSG *>(msg.body + sizeof(CONSOLE_MSG_HEADER));
    if (r->StringType != CONSOLE_ASCII && r->StringType != CONSOLE_REAL_UNICODE &&
        r->StringType != CONSOLE_FALSE_UNICODE && r->StringType != CONSOLE_ATTRIBUTE)
    {
        ucomplete(msg);
        return true;
    }
    if (sb)
    {
        if (r->StringType == CONSOLE_ATTRIBUTE)
        {
            auto *out = reinterpret_cast<WORD *>(msg.body + sizeof(CONSOLE_MSG_HEADER) +
                                                 sizeof(CONSOLE_READCONSOLEOUTPUTSTRING_MSG));
            auto maxn = (sizeof(msg.body) - sizeof(CONSOLE_MSG_HEADER) - sizeof(CONSOLE_READCONSOLEOUTPUTSTRING_MSG)) /
                        sizeof(WORD);
            r->NumRecords = static_cast<ULONG>(sb->read_attrs(r->ReadCoord, out, maxn));
        }
        else
        {
            auto *out = reinterpret_cast<wchar_t *>(msg.body + sizeof(CONSOLE_MSG_HEADER) +
                                                    sizeof(CONSOLE_READCONSOLEOUTPUTSTRING_MSG));
            auto maxn = (sizeof(msg.body) - sizeof(CONSOLE_MSG_HEADER) - sizeof(CONSOLE_READCONSOLEOUTPUTSTRING_MSG)) /
                        sizeof(wchar_t);
            r->NumRecords = static_cast<ULONG>(sb->read_wchars(r->ReadCoord, out, maxn));
        }
    }
    else
    {
        r->NumRecords = 0;
    }
    ucomplete_sz(msg, sizeof(CONSOLE_READCONSOLEOUTPUTSTRING_MSG));
    return true;
}

inline bool api_write_console_input(miniio::io_msg &msg, console_state &, screen_buffer *, input_buffer *inp,
                                    pipe_bridge *)
{
    auto *r = reinterpret_cast<CONSOLE_WRITECONSOLEINPUT_MSG *>(msg.body + sizeof(CONSOLE_MSG_HEADER));
    auto *records =
        reinterpret_cast<INPUT_RECORD *>(msg.body + sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_WRITECONSOLEINPUT_MSG));
    auto ib = msg.descriptor.InputSize - sizeof(CONSOLE_MSG_HEADER) - sizeof(CONSOLE_WRITECONSOLEINPUT_MSG);
    auto nrec = static_cast<size_t>(ib / sizeof(INPUT_RECORD));
    size_t written = 0;
    if (inp && nrec > 0)
        written = inp->write(records, nrec);
    r->NumRecords = static_cast<ULONG>(written);
    ucomplete_sz(msg, sizeof(CONSOLE_WRITECONSOLEINPUT_MSG));
    return true;
}

inline bool api_write_console_output(miniio::io_msg &msg, console_state &, screen_buffer *sb, input_buffer *,
                                     pipe_bridge *bridge)
{
    auto *r = reinterpret_cast<CONSOLE_WRITECONSOLEOUTPUT_MSG *>(msg.body + sizeof(CONSOLE_MSG_HEADER));
    SMALL_RECT &cr = r->CharRegion;
    if (cr.Left > cr.Right || cr.Top > cr.Bottom)
    {
        cr.Left = cr.Right = cr.Top = cr.Bottom = 0;
        ucomplete_sz(msg, sizeof(CONSOLE_WRITECONSOLEOUTPUT_MSG));
        return true;
    }
    SMALL_RECT orig = cr;
    if (sb)
    {
        // CHAR_INFO → row 转换: 逐行调用 from_char_info
        auto *data = reinterpret_cast<const CHAR_INFO *>(msg.body + sizeof(CONSOLE_MSG_HEADER) +
                                                         sizeof(CONSOLE_WRITECONSOLEOUTPUT_MSG));
        SHORT w = orig.Right - orig.Left + 1;
        for (SHORT y = orig.Top; y <= orig.Bottom && y < sb->size.Y; ++y)
            sb->row_from_ci(y, data + (y - orig.Top) * w, static_cast<uint16_t>(w), static_cast<uint16_t>(orig.Left));
        cr = orig;
    }
    else
    {
        cr.Left = cr.Right = cr.Top = cr.Bottom = 0;
    }

    if (bridge && bridge->vt_out.valid() && sb)
    {
        // ── DECSC → 逐行 CUP + SGR + text batch → DECRC ──
        // 将连续相同属性的 cell 打包为单个 text vt_message，统一走 vt_msg_send
        vt_message m{};
        bridge->vt_msg_send(vt_message_id::save_cursor, m);

        std::u32string text_buf; // 暂存同属性 cell 的字符序列
        text_buf.reserve(static_cast<size_t>(orig.Right - orig.Left + 1));

        for (SHORT y = orig.Top; y <= orig.Bottom; ++y)
        {
            m = vt_message{};
            m.row = static_cast<short>(y + 1);
            m.col = static_cast<short>(orig.Left + 1);
            bridge->vt_msg_send(vt_message_id::cursor_position, m);

            WORD last_attr = 0xFFFF;
            text_buf.clear();

            auto flush_text = [&]() {
                if (!text_buf.empty())
                {
                    m = vt_message{};
                    m.text = text_buf;
                    bridge->vt_msg_send(vt_message_id::text, m);
                    text_buf.clear();
                }
            };

            for (SHORT x = orig.Left; x <= orig.Right; ++x)
            {
                WORD cell_attr = sb->attr_at({x, y});
                if (cell_attr != last_attr)
                {
                    flush_text();
                    m = vt_message{};
                    set_sgr_from_win32_attr(m, cell_attr);
                    bridge->vt_msg_send(vt_message_id::sgr, m);
                    last_attr = cell_attr;
                }
                text_buf.push_back(sb->at_u32({x, y}));
            }
            flush_text(); // 行尾收尾
        }

        m = vt_message{};
        bridge->vt_msg_send(vt_message_id::restore_cursor, m);
        bridge->vt_flush();
    }

    ucomplete_sz(msg, sizeof(CONSOLE_WRITECONSOLEOUTPUT_MSG));
    return true;
}

inline bool api_write_output_string(miniio::io_msg &msg, console_state &state, screen_buffer *sb, input_buffer *,
                                    pipe_bridge *bridge)
{
    auto *r = reinterpret_cast<CONSOLE_WRITECONSOLEOUTPUTSTRING_MSG *>(msg.body + sizeof(CONSOLE_MSG_HEADER));
    if (r->StringType != CONSOLE_ASCII && r->StringType != CONSOLE_REAL_UNICODE &&
        r->StringType != CONSOLE_FALSE_UNICODE && r->StringType != CONSOLE_ATTRIBUTE)
    {
        ucomplete(msg);
        return true;
    }

    const wchar_t *chars_w = nullptr;
    if (sb)
    {
        if (r->StringType == CONSOLE_ATTRIBUTE)
        {
            auto *attrs = reinterpret_cast<const WORD *>(msg.body + sizeof(CONSOLE_MSG_HEADER) +
                                                         sizeof(CONSOLE_WRITECONSOLEOUTPUTSTRING_MSG));
            auto ib =
                msg.descriptor.InputSize - sizeof(CONSOLE_MSG_HEADER) - sizeof(CONSOLE_WRITECONSOLEOUTPUTSTRING_MSG);
            r->NumRecords = static_cast<ULONG>(sb->write_attr_seq(r->WriteCoord, attrs, ib / sizeof(WORD)));
        }
        else
        {
            chars_w = reinterpret_cast<const wchar_t *>(msg.body + sizeof(CONSOLE_MSG_HEADER) +
                                                        sizeof(CONSOLE_WRITECONSOLEOUTPUTSTRING_MSG));
            auto ib =
                msg.descriptor.InputSize - sizeof(CONSOLE_MSG_HEADER) - sizeof(CONSOLE_WRITECONSOLEOUTPUTSTRING_MSG);
            auto wlen = ib / sizeof(wchar_t);
            // convert wchar_t → char32_t for screen_buffer
            std::array<char32_t, 256> u32buf;
            size_t u32len = 0;
            const wchar_t *wp = chars_w;
            const wchar_t *wend = wp + wlen;
            while (wp != wend && u32len < u32buf.size())
                u32buf[u32len++] = to_char32_surrogate(wp, wend);
            r->NumRecords = static_cast<ULONG>(sb->write_char32(r->WriteCoord, u32buf.data(), u32len));
        }
    }
    else
    {
        r->NumRecords = 0;
    }

    if (bridge && bridge->vt_out.valid() && sb && r->NumRecords > 0 && chars_w)
    {
        // ── WriteConsoleOutputString 不移动光标 ──
        // 终端: DECSC → CUP → text → DECRC (光标回到原位)
        // 状态: 仅更新 screen_buffer 格子，不改变 state.cursor
        vt_message m{};
        bridge->vt_msg_send(vt_message_id::save_cursor, m);

        m = vt_message{};
        m.row = static_cast<short>(r->WriteCoord.Y + 1);
        m.col = static_cast<short>(r->WriteCoord.X + 1);
        bridge->vt_msg_send(vt_message_id::cursor_position, m);

        m = vt_message{};
        set_sgr_from_win32_attr(m, state.default_attributes);
        bridge->vt_msg_send(vt_message_id::sgr, m);

        std::u32string u32text(r->NumRecords, U'\0');
        for (ULONG i = 0; i < r->NumRecords; ++i)
            u32text[i] = to_char32(chars_w[i]);
        m = vt_message{};
        m.text = u32text;
        bridge->vt_msg_send(vt_message_id::text, m);

        m = vt_message{};
        bridge->vt_msg_send(vt_message_id::restore_cursor, m);
        bridge->vt_flush();
    }

    ucomplete_sz(msg, sizeof(CONSOLE_WRITECONSOLEOUTPUTSTRING_MSG));
    return true;
}

inline bool api_read_console_output(miniio::io_msg &msg, console_state &state, screen_buffer *sb, input_buffer *,
                                    pipe_bridge *)
{
    auto *r = reinterpret_cast<CONSOLE_READCONSOLEOUTPUT_MSG *>(msg.body + sizeof(CONSOLE_MSG_HEADER));
    SMALL_RECT &cr = r->CharRegion;
    if (cr.Left > cr.Right || cr.Top > cr.Bottom)
    {
        cr.Left = cr.Right = cr.Top = cr.Bottom = 0;
        ucomplete_sz(msg, sizeof(CONSOLE_READCONSOLEOUTPUT_MSG));
        return true;
    }
    if (sb)
    {
        // 逐行导出 CHAR_INFO
        SHORT w = cr.Right - cr.Left + 1;
        for (SHORT y = cr.Top; y <= cr.Bottom && y < sb->size.Y; ++y)
        {
            // row_to_ci 填充整行, 仅复制 [Left..Right] 段
            std::vector<CHAR_INFO> tmp(static_cast<size_t>(sb->size.X));
            sb->row_to_ci(y, tmp.data());
            auto *dst = reinterpret_cast<CHAR_INFO *>(msg.body + sizeof(CONSOLE_MSG_HEADER) +
                                                      sizeof(CONSOLE_READCONSOLEOUTPUT_MSG)) +
                        (y - cr.Top) * w;
            std::memcpy(dst, tmp.data() + cr.Left, w * sizeof(CHAR_INFO));
        }
    }
    else
        cr.Left = cr.Right = cr.Top = cr.Bottom = 0;
    state.cursor_position_dirty = true;
    ucomplete_sz(msg, sizeof(CONSOLE_READCONSOLEOUTPUT_MSG));
    return true;
}

// ── GetTitle / SetTitle (char32_t ↔ wchar_t 边界) ──

inline bool api_get_title(miniio::io_msg &msg, console_state &state, screen_buffer *, input_buffer *, pipe_bridge *)
{
    auto *r = reinterpret_cast<CONSOLE_GETTITLE_MSG *>(msg.body + sizeof(CONSOLE_MSG_HEADER));
    auto &src = r->Original ? state.original_title : state.title;
    std::wstring wstr;
    convert_u32_to_wstr(src, wstr);

    if (r->Unicode)
    {
        auto *out = reinterpret_cast<wchar_t *>(msg.body + sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_GETTITLE_MSG));
        auto maxc = (sizeof(msg.body) - sizeof(CONSOLE_MSG_HEADER) - sizeof(CONSOLE_GETTITLE_MSG)) / sizeof(wchar_t);
        auto cp = wstr.size() < maxc ? wstr.size() : maxc;
        std::memcpy(out, wstr.data(), cp * sizeof(wchar_t));
        if (cp < maxc)
            out[cp] = L'\0';
        r->TitleLength = static_cast<ULONG>(cp * sizeof(wchar_t));
        ucomplete_sz(msg, sizeof(CONSOLE_GETTITLE_MSG) + static_cast<ULONG>((cp + 1) * sizeof(wchar_t)));
    }
    else
    {
        auto *out = reinterpret_cast<char *>(msg.body + sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_GETTITLE_MSG));
        auto maxb = sizeof(msg.body) - sizeof(CONSOLE_MSG_HEADER) - sizeof(CONSOLE_GETTITLE_MSG);
        size_t n = convert_wide_to_ansi_raw(wstr.data(), wstr.size(), CP_ACP, out, maxb);
        r->TitleLength = static_cast<ULONG>(n);
        ucomplete_sz(msg, sizeof(CONSOLE_GETTITLE_MSG) + static_cast<ULONG>(n + 1));
    }
    return true;
}

inline bool api_set_title(miniio::io_msg &msg, console_state &state, screen_buffer *, input_buffer *,
                          pipe_bridge *bridge)
{
    auto *in = reinterpret_cast<const wchar_t *>(msg.body + sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_SETTITLE_MSG));
    auto ib = msg.descriptor.InputSize - sizeof(CONSOLE_MSG_HEADER) - sizeof(CONSOLE_SETTITLE_MSG);
    auto ic = ib / sizeof(wchar_t);
    auto cp = ic < 256 ? ic : 255;
    // wchar_t → char32_t
    std::u32string u32title;
    convert_utf16_to_u32(std::wstring_view{in, cp}, u32title);
    if (state.title.empty())
        state.original_title = u32title;
    state.title = std::move(u32title);

    // VT 同步: OSC 0 设置终端标题
    if (bridge && bridge->vt_out.valid())
    {
        vt_message m{};
        m.title = state.title;
        bridge->vt_msg_send(vt_message_id::set_window_title, m);
        bridge->vt_flush();
    }

    ucomplete(msg);
    return true;
}

// ════════════════════════════════════════════════════════
// L3 API handlers (对标 conmsgl3.h + ApiDispatchers.cpp)
//
// 第一类 (20 个): ConPTY 下有实际意义，直接读写 console_state
// 第二类 (24 个): 原始全部路由到 ServerDeprecatedApi → ucomplete
// ════════════════════════════════════════════════════════

// ── 0x01 GetMouseInfo ──
inline bool api_l3_get_mouse_info(miniio::io_msg &msg, console_state &state, screen_buffer *, input_buffer *,
                                  pipe_bridge *)
{
    auto *r = reinterpret_cast<CONSOLE_GETMOUSEINFO_MSG *>(msg.body + sizeof(CONSOLE_MSG_HEADER));
    r->NumButtons = state.mouse_buttons;
    ucomplete_sz(msg, sizeof(CONSOLE_GETMOUSEINFO_MSG));
    return true;
}

// ── 0x03 GetFontSize ──
inline bool api_l3_get_font_size(miniio::io_msg &msg, console_state &state, screen_buffer *, input_buffer *,
                                 pipe_bridge *)
{
    auto *r = reinterpret_cast<CONSOLE_GETFONTSIZE_MSG *>(msg.body + sizeof(CONSOLE_MSG_HEADER));
    r->FontSize = state.font_size; // 简化: 返回当前字体尺寸 (所有 index 相同)
    (void)r->FontIndex;
    ucomplete_sz(msg, sizeof(CONSOLE_GETFONTSIZE_MSG));
    return true;
}

// ── 0x04 GetCurrentFont ──
inline bool api_l3_get_current_font(miniio::io_msg &msg, console_state &state, screen_buffer *, input_buffer *,
                                    pipe_bridge *)
{
    auto *r = reinterpret_cast<CONSOLE_CURRENTFONT_MSG *>(msg.body + sizeof(CONSOLE_MSG_HEADER));
    r->FontIndex = state.font_index;
    r->FontSize = state.font_size;
    r->FontFamily = state.font_family;
    r->FontWeight = state.font_weight;
    std::memcpy(r->FaceName, state.face_name, sizeof(state.face_name));
    ucomplete_sz(msg, sizeof(CONSOLE_CURRENTFONT_MSG));
    return true;
}

// ── 0x0D SetDisplayMode ──
inline bool api_l3_set_display_mode(miniio::io_msg &msg, console_state &state, screen_buffer *sb, input_buffer *,
                                    pipe_bridge *)
{
    auto *r = reinterpret_cast<CONSOLE_SETDISPLAYMODE_MSG *>(msg.body + sizeof(CONSOLE_MSG_HEADER));
    state.display_mode = r->dwFlags;
    // CONSOLE_FULLSCREEN_MODE (1) vs CONSOLE_WINDOWED_MODE (2)
    // ConPTY 下忽略全屏，但 resize screen buffer
    if (r->ScreenBufferDimensions.X > 0 && r->ScreenBufferDimensions.Y > 0)
    {
        state.screen_buffer_size = r->ScreenBufferDimensions;
        state.current_window_size = r->ScreenBufferDimensions;
        if (sb)
            sb->resize(r->ScreenBufferDimensions);
    }
    ucomplete_sz(msg, sizeof(CONSOLE_SETDISPLAYMODE_MSG));
    return true;
}

// ── 0x11 GetDisplayMode ──
inline bool api_l3_get_display_mode(miniio::io_msg &msg, console_state &state, screen_buffer *, input_buffer *,
                                    pipe_bridge *)
{
    auto *r = reinterpret_cast<CONSOLE_GETDISPLAYMODE_MSG *>(msg.body + sizeof(CONSOLE_MSG_HEADER));
    r->ModeFlags = state.display_mode;
    ucomplete_sz(msg, sizeof(CONSOLE_GETDISPLAYMODE_MSG));
    return true;
}

// ── 0x12 AddAlias ──
// 消息格式: CONSOLE_ADDALIAS_MSG + Exe(ExeLen bytes) + Source(SrcLen bytes) + Target(TgtLen bytes)
// 注意: SourceLength/TargetLength/ExeLength 是字节数 (USHORT)
inline bool api_l3_add_alias(miniio::io_msg &msg, console_state &state, screen_buffer *, input_buffer *, pipe_bridge *)
{
    auto *r = reinterpret_cast<CONSOLE_ADDALIAS_MSG *>(msg.body + sizeof(CONSOLE_MSG_HEADER));
    auto *db = msg.body + sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_ADDALIAS_MSG);

    auto exe_len_bytes = static_cast<size_t>(r->ExeLength);
    auto src_len_bytes = static_cast<size_t>(r->SourceLength);
    auto tgt_len_bytes = static_cast<size_t>(r->TargetLength);

    if (r->Unicode)
    {
        auto *src = reinterpret_cast<const wchar_t *>(db + exe_len_bytes);
        auto *tgt = reinterpret_cast<const wchar_t *>(db + exe_len_bytes + src_len_bytes);
        auto src_chars = src_len_bytes / sizeof(wchar_t);
        auto tgt_chars = tgt_len_bytes / sizeof(wchar_t);

        if (src_chars > 0 && tgt_chars > 0)
            state.aliases.emplace(std::wstring_view{src, src_chars}, std::wstring_view{tgt, tgt_chars});
    }
    else
    {
        auto *src_a = reinterpret_cast<const char *>(db + exe_len_bytes);
        auto *tgt_a = reinterpret_cast<const char *>(db + exe_len_bytes + src_len_bytes);
        if (src_len_bytes > 0 && tgt_len_bytes > 0)
        {
            std::wstring wsrc, wtgt;
            convert_ansi_to_wstr(src_a, src_len_bytes, CP_ACP, wsrc);
            convert_ansi_to_wstr(tgt_a, tgt_len_bytes, CP_ACP, wtgt);
            if (!wsrc.empty() && !wtgt.empty())
                state.aliases.emplace(std::move(wsrc), std::move(wtgt));
        }
    }

    ucomplete(msg);
    return true;
}

// ── 0x13 GetAlias ──
// 消息格式同 AddAlias: Exe(ExeLen bytes) + Source(SrcLen bytes)
// SourceLength/TargetLength/ExeLength 均为字节数 (USHORT)
inline bool api_l3_get_alias(miniio::io_msg &msg, console_state &state, screen_buffer *, input_buffer *, pipe_bridge *)
{
    auto *r = reinterpret_cast<CONSOLE_GETALIAS_MSG *>(msg.body + sizeof(CONSOLE_MSG_HEADER));
    auto *db = msg.body + sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_GETALIAS_MSG);

    auto exe_len_bytes = static_cast<size_t>(r->ExeLength);
    auto src_len_bytes = static_cast<size_t>(r->SourceLength);

    if (r->Unicode)
    {
        auto *src = reinterpret_cast<const wchar_t *>(db + exe_len_bytes);
        auto src_chars = src_len_bytes / sizeof(wchar_t);

        if (src_chars > 0)
        {
            std::wstring key{src, src_chars};
            auto it = state.aliases.find(key);
            if (it != state.aliases.end())
            {
                auto &wval = it->second;
                auto *tgt_out = reinterpret_cast<wchar_t *>(db + exe_len_bytes);
                auto available_chars =
                    (sizeof(msg.body) - sizeof(CONSOLE_MSG_HEADER) - sizeof(CONSOLE_GETALIAS_MSG) - exe_len_bytes) /
                    sizeof(wchar_t);
                if (wval.size() + 1 <= available_chars)
                {
                    std::memcpy(tgt_out, wval.data(), wval.size() * sizeof(wchar_t));
                    tgt_out[wval.size()] = L'\0';
                    r->TargetLength = static_cast<USHORT>(wval.size() * sizeof(wchar_t));
                }
                ucomplete_sz(msg, sizeof(CONSOLE_GETALIAS_MSG));
                return true;
            }
        }
    }
    else
    {
        auto *src_a = reinterpret_cast<const char *>(db + exe_len_bytes);
        if (src_len_bytes > 0)
        {
            std::wstring key;
            convert_ansi_to_wstr(src_a, src_len_bytes, CP_ACP, key);
            if (!key.empty())
            {
                auto it = state.aliases.find(key);
                if (it != state.aliases.end())
                {
                    auto &wval = it->second;
                    auto *tgt_out = reinterpret_cast<char *>(db + exe_len_bytes);
                    auto available_bytes =
                        sizeof(msg.body) - sizeof(CONSOLE_MSG_HEADER) - sizeof(CONSOLE_GETALIAS_MSG) - exe_len_bytes;
                    size_t n = convert_wide_to_ansi_raw(wval.data(), wval.size(), CP_ACP, tgt_out, available_bytes);
                    if (n > 0)
                        r->TargetLength = static_cast<USHORT>(n);
                    ucomplete_sz(msg, sizeof(CONSOLE_GETALIAS_MSG));
                    return true;
                }
            }
        }
    }
    r->TargetLength = 0;
    ucomplete_sz(msg, sizeof(CONSOLE_GETALIAS_MSG));
    return true;
}

// ── 0x14 GetAliasesLength ──
inline bool api_l3_get_aliases_length(miniio::io_msg &msg, console_state &state, screen_buffer *, input_buffer *,
                                      pipe_bridge *)
{
    auto *r = reinterpret_cast<CONSOLE_GETALIASESLENGTH_MSG *>(msg.body + sizeof(CONSOLE_MSG_HEADER));
    ULONG total = 0;
    if (r->Unicode)
    {
        for (const auto &[k, v] : state.aliases)
            total += static_cast<ULONG>((k.size() + 1 + v.size() + 1) * sizeof(wchar_t));
    }
    else
    {
        for (const auto &[k, v] : state.aliases)
        {
            auto k_len = wstr_to_ansi_len(std::wstring_view{k.data(), k.size()}, CP_ACP);
            auto v_len = wstr_to_ansi_len(std::wstring_view{v.data(), v.size()}, CP_ACP);
            if (k_len > 0 && v_len > 0)
                total += static_cast<ULONG>(k_len + 1 + v_len + 1);
        }
    }
    r->AliasesLength = total;
    ucomplete_sz(msg, sizeof(CONSOLE_GETALIASESLENGTH_MSG));
    return true;
}

// ── 0x15 GetAliasExesLength ──
inline bool api_l3_get_alias_exes_length(miniio::io_msg &msg, console_state &state, screen_buffer *, input_buffer *,
                                         pipe_bridge *)
{
    auto *r = reinterpret_cast<CONSOLE_GETALIASEXESLENGTH_MSG *>(msg.body + sizeof(CONSOLE_MSG_HEADER));
    r->AliasExesLength = 0; // ConPTY 下不跟踪 exe 名称
    ucomplete_sz(msg, sizeof(CONSOLE_GETALIASEXESLENGTH_MSG));
    return true;
}

// ── 0x16 GetAliases ──
inline bool api_l3_get_aliases(miniio::io_msg &msg, console_state &state, screen_buffer *, input_buffer *,
                               pipe_bridge *)
{
    auto *r = reinterpret_cast<CONSOLE_GETALIASES_MSG *>(msg.body + sizeof(CONSOLE_MSG_HEADER));
    ULONG written = 0;
    if (r->Unicode)
    {
        auto *out = reinterpret_cast<wchar_t *>(msg.body + sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_GETALIASES_MSG));
        auto maxw = (sizeof(msg.body) - sizeof(CONSOLE_MSG_HEADER) - sizeof(CONSOLE_GETALIASES_MSG)) / sizeof(wchar_t);
        for (const auto &[k, v] : state.aliases)
        {
            ULONG need = static_cast<ULONG>(k.size() + 1 + v.size() + 1);
            if (written + need > maxw)
                break;
            std::memcpy(out + written, k.data(), k.size() * sizeof(wchar_t));
            written += static_cast<ULONG>(k.size());
            out[written++] = L'\0';
            std::memcpy(out + written, v.data(), v.size() * sizeof(wchar_t));
            written += static_cast<ULONG>(v.size());
            out[written++] = L'\0';
        }
        r->AliasesBufferLength = written * sizeof(wchar_t);
        ucomplete_sz(msg, sizeof(CONSOLE_GETALIASES_MSG) + written * sizeof(wchar_t));
    }
    else
    {
        auto *out = reinterpret_cast<char *>(msg.body + sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_GETALIASES_MSG));
        auto maxb = sizeof(msg.body) - sizeof(CONSOLE_MSG_HEADER) - sizeof(CONSOLE_GETALIASES_MSG);
        for (const auto &[k, v] : state.aliases)
        {
            auto k_len = wstr_to_ansi_len(std::wstring_view{k.data(), k.size()}, CP_ACP);
            auto v_len = wstr_to_ansi_len(std::wstring_view{v.data(), v.size()}, CP_ACP);
            if (k_len == 0 || v_len == 0)
                continue;
            ULONG need = static_cast<ULONG>(k_len + 1 + v_len + 1);
            if (written + need > maxb)
                break;
            convert_wide_to_ansi_raw(k.data(), k.size(), CP_ACP, out + written, maxb - written);
            written += static_cast<ULONG>(k_len) + 1; // +1 for \0 from convert_wide_to_ansi_raw
            convert_wide_to_ansi_raw(v.data(), v.size(), CP_ACP, out + written, maxb - written);
            written += static_cast<ULONG>(v_len) + 1;
        }
        r->AliasesBufferLength = written;
        ucomplete_sz(msg, sizeof(CONSOLE_GETALIASES_MSG) + written);
    }
    return true;
}

// ── 0x17 GetAliasExes ──
inline bool api_l3_get_alias_exes(miniio::io_msg &msg, console_state &, screen_buffer *, input_buffer *, pipe_bridge *)
{
    auto *r = reinterpret_cast<CONSOLE_GETALIASEXES_MSG *>(msg.body + sizeof(CONSOLE_MSG_HEADER));
    r->AliasExesBufferLength = 0;
    ucomplete_sz(msg, sizeof(CONSOLE_GETALIASEXES_MSG));
    return true;
}

// ── 0x18 ExpungeCommandHistory ──
inline bool api_l3_expunge_history(miniio::io_msg &msg, console_state &state, screen_buffer *, input_buffer *,
                                   pipe_bridge *)
{
    state.command_history.clear();
    ucomplete(msg);
    return true;
}

// ── 0x19 SetNumberOfCommands ──
inline bool api_l3_set_num_commands(miniio::io_msg &msg, console_state &state, screen_buffer *, input_buffer *,
                                    pipe_bridge *)
{
    auto *r = reinterpret_cast<CONSOLE_SETNUMBEROFCOMMANDS_MSG *>(msg.body + sizeof(CONSOLE_MSG_HEADER));
    state.history_num_buffers = r->NumCommands;
    ucomplete(msg);
    return true;
}

// ── 0x1A GetCommandHistoryLength ──
// ConPTY 模式下历史由 pipe_bridge 内部管理，不通过 ConDrv CommandHistory API 暴露。
inline bool api_l3_get_history_length(miniio::io_msg &msg, console_state &, screen_buffer *, input_buffer *,
                                      pipe_bridge *)
{
    auto *r = reinterpret_cast<CONSOLE_GETCOMMANDHISTORYLENGTH_MSG *>(msg.body + sizeof(CONSOLE_MSG_HEADER));
    r->CommandHistoryLength = 0;
    ucomplete_sz(msg, sizeof(CONSOLE_GETCOMMANDHISTORYLENGTH_MSG));
    return true;
}

// ── 0x1B GetCommandHistory ──
// ConPTY 模式下历史由 pipe_bridge 内部管理，不通过 ConDrv CommandHistory API 暴露。
inline bool api_l3_get_history(miniio::io_msg &msg, console_state &, screen_buffer *, input_buffer *, pipe_bridge *)
{
    auto *r = reinterpret_cast<CONSOLE_GETCOMMANDHISTORY_MSG *>(msg.body + sizeof(CONSOLE_MSG_HEADER));
    r->CommandBufferLength = 0;
    ucomplete_sz(msg, sizeof(CONSOLE_GETCOMMANDHISTORY_MSG));
    return true;
}

// ── 0x1F GetConsoleWindow ──
inline bool api_l3_get_console_window(miniio::io_msg &msg, console_state &, screen_buffer *, input_buffer *,
                                      pipe_bridge *)
{
    auto *r = reinterpret_cast<CONSOLE_GETCONSOLEWINDOW_MSG *>(msg.body + sizeof(CONSOLE_MSG_HEADER));
    r->hwnd = ::GetConsoleWindow(); // 返回实际 HWND (可能 NULL)
    ucomplete_sz(msg, sizeof(CONSOLE_GETCONSOLEWINDOW_MSG));
    return true;
}

// ── 0x28 GetSelectionInfo ──
inline bool api_l3_get_selection_info(miniio::io_msg &msg, console_state &state, screen_buffer *, input_buffer *,
                                      pipe_bridge *)
{
    auto *r = reinterpret_cast<CONSOLE_GETSELECTIONINFO_MSG *>(msg.body + sizeof(CONSOLE_MSG_HEADER));
    r->SelectionInfo = state.selection_info;
    ucomplete_sz(msg, sizeof(CONSOLE_GETSELECTIONINFO_MSG));
    return true;
}

// ── 0x29 GetConsoleProcessList ──
// 需要访问 io_state 的 process_list，但 handler 签名不含 io_state。
// 通过 bridge.proc_list/proc_count 间接获取 (pipe_bridge 也维护副本)。
inline bool api_l3_get_process_list(miniio::io_msg &msg, console_state &, screen_buffer *, input_buffer *,
                                    pipe_bridge *bridge)
{
    auto *r = reinterpret_cast<CONSOLE_GETCONSOLEPROCESSLIST_MSG *>(msg.body + sizeof(CONSOLE_MSG_HEADER));
    auto *out =
        reinterpret_cast<DWORD *>(msg.body + sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_GETCONSOLEPROCESSLIST_MSG));
    auto maxc =
        (sizeof(msg.body) - sizeof(CONSOLE_MSG_HEADER) - sizeof(CONSOLE_GETCONSOLEPROCESSLIST_MSG)) / sizeof(DWORD);
    size_t count = 0;
    if (bridge)
    {
        count = bridge->proc_count < maxc ? bridge->proc_count : maxc;
        for (size_t i = 0; i < count; ++i)
            out[i] = bridge->proc_list[bridge->proc_count - 1 - i]; // newest first
    }
    r->dwProcessCount = static_cast<ULONG>(count);
    ucomplete_sz(msg, sizeof(CONSOLE_GETCONSOLEPROCESSLIST_MSG) + static_cast<ULONG>(count * sizeof(DWORD)));
    return true;
}

// ── 0x2A GetHistory ──
inline bool api_l3_get_history_info(miniio::io_msg &msg, console_state &state, screen_buffer *, input_buffer *,
                                    pipe_bridge *)
{
    auto *r = reinterpret_cast<CONSOLE_HISTORY_MSG *>(msg.body + sizeof(CONSOLE_MSG_HEADER));
    r->HistoryBufferSize = static_cast<ULONG>(state.history_buffer_size);
    r->NumberOfHistoryBuffers = static_cast<ULONG>(state.history_num_buffers);
    r->dwFlags = state.history_flags;
    ucomplete_sz(msg, sizeof(CONSOLE_HISTORY_MSG));
    return true;
}

// ── 0x2B SetHistory ──
inline bool api_l3_set_history_info(miniio::io_msg &msg, console_state &state, screen_buffer *, input_buffer *,
                                    pipe_bridge *)
{
    auto *r = reinterpret_cast<CONSOLE_HISTORY_MSG *>(msg.body + sizeof(CONSOLE_MSG_HEADER));
    state.history_buffer_size = r->HistoryBufferSize;
    state.history_num_buffers = r->NumberOfHistoryBuffers;
    state.history_flags = r->dwFlags;
    ucomplete_sz(msg, sizeof(CONSOLE_HISTORY_MSG));
    return true;
}

// ── 0x2C SetCurrentFont ──
inline bool api_l3_set_current_font(miniio::io_msg &msg, console_state &state, screen_buffer *, input_buffer *,
                                    pipe_bridge *)
{
    auto *r = reinterpret_cast<CONSOLE_CURRENTFONT_MSG *>(msg.body + sizeof(CONSOLE_MSG_HEADER));
    state.font_index = r->FontIndex;
    state.font_size = r->FontSize;
    state.font_family = r->FontFamily;
    state.font_weight = r->FontWeight;
    std::memcpy(state.face_name, r->FaceName, sizeof(state.face_name));
    ucomplete_sz(msg, sizeof(CONSOLE_CURRENTFONT_MSG));
    return true;
}

// ── 第二类 L3: 废弃 API (对标 ServerDeprecatedApi) ──
inline bool api_l3_deprecated(miniio::io_msg &msg, console_state &, screen_buffer *, input_buffer *, pipe_bridge *)
{
    ucomplete(msg);
    return true;
}

} // namespace conpty
