// ── conpty/api_handlers.hpp ────────────────────────
// Layer 3: Console API handler。这里把 ConDrv 的用户态 API 消息转换为
// console_state、screen_buffer、input_buffer 和 VT 输出操作。
#pragma once
#include <windows.h>
#include <algorithm>
#include <cstring>
#include <cwctype>
#include <array>
#include <vector>
#include "miniio/io_thread.hpp"
#include "os/Console/conmsgl1.h"
#include "os/Console/conmsgl2.h"
#include "os/Console/conmsgl3.h"
#include "console_state.hpp"
#include "screen_buffer.hpp"
#include "input_buffer.hpp"
#include "pipe_bridge.hpp"
#include "viewport_render.hpp"
#include "conpty_vt_parser.hpp"
#include "vt_msg_dispatch.hpp"
#include "char_convert.hpp"
#include "char_width.hpp"
#include "perf_diag.hpp"
#include "utility/log.hpp"
#include "ntapi/consolenslmode.hpp"

namespace conpty
{

inline constexpr LONG status_invalid_parameter = 0xC000000D;
inline constexpr LONG status_buffer_too_small = 0xC0000023;
inline constexpr LONG status_illegal_function = 0xC00000AF;
inline constexpr LONG status_not_implemented = 0xC0000002;
inline constexpr LONG status_not_supported = 0xC00000BB;
inline constexpr UINT code_page_japanese = 932;
inline constexpr UINT code_page_korean = 949;
inline constexpr UINT code_page_chinese_simplified = 936;
inline constexpr UINT code_page_chinese_traditional = 950;
inline constexpr DWORD valid_input_modes = ENABLE_LINE_INPUT | ENABLE_PROCESSED_INPUT | ENABLE_ECHO_INPUT |
                                           ENABLE_WINDOW_INPUT | ENABLE_MOUSE_INPUT | ENABLE_VIRTUAL_TERMINAL_INPUT;
inline constexpr DWORD private_input_modes =
    ENABLE_INSERT_MODE | ENABLE_QUICK_EDIT_MODE | ENABLE_EXTENDED_FLAGS | ENABLE_AUTO_POSITION;
inline constexpr DWORD valid_output_modes = ENABLE_PROCESSED_OUTPUT | ENABLE_WRAP_AT_EOL_OUTPUT |
                                            ENABLE_VIRTUAL_TERMINAL_PROCESSING | DISABLE_NEWLINE_AUTO_RETURN |
                                            ENABLE_LVB_GRID_WORLDWIDE;

inline std::wstring lower_wstring(std::wstring_view text)
{
    std::wstring result{text};
    for (auto &ch : result)
        if (ch >= L'A' && ch <= L'Z')
            ch = static_cast<wchar_t>(ch - L'A' + L'a');
    return result;
}

inline const std::wstring *find_alias_value(const console_state &state, std::wstring_view exe, std::wstring_view source)
{
    auto exe_key = lower_wstring(exe);
    auto source_key = lower_wstring(source);
    if (auto exe_it = state.aliases_by_exe.find(exe_key); exe_it != state.aliases_by_exe.end())
        if (auto alias_it = exe_it->second.find(source_key); alias_it != exe_it->second.end())
            return &alias_it->second;
    if (auto alias_it = state.aliases.find(source_key); alias_it != state.aliases.end())
        return &alias_it->second;
    return nullptr;
}

inline bool is_east_asian_code_page(UINT code_page) noexcept
{
    return code_page == code_page_japanese || code_page == code_page_korean ||
           code_page == code_page_chinese_simplified || code_page == code_page_chinese_traditional;
}

inline LANGID lang_id_from_console_output_code_page(UINT output_code_page) noexcept
{
    switch (output_code_page)
    {
    case code_page_japanese:
        return MAKELANGID(LANG_JAPANESE, SUBLANG_DEFAULT);
    case code_page_korean:
        return MAKELANGID(LANG_KOREAN, SUBLANG_KOREAN);
    case code_page_chinese_simplified:
        return MAKELANGID(LANG_CHINESE, SUBLANG_CHINESE_SIMPLIFIED);
    case code_page_chinese_traditional:
        return MAKELANGID(LANG_CHINESE, SUBLANG_CHINESE_TRADITIONAL);
    default:
        return MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US);
    }
}

inline size_t message_tail_capacity(const miniio::io_msg &msg, size_t api_size, ULONG declared_size) noexcept
{
    const auto prefix = sizeof(CONSOLE_MSG_HEADER) + api_size;
    if (declared_size == 0)
        return sizeof(msg.body) - prefix;
    if (declared_size > prefix)
        return std::min<size_t>(declared_size - prefix, sizeof(msg.body) - prefix);
    return 0;
}

inline size_t message_input_tail_capacity(const miniio::io_msg &msg, size_t api_size) noexcept
{
    return message_tail_capacity(msg, api_size, msg.descriptor.InputSize);
}

inline size_t message_output_tail_capacity(const miniio::io_msg &msg, size_t api_size) noexcept
{
    const auto local_prefix = sizeof(CONSOLE_MSG_HEADER) + api_size;
    const auto local_capacity = sizeof(msg.body) - local_prefix;
    if (msg.descriptor.OutputSize == 0)
        return local_capacity;
    if (msg.descriptor.OutputSize > api_size)
        return std::min<size_t>(msg.descriptor.OutputSize - api_size, local_capacity);
    return 0;
}

inline void ucomplete_status_sz(miniio::io_msg &msg, LONG status, ULONG sz)
{
    auto &c = miniio::prepare_completion(msg, status, sz);
    c.Write.Data = msg.body + sizeof(CONSOLE_MSG_HEADER);
    c.Write.Size = sz;
}

// ── 前向声明 ──
struct pipe_bridge;

// Win32 控制台属性低 4 位使用 BGRI 顺序：
//   bit0=BLUE, bit1=GREEN, bit2=RED, bit3=INTENSITY
// ANSI SGR 30-37/90-97 使用 RGB 顺序：
//   0=black, 1=red, 2=green, 3=yellow, 4=blue, ...
// 不能直接把 attr&0x0F 透传给 SGR，否则红/蓝会互换。
inline short win32_attr_color_to_sgr_index(WORD color) noexcept
{
    // 输入只使用低 4 位 Win32 BGRI 颜色；返回值是 SGR 0..15 颜色索引。
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
    // 调用者传入的是完整 Win32 属性 WORD。本函数只填充 vt_message 中和 SGR
    // 有关的字段，其他字段保持调用前状态。
    m.payload.sgr.fg.set_index(win32_attr_color_to_sgr_index(attr & 0x0F));
    m.payload.sgr.bg.set_index(win32_attr_color_to_sgr_index((attr >> 4) & 0x0F));

    auto fl = (attr >> 8) & 0xFF;
    if (fl & COMMON_LVB_UNDERSCORE)
        m.payload.sgr.set(vt_sgr_flag::underline);
    if (fl & COMMON_LVB_REVERSE_VIDEO)
        m.payload.sgr.set(vt_sgr_flag::negative);
}

inline bool is_line_terminator_echo(std::u32string_view text) noexcept
{
    // PowerShell 在 Enter 后可能补写纯换行 echo；这类文本不应再次显示。
    return text == U"\r"sv || text == U"\n"sv || text == U"\r\n"sv;
}

inline bool is_printable_ascii_text(std::u32string_view text) noexcept
{
    for (char32_t ch : text)
        if (ch < U' ' || ch >= U'\x7f')
            return false;
    return true;
}

inline bool viewport_covers_screen_buffer(const screen_buffer &sb) noexcept
{
    return sb.viewport.covers(sb.size);
}

inline void clear_viewport_rect(screen_buffer &sb, WORD attr, SMALL_RECT rect) noexcept
{
    const auto view = sb.viewport.rect();
    rect.Left = std::max(rect.Left, view.Left);
    rect.Top = std::max(rect.Top, view.Top);
    rect.Right = std::min(rect.Right, view.Right);
    rect.Bottom = std::min(rect.Bottom, view.Bottom);
    if (rect.Left > rect.Right || rect.Top > rect.Bottom)
        return;

    for (SHORT y = rect.Top; y <= rect.Bottom; ++y)
        for (SHORT x = rect.Left; x <= rect.Right; ++x)
            sb.clear_cell({x, y}, attr);
}

inline void apply_terminal_erase_in_display(const vt_message &msg, console_state &state, screen_buffer &sb) noexcept
{
    const auto view = sb.viewport.rect();
    const auto cursor_x = std::clamp<SHORT>(state.cursor.position.X, view.Left, view.Right);
    const auto cursor_y = std::clamp<SHORT>(state.cursor.position.Y, view.Top, view.Bottom);
    switch (msg.payload.erase_mode)
    {
    case 0:
        clear_viewport_rect(sb, state.default_attributes, {cursor_x, cursor_y, view.Right, cursor_y});
        clear_viewport_rect(sb, state.default_attributes,
                            {view.Left, static_cast<SHORT>(cursor_y + 1), view.Right, view.Bottom});
        break;
    case 1:
        clear_viewport_rect(sb, state.default_attributes,
                            {view.Left, view.Top, view.Right, static_cast<SHORT>(cursor_y - 1)});
        clear_viewport_rect(sb, state.default_attributes, {view.Left, cursor_y, cursor_x, cursor_y});
        break;
    case 2:
    case 3:
        clear_viewport_rect(sb, state.default_attributes, view);
        break;
    default:
        break;
    }
}

inline void apply_terminal_erase_in_line(const vt_message &msg, console_state &state, screen_buffer &sb) noexcept
{
    const auto view = sb.viewport.rect();
    if (state.cursor.position.Y < view.Top || state.cursor.position.Y > view.Bottom)
        return;

    const auto cursor_x = std::clamp<SHORT>(state.cursor.position.X, view.Left, view.Right);
    const auto y = state.cursor.position.Y;
    switch (msg.payload.erase_mode)
    {
    case 0:
        clear_viewport_rect(sb, state.default_attributes, {cursor_x, y, view.Right, y});
        break;
    case 1:
        clear_viewport_rect(sb, state.default_attributes, {view.Left, y, cursor_x, y});
        break;
    case 2:
        clear_viewport_rect(sb, state.default_attributes, {view.Left, y, view.Right, y});
        break;
    default:
        break;
    }
}

inline SMALL_RECT terminal_scroll_region(const console_state &state, SMALL_RECT view) noexcept
{
    if (state.scroll_region_top == 1 && state.scroll_region_bottom <= 0)
        return view;

    const auto height = static_cast<SHORT>(view.Bottom - view.Top + 1);
    const auto top = std::clamp<SHORT>(state.scroll_region_top, 1, height);
    const auto bottom =
        state.scroll_region_bottom <= 0 ? height : std::clamp<SHORT>(state.scroll_region_bottom, 1, height);
    if (top >= bottom)
        return view;
    return {view.Left, static_cast<SHORT>(view.Top + top - 1), view.Right, static_cast<SHORT>(view.Top + bottom - 1)};
}

inline SMALL_RECT terminal_scroll_region(const console_state &state, screen_buffer &sb) noexcept
{
    return terminal_scroll_region(state, sb.viewport.rect());
}

inline void apply_terminal_line_feed(console_state &state, screen_buffer &sb)
{
    COREHOST_PERF_SCOPE(apply_line_feed);
    const auto view = sb.viewport.rect();
    const auto scroll_region = terminal_scroll_region(state, view);
    if (state.cursor.position.Y == scroll_region.Bottom && state.cursor.position.Y >= scroll_region.Top)
    {
        sb.scroll(scroll_region, scroll_region, true, {scroll_region.Left, static_cast<SHORT>(scroll_region.Top - 1)},
                  U' ', state.default_attributes);
        state.cursor.position.Y = scroll_region.Bottom;
    }
    else
    {
        state.cursor.position.Y = std::min<SHORT>(view.Bottom, static_cast<SHORT>(state.cursor.position.Y + 1));
    }
    state.cursor.position.X = view.Left;
}

inline void apply_terminal_text(std::u32string_view text, console_state &state, screen_buffer &sb)
{
    const auto view = sb.viewport.rect();
    state.cursor.position.X = std::clamp<SHORT>(state.cursor.position.X, view.Left, view.Right);
    state.cursor.position.Y = std::clamp<SHORT>(state.cursor.position.Y, view.Top, view.Bottom);

    auto remaining = text;
    while (!remaining.empty())
    {
        const auto result = sb.write_text_row(state.cursor.position, remaining, state.default_attributes,
                                              state.text_measurement, state.ambiguous_is_wide);
        remaining.remove_prefix(result.consumed);
        if (result.row_end)
        {
            apply_terminal_line_feed(state, sb);
            continue;
        }
        if (result.consumed == 0)
            break;
    }
}

inline void apply_terminal_text(const vt_message &msg, console_state &state, screen_buffer &sb)
{
    apply_terminal_text(msg.payload.text, state, sb);
}

inline void consume_write_console_text_run(std::u32string_view text, console_state &state, screen_buffer &sb,
                                           pipe_bridge &bridge, bool emit_vt = true)
{
    if (emit_vt)
        bridge.vt_write_text(text);
    apply_terminal_text(text, state, sb);
}

inline void consume_write_console_line_feed(console_state &state, screen_buffer &sb, pipe_bridge &bridge,
                                            bool emit_vt = true)
{
    if (emit_vt)
        bridge.vt_write_crlf();
    apply_terminal_line_feed(state, sb);
}

inline void clear_terminal_line_range(screen_buffer &sb, SHORT y, SHORT left, SHORT right, WORD attr)
{
    for (SHORT x = left; x <= right; ++x)
        sb.clear_cell({x, y}, attr);
}

inline void apply_terminal_insert_characters(const vt_message &msg, console_state &state, screen_buffer &sb)
{
    const auto view = sb.viewport.rect();
    if (state.cursor.position.Y < view.Top || state.cursor.position.Y > view.Bottom)
        return;

    const auto y = state.cursor.position.Y;
    const auto x = std::clamp<SHORT>(state.cursor.position.X, view.Left, view.Right);
    const auto available = static_cast<SHORT>(view.Right - x + 1);
    const auto count = std::min<SHORT>(available, static_cast<SHORT>(std::max<int>(1, msg.payload.count.value)));
    const auto saved = sb.row(y);
    const auto copy_count = static_cast<SHORT>(available - count);
    if (copy_count > 0)
        sb.row(y).copy_from(saved, static_cast<uint16_t>(x), static_cast<uint16_t>(x + count),
                            static_cast<uint16_t>(copy_count));
    clear_terminal_line_range(sb, y, x, static_cast<SHORT>(x + count - 1), state.default_attributes);
}

inline void apply_terminal_delete_characters(const vt_message &msg, console_state &state, screen_buffer &sb)
{
    const auto view = sb.viewport.rect();
    if (state.cursor.position.Y < view.Top || state.cursor.position.Y > view.Bottom)
        return;

    const auto y = state.cursor.position.Y;
    const auto x = std::clamp<SHORT>(state.cursor.position.X, view.Left, view.Right);
    const auto available = static_cast<SHORT>(view.Right - x + 1);
    const auto count = std::min<SHORT>(available, static_cast<SHORT>(std::max<int>(1, msg.payload.count.value)));
    const auto saved = sb.row(y);
    const auto copy_count = static_cast<SHORT>(available - count);
    if (copy_count > 0)
        sb.row(y).copy_from(saved, static_cast<uint16_t>(x + count), static_cast<uint16_t>(x),
                            static_cast<uint16_t>(copy_count));
    clear_terminal_line_range(sb, y, static_cast<SHORT>(view.Right - count + 1), view.Right, state.default_attributes);
}

inline void apply_terminal_erase_characters(const vt_message &msg, console_state &state, screen_buffer &sb)
{
    const auto view = sb.viewport.rect();
    if (state.cursor.position.Y < view.Top || state.cursor.position.Y > view.Bottom)
        return;

    const auto y = state.cursor.position.Y;
    const auto x = std::clamp<SHORT>(state.cursor.position.X, view.Left, view.Right);
    const auto count =
        std::min<SHORT>(static_cast<SHORT>(view.Right - x + 1), static_cast<SHORT>(std::max<int>(1, msg.payload.count.value)));
    clear_terminal_line_range(sb, y, x, static_cast<SHORT>(x + count - 1), state.default_attributes);
}

inline void apply_terminal_reverse_index(console_state &state, screen_buffer &sb)
{
    const auto view = sb.viewport.rect();
    const auto scroll_region = terminal_scroll_region(state, sb);
    state.cursor.position.X = std::clamp<SHORT>(state.cursor.position.X, view.Left, view.Right);
    state.cursor.position.Y = std::clamp<SHORT>(state.cursor.position.Y, view.Top, view.Bottom);

    if (state.cursor.position.Y == scroll_region.Top)
    {
        sb.scroll(scroll_region, scroll_region, true, {scroll_region.Left, static_cast<SHORT>(scroll_region.Top + 1)},
                  U' ', state.default_attributes);
        return;
    }
    --state.cursor.position.Y;
}

inline SHORT terminal_relative_column(console_state &state, screen_buffer &sb) noexcept
{
    const auto view = sb.viewport.rect();
    return std::clamp<SHORT>(static_cast<SHORT>(state.cursor.position.X - view.Left), 0,
                             static_cast<SHORT>(view.Right - view.Left));
}

inline void apply_terminal_forward_tab(const vt_message &msg, console_state &state, screen_buffer &sb) noexcept
{
    const auto view = sb.viewport.rect();
    auto relative_x = terminal_relative_column(state, sb);
    const auto count = std::max<int>(1, msg.payload.count.value);
    for (int i = 0; i < count; ++i)
        relative_x = state.next_tab_stop(relative_x);
    state.cursor.position.X = std::clamp<SHORT>(static_cast<SHORT>(view.Left + relative_x), view.Left, view.Right);
}

inline void apply_terminal_backward_tab(const vt_message &msg, console_state &state, screen_buffer &sb) noexcept
{
    const auto view = sb.viewport.rect();
    auto relative_x = terminal_relative_column(state, sb);
    const auto count = std::max<int>(1, msg.payload.count.value);
    for (int i = 0; i < count; ++i)
        relative_x = state.prev_tab_stop(relative_x);
    state.cursor.position.X = std::clamp<SHORT>(static_cast<SHORT>(view.Left + relative_x), view.Left, view.Right);
}

inline void apply_terminal_scrolling_region(const vt_message &msg, console_state &state, screen_buffer &sb) noexcept
{
    const auto view = sb.viewport.rect();
    const auto height = static_cast<SHORT>(view.Bottom - view.Top + 1);
    const auto top = std::clamp<SHORT>(msg.payload.scroll_region.top, 1, height);
    const auto bottom = msg.payload.scroll_region.bottom <= 0 ? height : std::clamp<SHORT>(msg.payload.scroll_region.bottom, 1, height);
    if (top < bottom)
    {
        state.scroll_region_top = top;
        state.scroll_region_bottom = bottom == height ? 0 : bottom;
    }
    else
    {
        state.scroll_region_top = 1;
        state.scroll_region_bottom = 0;
    }

    state.cursor.position = {view.Left, view.Top};
}

inline void vt_msg_apply_terminal_state(vt_message_id id, const vt_message &msg, console_state &state,
                                        screen_buffer &sb)
{
    const auto view = sb.viewport.rect();
    const auto origin = sb.viewport.origin();
    const auto count = static_cast<SHORT>(std::max<int>(1, msg.payload.count.value));
    // case 顺序与 vt_message_id 枚举声明顺序保持一致。
    switch (id)
    {
    case vt_message_id::text:
        apply_terminal_text(msg, state, sb);
        break;
    case vt_message_id::carriage_return:
        state.cursor.position.X = view.Left;
        break;
    case vt_message_id::line_feed:
        apply_terminal_line_feed(state, sb);
        break;
    case vt_message_id::reverse_index:
        apply_terminal_reverse_index(state, sb);
        break;
    case vt_message_id::horizontal_tab_set:
        state.set_tab_stop(terminal_relative_column(state, sb));
        break;
    case vt_message_id::tab_clear_current:
        state.clear_tab_stop(terminal_relative_column(state, sb));
        break;
    case vt_message_id::tab_clear_all:
        state.clear_all_tab_stops();
        break;
    case vt_message_id::cursor_up:
        state.cursor.position.Y = std::max<SHORT>(view.Top, static_cast<SHORT>(state.cursor.position.Y - count));
        break;
    case vt_message_id::cursor_down:
        state.cursor.position.Y = std::min<SHORT>(view.Bottom, static_cast<SHORT>(state.cursor.position.Y + count));
        break;
    case vt_message_id::cursor_forward:
        state.cursor.position.X = std::min<SHORT>(view.Right, static_cast<SHORT>(state.cursor.position.X + count));
        break;
    case vt_message_id::cursor_backward:
        state.cursor.position.X = std::max<SHORT>(view.Left, static_cast<SHORT>(state.cursor.position.X - count));
        break;
    case vt_message_id::cursor_next_line:
        state.cursor.position.X = view.Left;
        state.cursor.position.Y = std::min<SHORT>(view.Bottom, static_cast<SHORT>(state.cursor.position.Y + count));
        break;
    case vt_message_id::cursor_prev_line:
        state.cursor.position.X = view.Left;
        state.cursor.position.Y = std::max<SHORT>(view.Top, static_cast<SHORT>(state.cursor.position.Y - count));
        break;
    case vt_message_id::scroll_up: {
        const auto region = terminal_scroll_region(state, sb);
        sb.scroll(region, region, true, {region.Left, static_cast<SHORT>(region.Top - count)}, U' ',
                  state.default_attributes);
        break;
    }
    case vt_message_id::scroll_down: {
        const auto region = terminal_scroll_region(state, sb);
        sb.scroll(region, region, true, {region.Left, static_cast<SHORT>(region.Top + count)}, U' ',
                  state.default_attributes);
        break;
    }
    case vt_message_id::insert_characters:
        apply_terminal_insert_characters(msg, state, sb);
        break;
    case vt_message_id::delete_characters:
        apply_terminal_delete_characters(msg, state, sb);
        break;
    case vt_message_id::erase_characters:
        apply_terminal_erase_characters(msg, state, sb);
        break;
    case vt_message_id::insert_lines: {
        const auto region = terminal_scroll_region(state, sb);
        if (state.cursor.position.Y >= region.Top && state.cursor.position.Y <= region.Bottom)
        {
            const SMALL_RECT lines{region.Left, state.cursor.position.Y, region.Right, region.Bottom};
            sb.scroll(lines, lines, true, {region.Left, static_cast<SHORT>(state.cursor.position.Y + count)}, U' ',
                      state.default_attributes);
        }
        break;
    }
    case vt_message_id::delete_lines: {
        const auto region = terminal_scroll_region(state, sb);
        if (state.cursor.position.Y >= region.Top && state.cursor.position.Y <= region.Bottom)
        {
            const SMALL_RECT lines{region.Left, state.cursor.position.Y, region.Right, region.Bottom};
            sb.scroll(lines, lines, true, {region.Left, static_cast<SHORT>(state.cursor.position.Y - count)}, U' ',
                      state.default_attributes);
        }
        break;
    }
    case vt_message_id::cursor_forward_tab:
        apply_terminal_forward_tab(msg, state, sb);
        break;
    case vt_message_id::cursor_backward_tab:
        apply_terminal_backward_tab(msg, state, sb);
        break;
    case vt_message_id::cursor_vert_absolute: {
        auto adjusted = msg;
        adjusted.payload.position.row = static_cast<short>(msg.payload.position.row + origin.Y);
        vt_msg_apply_state(id, adjusted, state, sb);
        break;
    }
    case vt_message_id::cursor_horiz_absolute: {
        auto adjusted = msg;
        adjusted.payload.position.col = static_cast<short>(msg.payload.position.col + origin.X);
        vt_msg_apply_state(id, adjusted, state, sb);
        break;
    }
    case vt_message_id::cursor_position: {
        auto adjusted = msg;
        adjusted.payload.position.row = static_cast<short>(msg.payload.position.row + origin.Y);
        adjusted.payload.position.col = static_cast<short>(msg.payload.position.col + origin.X);
        vt_msg_apply_state(id, adjusted, state, sb);
        break;
    }
    case vt_message_id::erase_in_display:
        apply_terminal_erase_in_display(msg, state, sb);
        break;
    case vt_message_id::erase_in_line:
        apply_terminal_erase_in_line(msg, state, sb);
        break;
    case vt_message_id::set_scrolling_region:
        apply_terminal_scrolling_region(msg, state, sb);
        break;
    default:
        vt_msg_apply_state(id, msg, state, sb);
        break;
    }
}

inline bool can_passthrough_write_console_vt(vt_message_id id) noexcept
{
    switch (id)
    {
    case vt_message_id::cursor_up:
    case vt_message_id::cursor_down:
    case vt_message_id::cursor_forward:
    case vt_message_id::cursor_backward:
    case vt_message_id::cursor_next_line:
    case vt_message_id::cursor_prev_line:
    case vt_message_id::cursor_horiz_absolute:
    case vt_message_id::cursor_vert_absolute:
    case vt_message_id::cursor_position:
    case vt_message_id::ansi_save_cursor:
    case vt_message_id::ansi_restore_cursor:
    case vt_message_id::cursor_enable_blinking:
    case vt_message_id::cursor_disable_blinking:
    case vt_message_id::cursor_show:
    case vt_message_id::cursor_hide:
    case vt_message_id::scroll_up:
    case vt_message_id::scroll_down:
    case vt_message_id::insert_characters:
    case vt_message_id::delete_characters:
    case vt_message_id::erase_characters:
    case vt_message_id::insert_lines:
    case vt_message_id::delete_lines:
    case vt_message_id::erase_in_display:
    case vt_message_id::erase_in_line:
    case vt_message_id::sgr:
    case vt_message_id::set_palette_color:
    case vt_message_id::cursor_keys_app_mode:
    case vt_message_id::cursor_keys_normal_mode:
    case vt_message_id::report_cursor_position:
    case vt_message_id::device_attributes:
    case vt_message_id::cursor_forward_tab:
    case vt_message_id::cursor_backward_tab:
    case vt_message_id::tab_clear_current:
    case vt_message_id::tab_clear_all:
    case vt_message_id::set_scrolling_region:
    case vt_message_id::set_window_title:
    case vt_message_id::use_alternate_buffer:
    case vt_message_id::use_main_buffer:
    case vt_message_id::set_columns_132:
    case vt_message_id::set_columns_80:
    case vt_message_id::reverse_index:
    case vt_message_id::save_cursor:
    case vt_message_id::restore_cursor:
    case vt_message_id::horizontal_tab_set:
    case vt_message_id::keypad_app_mode:
    case vt_message_id::keypad_numeric_mode:
    case vt_message_id::designate_charset_line_drawing:
    case vt_message_id::designate_charset_ascii:
        return true;
    default:
        return false;
    }
}

template <vt_message_id id>
inline void consume_write_console_vt_message(vt_parser &parser, console_state &state, screen_buffer &sb,
                                             pipe_bridge &bridge, bool emit_vt = true)
{
    auto &msg = parser.get();
    if constexpr (id == vt_message_id::unknown_sequence)
    {
        parser.reset<vt_message_id::unknown_sequence>();
        return;
    }

    const auto raw = parser.raw_sequence();
    if (emit_vt)
    {
        if (!raw.empty() && can_passthrough_write_console_vt(id))
            bridge.vt_append_raw_sequence(raw);
        else
            bridge.vt_msg_send<id>(msg);
    }

    if constexpr (id != vt_message_id::sgr)
        vt_msg_apply_terminal_state(id, msg, state, sb);
    parser.reset<id>();
}

inline void dispatch_write_console_vt_message(vt_message_id id, vt_parser &parser, console_state &state,
                                              screen_buffer &sb, pipe_bridge &bridge, bool emit_vt = true)
{
    // case 顺序与 vt_message_id 枚举声明顺序保持一致。
    switch (id)
    {
    case vt_message_id::continue_text:
        consume_write_console_vt_message<vt_message_id::continue_text>(parser, state, sb, bridge, emit_vt);
        break;
    case vt_message_id::continue_:
        consume_write_console_vt_message<vt_message_id::continue_>(parser, state, sb, bridge, emit_vt);
        break;
    case vt_message_id::text:
        consume_write_console_vt_message<vt_message_id::text>(parser, state, sb, bridge, emit_vt);
        break;
    case vt_message_id::unknown_sequence:
        consume_write_console_vt_message<vt_message_id::unknown_sequence>(parser, state, sb, bridge, emit_vt);
        break;
    case vt_message_id::carriage_return:
        consume_write_console_vt_message<vt_message_id::carriage_return>(parser, state, sb, bridge, emit_vt);
        break;
    case vt_message_id::line_feed:
        consume_write_console_vt_message<vt_message_id::line_feed>(parser, state, sb, bridge, emit_vt);
        break;
    case vt_message_id::reverse_index:
        consume_write_console_vt_message<vt_message_id::reverse_index>(parser, state, sb, bridge, emit_vt);
        break;
    case vt_message_id::save_cursor:
        consume_write_console_vt_message<vt_message_id::save_cursor>(parser, state, sb, bridge, emit_vt);
        break;
    case vt_message_id::restore_cursor:
        consume_write_console_vt_message<vt_message_id::restore_cursor>(parser, state, sb, bridge, emit_vt);
        break;
    case vt_message_id::horizontal_tab_set:
        consume_write_console_vt_message<vt_message_id::horizontal_tab_set>(parser, state, sb, bridge, emit_vt);
        break;
    case vt_message_id::keypad_app_mode:
        consume_write_console_vt_message<vt_message_id::keypad_app_mode>(parser, state, sb, bridge, emit_vt);
        break;
    case vt_message_id::keypad_numeric_mode:
        consume_write_console_vt_message<vt_message_id::keypad_numeric_mode>(parser, state, sb, bridge, emit_vt);
        break;
    case vt_message_id::designate_charset_line_drawing:
        consume_write_console_vt_message<vt_message_id::designate_charset_line_drawing>(parser, state, sb, bridge, emit_vt);
        break;
    case vt_message_id::designate_charset_ascii:
        consume_write_console_vt_message<vt_message_id::designate_charset_ascii>(parser, state, sb, bridge, emit_vt);
        break;
    case vt_message_id::ansi_save_cursor:
        consume_write_console_vt_message<vt_message_id::ansi_save_cursor>(parser, state, sb, bridge, emit_vt);
        break;
    case vt_message_id::ansi_restore_cursor:
        consume_write_console_vt_message<vt_message_id::ansi_restore_cursor>(parser, state, sb, bridge, emit_vt);
        break;
    case vt_message_id::cursor_enable_blinking:
        consume_write_console_vt_message<vt_message_id::cursor_enable_blinking>(parser, state, sb, bridge, emit_vt);
        break;
    case vt_message_id::cursor_disable_blinking:
        consume_write_console_vt_message<vt_message_id::cursor_disable_blinking>(parser, state, sb, bridge, emit_vt);
        break;
    case vt_message_id::cursor_show:
        consume_write_console_vt_message<vt_message_id::cursor_show>(parser, state, sb, bridge, emit_vt);
        break;
    case vt_message_id::cursor_hide:
        consume_write_console_vt_message<vt_message_id::cursor_hide>(parser, state, sb, bridge, emit_vt);
        break;
    case vt_message_id::cursor_keys_app_mode:
        consume_write_console_vt_message<vt_message_id::cursor_keys_app_mode>(parser, state, sb, bridge, emit_vt);
        break;
    case vt_message_id::cursor_keys_normal_mode:
        consume_write_console_vt_message<vt_message_id::cursor_keys_normal_mode>(parser, state, sb, bridge, emit_vt);
        break;
    case vt_message_id::report_cursor_position:
        consume_write_console_vt_message<vt_message_id::report_cursor_position>(parser, state, sb, bridge, emit_vt);
        break;
    case vt_message_id::device_attributes:
        consume_write_console_vt_message<vt_message_id::device_attributes>(parser, state, sb, bridge, emit_vt);
        break;
    case vt_message_id::tab_clear_current:
        consume_write_console_vt_message<vt_message_id::tab_clear_current>(parser, state, sb, bridge, emit_vt);
        break;
    case vt_message_id::tab_clear_all:
        consume_write_console_vt_message<vt_message_id::tab_clear_all>(parser, state, sb, bridge, emit_vt);
        break;
    case vt_message_id::set_window_title:
        consume_write_console_vt_message<vt_message_id::set_window_title>(parser, state, sb, bridge, emit_vt);
        break;
    case vt_message_id::use_alternate_buffer:
        consume_write_console_vt_message<vt_message_id::use_alternate_buffer>(parser, state, sb, bridge, emit_vt);
        break;
    case vt_message_id::use_main_buffer:
        consume_write_console_vt_message<vt_message_id::use_main_buffer>(parser, state, sb, bridge, emit_vt);
        break;
    case vt_message_id::soft_reset:
        consume_write_console_vt_message<vt_message_id::soft_reset>(parser, state, sb, bridge, emit_vt);
        break;
    case vt_message_id::key_up:
        consume_write_console_vt_message<vt_message_id::key_up>(parser, state, sb, bridge, emit_vt);
        break;
    case vt_message_id::key_down:
        consume_write_console_vt_message<vt_message_id::key_down>(parser, state, sb, bridge, emit_vt);
        break;
    case vt_message_id::key_right:
        consume_write_console_vt_message<vt_message_id::key_right>(parser, state, sb, bridge, emit_vt);
        break;
    case vt_message_id::key_left:
        consume_write_console_vt_message<vt_message_id::key_left>(parser, state, sb, bridge, emit_vt);
        break;
    case vt_message_id::key_home:
        consume_write_console_vt_message<vt_message_id::key_home>(parser, state, sb, bridge, emit_vt);
        break;
    case vt_message_id::key_end:
        consume_write_console_vt_message<vt_message_id::key_end>(parser, state, sb, bridge, emit_vt);
        break;
    case vt_message_id::key_insert:
        consume_write_console_vt_message<vt_message_id::key_insert>(parser, state, sb, bridge, emit_vt);
        break;
    case vt_message_id::key_delete:
        consume_write_console_vt_message<vt_message_id::key_delete>(parser, state, sb, bridge, emit_vt);
        break;
    case vt_message_id::key_page_up:
        consume_write_console_vt_message<vt_message_id::key_page_up>(parser, state, sb, bridge, emit_vt);
        break;
    case vt_message_id::key_page_down:
        consume_write_console_vt_message<vt_message_id::key_page_down>(parser, state, sb, bridge, emit_vt);
        break;
    case vt_message_id::key_f1:
        consume_write_console_vt_message<vt_message_id::key_f1>(parser, state, sb, bridge, emit_vt);
        break;
    case vt_message_id::key_f2:
        consume_write_console_vt_message<vt_message_id::key_f2>(parser, state, sb, bridge, emit_vt);
        break;
    case vt_message_id::key_f3:
        consume_write_console_vt_message<vt_message_id::key_f3>(parser, state, sb, bridge, emit_vt);
        break;
    case vt_message_id::key_f4:
        consume_write_console_vt_message<vt_message_id::key_f4>(parser, state, sb, bridge, emit_vt);
        break;
    case vt_message_id::key_f5:
        consume_write_console_vt_message<vt_message_id::key_f5>(parser, state, sb, bridge, emit_vt);
        break;
    case vt_message_id::key_f6:
        consume_write_console_vt_message<vt_message_id::key_f6>(parser, state, sb, bridge, emit_vt);
        break;
    case vt_message_id::key_f7:
        consume_write_console_vt_message<vt_message_id::key_f7>(parser, state, sb, bridge, emit_vt);
        break;
    case vt_message_id::key_f8:
        consume_write_console_vt_message<vt_message_id::key_f8>(parser, state, sb, bridge, emit_vt);
        break;
    case vt_message_id::key_f9:
        consume_write_console_vt_message<vt_message_id::key_f9>(parser, state, sb, bridge, emit_vt);
        break;
    case vt_message_id::key_f10:
        consume_write_console_vt_message<vt_message_id::key_f10>(parser, state, sb, bridge, emit_vt);
        break;
    case vt_message_id::key_f11:
        consume_write_console_vt_message<vt_message_id::key_f11>(parser, state, sb, bridge, emit_vt);
        break;
    case vt_message_id::key_f12:
        consume_write_console_vt_message<vt_message_id::key_f12>(parser, state, sb, bridge, emit_vt);
        break;
    case vt_message_id::key_ctrl_up:
        consume_write_console_vt_message<vt_message_id::key_ctrl_up>(parser, state, sb, bridge, emit_vt);
        break;
    case vt_message_id::key_ctrl_down:
        consume_write_console_vt_message<vt_message_id::key_ctrl_down>(parser, state, sb, bridge, emit_vt);
        break;
    case vt_message_id::key_ctrl_right:
        consume_write_console_vt_message<vt_message_id::key_ctrl_right>(parser, state, sb, bridge, emit_vt);
        break;
    case vt_message_id::key_ctrl_left:
        consume_write_console_vt_message<vt_message_id::key_ctrl_left>(parser, state, sb, bridge, emit_vt);
        break;
    case vt_message_id::char_del:
        consume_write_console_vt_message<vt_message_id::char_del>(parser, state, sb, bridge, emit_vt);
        break;
    case vt_message_id::char_sub:
        consume_write_console_vt_message<vt_message_id::char_sub>(parser, state, sb, bridge, emit_vt);
        break;
    case vt_message_id::char_esc:
        consume_write_console_vt_message<vt_message_id::char_esc>(parser, state, sb, bridge, emit_vt);
        break;
    case vt_message_id::char_nul:
        consume_write_console_vt_message<vt_message_id::char_nul>(parser, state, sb, bridge, emit_vt);
        break;
    case vt_message_id::cursor_up:
        consume_write_console_vt_message<vt_message_id::cursor_up>(parser, state, sb, bridge, emit_vt);
        break;
    case vt_message_id::cursor_down:
        consume_write_console_vt_message<vt_message_id::cursor_down>(parser, state, sb, bridge, emit_vt);
        break;
    case vt_message_id::cursor_forward:
        consume_write_console_vt_message<vt_message_id::cursor_forward>(parser, state, sb, bridge, emit_vt);
        break;
    case vt_message_id::cursor_backward:
        consume_write_console_vt_message<vt_message_id::cursor_backward>(parser, state, sb, bridge, emit_vt);
        break;
    case vt_message_id::cursor_next_line:
        consume_write_console_vt_message<vt_message_id::cursor_next_line>(parser, state, sb, bridge, emit_vt);
        break;
    case vt_message_id::cursor_prev_line:
        consume_write_console_vt_message<vt_message_id::cursor_prev_line>(parser, state, sb, bridge, emit_vt);
        break;
    case vt_message_id::scroll_up:
        consume_write_console_vt_message<vt_message_id::scroll_up>(parser, state, sb, bridge, emit_vt);
        break;
    case vt_message_id::scroll_down:
        consume_write_console_vt_message<vt_message_id::scroll_down>(parser, state, sb, bridge, emit_vt);
        break;
    case vt_message_id::insert_characters:
        consume_write_console_vt_message<vt_message_id::insert_characters>(parser, state, sb, bridge, emit_vt);
        break;
    case vt_message_id::delete_characters:
        consume_write_console_vt_message<vt_message_id::delete_characters>(parser, state, sb, bridge, emit_vt);
        break;
    case vt_message_id::erase_characters:
        consume_write_console_vt_message<vt_message_id::erase_characters>(parser, state, sb, bridge, emit_vt);
        break;
    case vt_message_id::insert_lines:
        consume_write_console_vt_message<vt_message_id::insert_lines>(parser, state, sb, bridge, emit_vt);
        break;
    case vt_message_id::delete_lines:
        consume_write_console_vt_message<vt_message_id::delete_lines>(parser, state, sb, bridge, emit_vt);
        break;
    case vt_message_id::cursor_forward_tab:
        consume_write_console_vt_message<vt_message_id::cursor_forward_tab>(parser, state, sb, bridge, emit_vt);
        break;
    case vt_message_id::cursor_backward_tab:
        consume_write_console_vt_message<vt_message_id::cursor_backward_tab>(parser, state, sb, bridge, emit_vt);
        break;
    case vt_message_id::cursor_vert_absolute:
        consume_write_console_vt_message<vt_message_id::cursor_vert_absolute>(parser, state, sb, bridge, emit_vt);
        break;
    case vt_message_id::cursor_horiz_absolute:
        consume_write_console_vt_message<vt_message_id::cursor_horiz_absolute>(parser, state, sb, bridge, emit_vt);
        break;
    case vt_message_id::cursor_position:
        consume_write_console_vt_message<vt_message_id::cursor_position>(parser, state, sb, bridge, emit_vt);
        break;
    case vt_message_id::set_cursor_shape:
        consume_write_console_vt_message<vt_message_id::set_cursor_shape>(parser, state, sb, bridge, emit_vt);
        break;
    case vt_message_id::erase_in_display:
        consume_write_console_vt_message<vt_message_id::erase_in_display>(parser, state, sb, bridge, emit_vt);
        break;
    case vt_message_id::erase_in_line:
        consume_write_console_vt_message<vt_message_id::erase_in_line>(parser, state, sb, bridge, emit_vt);
        break;
    case vt_message_id::set_palette_color:
        consume_write_console_vt_message<vt_message_id::set_palette_color>(parser, state, sb, bridge, emit_vt);
        break;
    case vt_message_id::set_scrolling_region:
        consume_write_console_vt_message<vt_message_id::set_scrolling_region>(parser, state, sb, bridge, emit_vt);
        break;
    case vt_message_id::set_columns_132:
        consume_write_console_vt_message<vt_message_id::set_columns_132>(parser, state, sb, bridge, emit_vt);
        break;
    case vt_message_id::set_columns_80:
        consume_write_console_vt_message<vt_message_id::set_columns_80>(parser, state, sb, bridge, emit_vt);
        break;
    case vt_message_id::resize_window:
        consume_write_console_vt_message<vt_message_id::resize_window>(parser, state, sb, bridge, emit_vt);
        break;
    case vt_message_id::win32_input_key:
        consume_write_console_vt_message<vt_message_id::win32_input_key>(parser, state, sb, bridge, emit_vt);
        break;
    case vt_message_id::cpr_response:
        consume_write_console_vt_message<vt_message_id::cpr_response>(parser, state, sb, bridge, emit_vt);
        break;
    case vt_message_id::sgr:
        consume_write_console_vt_message<vt_message_id::sgr>(parser, state, sb, bridge, emit_vt);
        break;
    }
}

[[nodiscard]] inline size_t simple_sgr_passthrough_length(std::u32string_view input) noexcept
{
    if (input.size() < 3 || input[0] != U'\x1b' || input[1] != U'[')
        return 0;

    for (size_t i = 2; i < input.size(); ++i)
    {
        const char32_t ch = input[i];
        if ((ch >= U'0' && ch <= U'9') || ch == U';')
            continue;
        if (ch == U'm')
            return i + 1;
        return 0;
    }
    return 0;
}

// ── completion 辅助 ──

inline void ucomplete(miniio::io_msg &msg)
{
    // USER_DEFINED API 的 completion payload 从 CONSOLE_MSG_HEADER 后开始；
    // 零长度表示该 API 只需要状态码，不返回结构体。
    auto &c = miniio::prepare_completion(msg, 0, 0);
    c.Write.Data = msg.body + sizeof(CONSOLE_MSG_HEADER);
    c.Write.Size = 0;
}

inline void ucomplete_sz(miniio::io_msg &msg, ULONG sz)
{
    // sz 是 header 后返回给客户端的字节数，不包含 CONSOLE_MSG_HEADER。
    auto &c = miniio::prepare_completion(msg, 0, sz);
    c.Write.Data = msg.body + sizeof(CONSOLE_MSG_HEADER);
    c.Write.Size = sz;
}

// ════════════════════════════════════════════════════════
// L1 API handlers
// ════════════════════════════════════════════════════════

inline bool api_get_cp(miniio::io_msg &msg, console_state &state, screen_buffer &, input_buffer &, pipe_bridge &)
{
    auto *r = reinterpret_cast<CONSOLE_GETCP_MSG *>(msg.body + sizeof(CONSOLE_MSG_HEADER));
    r->CodePage = r->Output ? state.output_code_page : state.input_code_page;
    ucomplete_sz(msg, sizeof(CONSOLE_GETCP_MSG));
    return true;
}

inline bool api_get_mode(miniio::io_msg &msg, console_state &state, screen_buffer &, input_buffer &, pipe_bridge &,
                         bool input_handle)
{
    auto *r = reinterpret_cast<CONSOLE_MODE_MSG *>(msg.body + sizeof(CONSOLE_MSG_HEADER));
    r->Mode = input_handle ? state.input_mode : state.output_mode;
    ucomplete_sz(msg, sizeof(CONSOLE_MODE_MSG));
    return true;
}

inline bool api_set_mode(miniio::io_msg &msg, console_state &state, screen_buffer &, input_buffer &, pipe_bridge &,
                         bool input_handle)
{
    auto *r = reinterpret_cast<CONSOLE_MODE_MSG *>(msg.body + sizeof(CONSOLE_MSG_HEADER));
    if (input_handle)
    {
        const auto requested_mode = r->Mode;
        state.input_mode = requested_mode & ~private_input_modes;

        // 原版为了兼容会先保存模式，再返回 E_INVALIDARG。PSReadLine 依赖
        // “ECHO 开启但 LINE 关闭”这种无效组合仍然影响后续 Ctrl+C 行为。
        if ((requested_mode & ~(valid_input_modes | private_input_modes)) != 0 ||
            ((requested_mode & ENABLE_ECHO_INPUT) != 0 && (requested_mode & ENABLE_LINE_INPUT) == 0))
        {
            miniio::prepare_completion(msg, status_invalid_parameter);
            return true;
        }
    }
    else
    {
        if ((r->Mode & ~valid_output_modes) != 0)
        {
            miniio::prepare_completion(msg, status_invalid_parameter);
            return true;
        }
        state.output_mode = r->Mode;
    }
    ucomplete(msg);
    return true;
}

inline bool api_get_num_input(miniio::io_msg &msg, console_state &, screen_buffer &, input_buffer &inp,
                              pipe_bridge &bridge)
{
    bridge.prepare_console_input_events();
    auto *r = reinterpret_cast<CONSOLE_GETNUMBEROFINPUTEVENTS_MSG *>(msg.body + sizeof(CONSOLE_MSG_HEADER));
    r->ReadyEvents = static_cast<DWORD>(inp.available());
    ucomplete_sz(msg, sizeof(CONSOLE_GETNUMBEROFINPUTEVENTS_MSG));
    return true;
}

inline bool api_get_console_input(miniio::io_msg &msg, console_state &, screen_buffer &, input_buffer &,
                                  pipe_bridge &bridge)
{
    // GetConsoleInput 可能同步完成，也可能由 bridge 挂起到输入事件到达。
    return bridge.handle_console_input(msg);
}

inline bool api_get_langid(miniio::io_msg &msg, console_state &state, screen_buffer &, input_buffer &, pipe_bridge &)
{
    auto *r = reinterpret_cast<CONSOLE_LANGID_MSG *>(msg.body + sizeof(CONSOLE_MSG_HEADER));
    // 原版只在 Windows ACP 是东亚代码页时成功返回 LANGID；否则返回
    // STATUS_NOT_SUPPORTED，让客户端 loader 跳过 SetThreadLocale。
    if (!is_east_asian_code_page(::GetACP()))
    {
        miniio::prepare_completion(msg, status_not_supported);
        return true;
    }

    r->LangId = lang_id_from_console_output_code_page(state.output_code_page);
    ucomplete_sz(msg, sizeof(CONSOLE_LANGID_MSG));
    return true;
}

inline void write_console_payload(bool unicode, const BYTE *data, ULONG bytes, console_state &state, screen_buffer &sb,
                                  pipe_bridge &bridge, bool emit_console_attributes = true)
{
    COREHOST_PERF_SCOPE_AMOUNT(write_console_payload, bytes);
    if (bytes > 0)
    {
        auto &u32s = bridge.conv_u32();
        const UINT output_code_page = state.output_code_page ? state.output_code_page : CP_ACP;
        {
            COREHOST_PERF_SCOPE_AMOUNT(write_console_convert, bytes);
            if (unicode)
            {
                // WriteConsoleW 的 NumBytes/输入长度仍按 UTF-16 字节计算；内部统一成
                // char32_t，便于和 VT parser/screen_buffer 共用文本路径。
                auto *ws = reinterpret_cast<const wchar_t *>(data);
                auto wl = bytes / sizeof(wchar_t);
                convert_utf16_to_u32(std::wstring_view{ws, wl}, u32s);
            }
            else
            {
                // 非 Unicode 路径使用当前输出代码页；0 是未初始化兜底，退回系统 ACP。
                convert_ansi_to_u32(reinterpret_cast<const char *>(data), bytes, output_code_page, u32s,
                                    bridge.conv_wstr());
            }
        }

        if (!u32s.empty())
        {
            COORD start_pos = state.cursor.position;
            auto preview_len = static_cast<int>(std::min<size_t>(60, u32s.size()));
            LOG("[api_write_console] start: u32s_len=%zu sbytes=%lu start=(%d,%d) first=%.*ls", u32s.size(),
                static_cast<unsigned long>(bytes), static_cast<int>(start_pos.X), static_cast<int>(start_pos.Y),
                preview_len, reinterpret_cast<const wchar_t *>(u32s.data()));

            if (state.dec_line_drawing_mode)
            {
                // ESC(0 只影响后续文本字节的显示字符，不改变输入字节数量或
                // WriteConsole 的 NumBytes 返回值。
                for (auto &ch : u32s)
                    if (ch >= 0x5f && ch <= 0x7e)
                        ch = state.dec_to_unicode(static_cast<unsigned char>(ch));
            }

            vt_message m{};

            {
                COREHOST_PERF_SCOPE(write_console_position);
                bool need_cup = bridge.consume_enter_newline();
                LOG("[api_write_console] need_cup=%d state_start=(%d,%d)", need_cup, state.cursor.position.X,
                    state.cursor.position.Y);
                if (need_cup)
                {
                    // 输入桥接已经本地回显 Enter。这里把应用输出起点移动到当时锁定
                    // 的新行位置，而不是使用后续 SetCursorPos 可能修改过的坐标。
                    COORD nl_pos = bridge.get_enter_dest();
                    state.cursor.position = nl_pos;
                    if (sb.viewport.snap_to_cursor(state.cursor.position, state.screen_buffer_size))
                        render_visible_viewport(state, sb, bridge);
                    bridge.vt_write_cup_buffer(nl_pos);
                    start_pos = state.cursor.position;

                    if (is_line_terminator_echo(u32_view(u32s)))
                    {
                        // 这条 completion 仍由调用者报告原始字节数已写入；这里只是
                        // 抑制终端输出，避免 Enter 后多出空行。
                        bridge.sync_cursor_after_write(state.cursor.position);
                        LOG("[api_write_console] swallowed enter echo newline");
                        return;
                    }
                }
                else
                {
                    // 真实 conhost 的 WriteConsole 从当前 Console 光标输出；宿主
                    // 终端光标可能因本地 echo 不同；若 bridge 已确认二者同步，
                    // 则跳过重复 CUP，避免按行小写入产生大量无意义 VT。
                    if (sb.viewport.snap_to_cursor(state.cursor.position, state.screen_buffer_size))
                        render_visible_viewport(state, sb, bridge);
                    if (!bridge.terminal_cursor_matches_buffer(start_pos))
                        bridge.vt_write_cup_buffer(start_pos);
                }
            }

            if (emit_console_attributes)
            {
                WORD attr = state.default_attributes;
                m = vt_message{};
                set_sgr_from_win32_attr(m, attr);
                bridge.vt_msg_send<vt_message_id::sgr>(m);
                vt_msg_apply_state(vt_message_id::sgr, m, state, sb);
            }

            const bool replay_utf8_to_terminal =
                !unicode && !emit_console_attributes && (output_code_page == CP_UTF8 || output_code_page == 65001);
            if (replay_utf8_to_terminal)
                bridge.vt_append_str(std::string_view{reinterpret_cast<const char *>(data), bytes});

            auto &output_parser = bridge.output_parser();

            {
                COREHOST_PERF_SCOPE_AMOUNT(write_console_parser, u32s.size());
                std::u32string_view input = u32_view(u32s);
                for (size_t i = 0; i < u32s.size();)
                {
                    if (auto count = output_parser.direct_ground_text_run_length(input.substr(i)); count != 0)
                    {
                        COREHOST_PERF_SCOPE(write_console_consume_msg);
                        consume_write_console_text_run(input.substr(i, count), state, sb, bridge,
                                                       !replay_utf8_to_terminal);
                        if (i + count + 1 < u32s.size() && input[i + count] == U'\r' &&
                            input[i + count + 1] == U'\n')
                        {
                            consume_write_console_line_feed(state, sb, bridge, !replay_utf8_to_terminal);
                            i += count + 2;
                            continue;
                        }
                        i += count;
                        continue;
                    }

                    if (input[i] == U'\r' && i + 1 < u32s.size() && u32s[i + 1] == U'\n' &&
                        output_parser.can_accept_direct_ground_text())
                    {
                        COREHOST_PERF_SCOPE(write_console_consume_msg);
                        consume_write_console_line_feed(state, sb, bridge, !replay_utf8_to_terminal);
                        i += 2;
                        continue;
                    }

                    if (input[i] == U'\r' && i + 1 < u32s.size() && u32s[i + 1] == U'\n' &&
                        output_parser.has_pending_text())
                    {
                        COREHOST_PERF_SCOPE(write_console_consume_msg);
                        (void)output_parser.flush_text();
                        auto &pm = output_parser.get();
                        consume_write_console_text_run(pm.payload.text, state, sb, bridge, !replay_utf8_to_terminal);
                        output_parser.reset<vt_message_id::text>();
                        consume_write_console_line_feed(state, sb, bridge, !replay_utf8_to_terminal);
                        i += 2;
                        continue;
                    }

                    if (input[i] == U'\x1b' && output_parser.has_pending_text())
                    {
                        if (auto count = simple_sgr_passthrough_length(input.substr(i)); count != 0)
                        {
                            COREHOST_PERF_SCOPE(write_console_consume_msg);
                            (void)output_parser.flush_text();
                            auto &pm = output_parser.get();
                            consume_write_console_text_run(pm.payload.text, state, sb, bridge,
                                                           !replay_utf8_to_terminal);
                            output_parser.reset<vt_message_id::text>();
                            if (!replay_utf8_to_terminal)
                            {
                                COREHOST_PERF_SCOPE_AMOUNT(write_console_sgr_passthrough, count);
                                bridge.vt_append_raw_sequence(input.substr(i, count));
                            }
                            i += count;
                            continue;
                        }
                    }

                    if (auto count = simple_sgr_passthrough_length(input.substr(i)); count != 0)
                    {
                        if (!replay_utf8_to_terminal)
                        {
                            COREHOST_PERF_SCOPE_AMOUNT(write_console_sgr_passthrough, count);
                            bridge.vt_append_raw_sequence(input.substr(i, count));
                        }
                        i += count;
                        continue;
                    }

                    const char32_t ch = u32s[i];
                    auto parsed = output_parser.parse_with_consumption(ch);
                    auto id = parsed.id;
                    if (id == vt_message_id::continue_ || id == vt_message_id::continue_text)
                    {
                        if (parsed.consumption == vt_parse_consumption::consumed)
                            ++i;
                        continue;
                    }

                    if (parsed.consumption == vt_parse_consumption::retry && id == vt_message_id::carriage_return &&
                        ch == U'\n')
                    {
                        id = vt_message_id::line_feed;
                        ++i;
                    }
                    else if (parsed.consumption == vt_parse_consumption::consumed &&
                             id == vt_message_id::carriage_return && i + 1 < u32s.size() && u32s[i + 1] == U'\n')
                    {
                        id = vt_message_id::line_feed;
                        i += 2;
                    }
                    else if (parsed.consumption == vt_parse_consumption::consumed)
                    {
                        ++i;
                    }

                    {
                        COREHOST_PERF_SCOPE(write_console_consume_msg);
                        dispatch_write_console_vt_message(id, output_parser, state, sb, bridge,
                                                          !replay_utf8_to_terminal);
                    }
                }
            }

            // 释放循环后残留的累积文本；没有控制字符结束的普通输出会走这里。
            {
                COREHOST_PERF_SCOPE(write_console_flush_parser);
                if (auto id = output_parser.flush_text(); id != vt_message_id::continue_)
                {
                    auto &pm = output_parser.get();
                    consume_write_console_text_run(pm.payload.text, state, sb, bridge, !replay_utf8_to_terminal);
                    output_parser.reset<vt_message_id::text>();
                }
                // 排空 parser 残留的 _pending_control，例如文本以单独 '\r' 结束。
                if (auto id = output_parser.parse(U'\0');
                    id != vt_message_id::continue_ && id != vt_message_id::continue_text)
                    dispatch_write_console_vt_message(id, output_parser, state, sb, bridge,
                                                      !replay_utf8_to_terminal);
            }

            // VT 可能仍在 bridge 缓冲中等待批量刷新；本地 cursor 状态必须在
            // completion 前同步，供下一次 ReadConsole 使用。
            {
                COREHOST_PERF_SCOPE(write_console_sync_cursor);
                bridge.sync_cursor_after_write(state.cursor.position);
            }

            LOG("[api_write_console] done: u32s_len=%zu sbytes=%lu end_cursor=(%d,%d) synced", u32s.size(),
                static_cast<unsigned long>(bytes), static_cast<int>(state.cursor.position.X),
                static_cast<int>(state.cursor.position.Y));
        }
    }
}

// ── WriteConsole: UTF-16/ANSI → char32_t → vt_message 驱动 ──
inline bool api_write_console(miniio::io_msg &msg, console_state &state, screen_buffer &sb, input_buffer &,
                              pipe_bridge &bridge)
{
    if (msg.descriptor.InputSize < sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_WRITECONSOLE_MSG))
    {
        miniio::prepare_completion(msg, status_invalid_parameter);
        return true;
    }

    auto *req = reinterpret_cast<CONSOLE_WRITECONSOLE_MSG *>(msg.body + sizeof(CONSOLE_MSG_HEADER));
    constexpr auto payload_offset = sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_WRITECONSOLE_MSG);
    auto payload = bridge.read_input_payload(msg, payload_offset);
    auto sbytes = static_cast<ULONG>(payload.size());
    auto consumed_bytes = req->Unicode ? sbytes - (sbytes % sizeof(wchar_t)) : sbytes;

    write_console_payload(req->Unicode != 0, payload.data(), consumed_bytes, state, sb, bridge,
                          (state.output_mode & ENABLE_VIRTUAL_TERMINAL_PROCESSING) == 0);
    req->NumBytes = consumed_bytes;
    ucomplete_sz(msg, sizeof(CONSOLE_WRITECONSOLE_MSG));
    return true;
}

inline bool api_raw_write_console(miniio::io_msg &msg, console_state &state, screen_buffer &sb, input_buffer &,
                                  pipe_bridge &bridge)
{
    // 原版 IoSorter 把 CONSOLE_IO_RAW_WRITE 伪造成 WriteConsoleA，而不是把
    // 客户端字节直接透传给终端。否则 WriteFile 写入的 OEM/ANSI 字节会被
    // 宿主终端当 UTF-8 显示，CJK live echo 会乱码。
    auto payload = bridge.read_input_payload(msg, 0);
    auto bytes = static_cast<ULONG>(payload.size());
    write_console_payload(false, payload.data(), bytes, state, sb, bridge,
                          (state.output_mode & ENABLE_VIRTUAL_TERMINAL_PROCESSING) == 0);
    miniio::prepare_completion(msg, 0, bytes);
    return true;
}

// ── ReadConsole ──
inline bool api_read_console(miniio::io_msg &msg, console_state &state, screen_buffer &, input_buffer &,
                             pipe_bridge &bridge)
{
    if (msg.descriptor.InputSize < sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_READCONSOLE_MSG))
    {
        miniio::prepare_completion(msg, status_invalid_parameter);
        return true;
    }

    auto *req = reinterpret_cast<CONSOLE_READCONSOLE_MSG *>(msg.body + sizeof(CONSOLE_MSG_HEADER));
    LOG("[api_read_console] unicode=%d ctrl_wakeup=%lu", req->Unicode, req->CtrlWakeupMask);
    // Ctrl+Z 只有在 processed input 模式下作为 EOF；raw 模式应作为普通字符。
    bool proc_z = req->ProcessControlZ && (state.input_mode & ENABLE_PROCESSED_INPUT);

    auto initial_bytes = req->InitialNumBytes;
    const BYTE *init_data = nullptr;
    if (initial_bytes > 0)
    {
        if (initial_bytes > message_output_tail_capacity(msg, sizeof(CONSOLE_READCONSOLE_MSG)))
        {
            miniio::prepare_completion(msg, status_invalid_parameter);
            return true;
        }

        // 原版只允许 ReadConsoleW 使用 InitialNumBytes。
        if (!req->Unicode)
        {
            miniio::prepare_completion(msg, 0xC000000D /* STATUS_INVALID_PARAMETER */);
            return true;
        }

        // Initial data 位于 ExeName 后。ExeNameLength 是 wchar_t 字符数，不是字节数。
        auto input_payload = msg.descriptor.InputSize - sizeof(CONSOLE_MSG_HEADER) - sizeof(CONSOLE_READCONSOLE_MSG);
        auto exe_bytes = static_cast<ULONG>(req->ExeNameLength) * static_cast<ULONG>(sizeof(wchar_t));
        if (exe_bytes > input_payload)
        {
            miniio::prepare_completion(msg, 0xC000000D /* STATUS_INVALID_PARAMETER */);
            return true;
        }

        init_data = msg.body + sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_READCONSOLE_MSG) + exe_bytes;
        auto available_initial_bytes = input_payload - exe_bytes;
        if (initial_bytes > available_initial_bytes)
            initial_bytes = available_initial_bytes;
    }

    return bridge.handle_console_read(msg, proc_z, init_data, initial_bytes);
}

inline bool api_deprecated_l1(miniio::io_msg &msg, console_state &, screen_buffer &, input_buffer &, pipe_bridge &)
{
    miniio::prepare_completion(msg, status_not_implemented);
    return true;
}

// ════════════════════════════════════════════════════════
// L2 API helpers
// ════════════════════════════════════════════════════════

// ════════════════════════════════════════════════════════
// L2 API handlers
// ════════════════════════════════════════════════════════

inline bool api_fill_output(miniio::io_msg &msg, console_state &state, screen_buffer &sb, input_buffer &,
                            pipe_bridge &bridge)
{
    if (msg.descriptor.InputSize < sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_FILLCONSOLEOUTPUT_MSG))
    {
        miniio::prepare_completion(msg, status_invalid_parameter);
        return true;
    }

    auto *r = reinterpret_cast<CONSOLE_FILLCONSOLEOUTPUT_MSG *>(msg.body + sizeof(CONSOLE_MSG_HEADER));
    ULONG orig_length = r->Length;
    r->Length = 0;
    if (r->ElementType != CONSOLE_ASCII && r->ElementType != CONSOLE_REAL_UNICODE &&
        r->ElementType != CONSOLE_FALSE_UNICODE && r->ElementType != CONSOLE_ATTRIBUTE)
    {
        miniio::prepare_completion(msg, 0xC000000D /* STATUS_INVALID_PARAMETER */);
        return true;
    }

    char32_t fill_char = static_cast<char32_t>(r->Element);
    if (r->ElementType == CONSOLE_ASCII)
    {
        const char byte = static_cast<char>(r->Element & 0xFF);
        convert_ansi_to_u32(&byte, 1, state.output_code_page ? state.output_code_page : CP_ACP, bridge.conv_u32(),
                            bridge.conv_wstr());
        if (!bridge.conv_u32().empty())
            fill_char = bridge.conv_u32().front();
    }

    if (r->ElementType == CONSOLE_ATTRIBUTE)
    {
        auto res = sb.fill_attr(static_cast<WORD>(r->Element), r->WriteCoord, orig_length);
        r->Length = res.cells_modified;
    }
    else
    {
        auto res = sb.fill_char(fill_char, r->WriteCoord, orig_length);
        r->Length = res.cells_modified;
    }

    bool is_fullscreen_space =
        (r->ElementType != CONSOLE_ATTRIBUTE && r->WriteCoord.X == 0 && r->WriteCoord.Y == 0 && fill_char == U' ' &&
         orig_length >=
             static_cast<ULONG>(state.screen_buffer_size.X) * static_cast<ULONG>(state.screen_buffer_size.Y));

    LOG("[api_fill_output] at=(%d,%d) len=%lu elem='%c'(%d) type=%d fullscreen=%d", r->WriteCoord.X, r->WriteCoord.Y,
        orig_length,
        (r->ElementType != CONSOLE_ATTRIBUTE && r->Element >= 32 && r->Element < 127) ? (char)r->Element : '?',
        r->Element, r->ElementType, is_fullscreen_space ? 1 : 0);
    if (!viewport_covers_screen_buffer(sb))
    {
        render_visible_viewport(state, sb, bridge);
        ucomplete_sz(msg, sizeof(CONSOLE_FILLCONSOLEOUTPUT_MSG));
        return true;
    }
    if (is_fullscreen_space)
    {
        // 全屏空格填充通常来自 Clear-Host/cls。ED2 清可见屏幕，ED3 请求
        // Windows Terminal 同时清 scrollback，避免滚动条拖回旧内容。
        bridge.reset_enter_newline();
        vt_message erase_display{};
        erase_display.payload.erase_mode = 2;
        bridge.vt_msg_send<vt_message_id::erase_in_display>(erase_display);
        vt_msg_apply_state(vt_message_id::erase_in_display, erase_display, state, sb);
        vt_message erase_scrollback{};
        erase_scrollback.payload.erase_mode = 3;
        bridge.vt_msg_send<vt_message_id::erase_in_display>(erase_scrollback);
        vt_msg_apply_state(vt_message_id::erase_in_display, erase_scrollback, state, sb);
        bridge.vt_flush();
    }
    else
    {
        // 局部填充不应改变应用可见光标。终端侧用 save/CUP/write/restore，
        // 本地 screen_buffer 已在上方 fill_* 完成。
        vt_message msg_save{};
        bridge.vt_msg_send<vt_message_id::save_cursor>(msg_save);
        vt_msg_apply_state(vt_message_id::save_cursor, msg_save, state, sb);

        vt_message msg_cup{};
        msg_cup.payload.position.row = static_cast<short>(r->WriteCoord.Y + 1);
        msg_cup.payload.position.col = static_cast<short>(r->WriteCoord.X + 1);
        bridge.vt_msg_send<vt_message_id::cursor_position>(msg_cup);
        vt_msg_apply_state(vt_message_id::cursor_position, msg_cup, state, sb);

        if (r->ElementType == CONSOLE_ATTRIBUTE)
        {
            // 属性填充需要重绘受影响的既有字符；只发送 SGR 会改变后续输出
            // 颜色，却不会让终端上已经显示的 cell 变色。
            WORD fill_attr = static_cast<WORD>(r->Element);
            vt_message m_sgr{};
            set_sgr_from_win32_attr(m_sgr, fill_attr);
            auto remaining = r->Length;
            auto y = r->WriteCoord.Y;
            auto x = r->WriteCoord.X;
            auto &fill_text = bridge.conv_u32();
            while (remaining > 0 && y >= 0 && y < sb.size.Y && x >= 0 && x < sb.size.X)
            {
                const auto n = std::min<ULONG>(remaining, static_cast<ULONG>(static_cast<ULONG>(sb.size.X) - x));
                vt_message row_cup{};
                row_cup.payload.position.row = static_cast<short>(y + 1);
                row_cup.payload.position.col = static_cast<short>(x + 1);
                bridge.vt_msg_send<vt_message_id::cursor_position>(row_cup);
                bridge.vt_msg_send<vt_message_id::sgr>(m_sgr);

                fill_text.clear();
                fill_text.reserve(n);
                for (ULONG i = 0; i < n; ++i)
                    fill_text.push_back(sb.at_u32({static_cast<SHORT>(x + i), y}));
                vt_message m_text{};
                m_text.payload.text = u32_view(fill_text);
                bridge.vt_msg_send<vt_message_id::text>(m_text);

                remaining -= n;
                x = 0;
                ++y;
            }

            vt_message restore_attr{};
            set_sgr_from_win32_attr(restore_attr, state.default_attributes);
            bridge.vt_msg_send<vt_message_id::sgr>(restore_attr);
        }
        else
        {
            // text fill 使用重复 codepoint 构造临时 text 视图；缓冲属于 bridge，
            // vt_msg_send 只在调用期间读取。
            auto &fill_text = bridge.conv_u32();
            fill_text.assign(static_cast<size_t>(r->Length), fill_char);
            vt_message m_text{};
            m_text.payload.text = u32_view(fill_text);
            bridge.vt_msg_send<vt_message_id::text>(m_text);
            vt_msg_apply_state(vt_message_id::text, m_text, state, sb);
        }

        vt_message msg_restore{};
        bridge.vt_msg_send<vt_message_id::restore_cursor>(msg_restore);
        vt_msg_apply_state(vt_message_id::restore_cursor, msg_restore, state, sb);
        bridge.vt_flush();
    }

    ucomplete_sz(msg, sizeof(CONSOLE_FILLCONSOLEOUTPUT_MSG));
    return true;
}

inline bool api_ctrl_event(miniio::io_msg &msg, console_state &, screen_buffer &, input_buffer &, pipe_bridge &)
{
    if (msg.descriptor.InputSize < sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_CTRLEVENT_MSG))
    {
        miniio::prepare_completion(msg, status_invalid_parameter);
        return true;
    }

    auto *r = reinterpret_cast<CONSOLE_CTRLEVENT_MSG *>(msg.body + sizeof(CONSOLE_MSG_HEADER));
    ::GenerateConsoleCtrlEvent(r->CtrlEvent, r->ProcessGroupId);
    ucomplete(msg);
    return true;
}

inline bool api_set_active_sb(miniio::io_msg &msg, console_state &, screen_buffer &, input_buffer &, pipe_bridge &)
{
    ucomplete(msg);
    return true;
}

inline bool api_flush_input_buf(miniio::io_msg &msg, console_state &, screen_buffer &, input_buffer &inp,
                                pipe_bridge &bridge)
{
    // FlushConsoleInputBuffer 也要取消等待输入的 pending 读，否则客户端可能在
    // 已清空队列后继续挂起。
    inp.flush();
    bridge.cancel_pending_read();
    ucomplete(msg);
    return true;
}

inline bool api_set_cp(miniio::io_msg &msg, console_state &state, screen_buffer &, input_buffer &, pipe_bridge &)
{
    if (msg.descriptor.InputSize < sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_SETCP_MSG))
    {
        miniio::prepare_completion(msg, status_invalid_parameter);
        return true;
    }

    auto *r = reinterpret_cast<CONSOLE_SETCP_MSG *>(msg.body + sizeof(CONSOLE_MSG_HEADER));
    if (!::IsValidCodePage(r->CodePage))
    {
        miniio::prepare_completion(msg, status_invalid_parameter);
        return true;
    }
    if (r->Output)
        state.output_code_page = r->CodePage;
    else
        state.input_code_page = r->CodePage;
    ucomplete(msg);
    return true;
}

inline bool api_get_cursor(miniio::io_msg &msg, console_state &state, screen_buffer &, input_buffer &, pipe_bridge &)
{
    if (msg.descriptor.InputSize < sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_GETCURSORINFO_MSG))
    {
        miniio::prepare_completion(msg, status_invalid_parameter);
        return true;
    }

    auto *r = reinterpret_cast<CONSOLE_GETCURSORINFO_MSG *>(msg.body + sizeof(CONSOLE_MSG_HEADER));
    r->CursorSize = state.cursor.size;
    r->Visible = state.cursor.visible ? TRUE : FALSE;
    ucomplete_sz(msg, sizeof(CONSOLE_GETCURSORINFO_MSG));
    return true;
}

inline bool api_set_cursor(miniio::io_msg &msg, console_state &state, screen_buffer &sb, input_buffer &,
                           pipe_bridge &bridge)
{
    if (msg.descriptor.InputSize < sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_SETCURSORINFO_MSG))
    {
        miniio::prepare_completion(msg, status_invalid_parameter);
        return true;
    }

    // CursorSize 只保存在本地状态；当前 VT 输出只同步可见性。
    auto *r = reinterpret_cast<CONSOLE_SETCURSORINFO_MSG *>(msg.body + sizeof(CONSOLE_MSG_HEADER));
    if (r->CursorSize == 0 || r->CursorSize > 100)
    {
        miniio::prepare_completion(msg, status_invalid_parameter);
        return true;
    }
    state.cursor.size = r->CursorSize;
    state.cursor.visible = r->Visible != FALSE;
    vt_message m{};
    if (state.cursor.visible)
        bridge.vt_msg_send<vt_message_id::cursor_show>(m);
    else
        bridge.vt_msg_send<vt_message_id::cursor_hide>(m);
    vt_msg_apply_state(state.cursor.visible ? vt_message_id::cursor_show : vt_message_id::cursor_hide, m, state, sb);
    bridge.vt_flush();
    ucomplete(msg);
    return true;
}

inline bool api_get_sb_info(miniio::io_msg &msg, console_state &state, screen_buffer &sb, input_buffer &, pipe_bridge &)
{
    if (msg.descriptor.InputSize < sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_SCREENBUFFERINFO_MSG))
    {
        miniio::prepare_completion(msg, status_invalid_parameter);
        return true;
    }

    auto *r = reinterpret_cast<CONSOLE_SCREENBUFFERINFO_MSG *>(msg.body + sizeof(CONSOLE_MSG_HEADER));
    std::memset(r, 0, sizeof(*r));
    if (state.cursor_position_dirty)
        // dirty 标志表示之前有延迟 cursor 同步需求；查询时返回当前 state 后即可清除。
        state.cursor_position_dirty = false;
    r->Size = state.screen_buffer_size;
    r->CursorPosition = state.cursor.position;
    r->ScrollPosition = sb.viewport.origin();
    r->Attributes = state.default_attributes;
    r->CurrentWindowSize = sb.viewport.size();
    r->MaximumWindowSize = state.max_window_size;
    r->PopupAttributes = state.popup_attributes;
    r->FullscreenSupported = FALSE;
    std::memcpy(r->ColorTable, state.color_table.data(), state.color_table.size() * sizeof(state.color_table[0]));
    // GetSBInfo 是 PSReadLine 高频轮询 API，不在这里记录普通路径日志。
    ucomplete_sz(msg, sizeof(CONSOLE_SCREENBUFFERINFO_MSG));
    return true;
}

inline bool api_set_sb_info(miniio::io_msg &msg, console_state &state, screen_buffer &sb, input_buffer &,
                            pipe_bridge &bridge)
{
    if (msg.descriptor.InputSize < sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_SCREENBUFFERINFO_MSG))
    {
        miniio::prepare_completion(msg, status_invalid_parameter);
        return true;
    }

    // SetScreenBufferInfoEx 不设置 cursor position；cursor 只在尺寸变化后
    // 保证仍位于缓冲区内。
    auto *r = reinterpret_cast<CONSOLE_SCREENBUFFERINFO_MSG *>(msg.body + sizeof(CONSOLE_MSG_HEADER));
    if (r->Size.X <= 0 || r->Size.Y <= 0 || r->Size.X == SHRT_MAX || r->Size.Y == SHRT_MAX ||
        r->CurrentWindowSize.X <= 0 || r->CurrentWindowSize.Y <= 0 || r->CurrentWindowSize.X > r->Size.X ||
        r->CurrentWindowSize.Y > r->Size.Y)
    {
        miniio::prepare_completion(msg, status_invalid_parameter);
        return true;
    }
    state.screen_buffer_size = r->Size;
    state.default_attributes = r->Attributes;
    state.max_window_size = r->MaximumWindowSize;
    state.popup_attributes = r->PopupAttributes;
    std::memcpy(state.color_table.data(), r->ColorTable, state.color_table.size() * sizeof(state.color_table[0]));
    const auto old_viewport_size = sb.viewport.size();
    sb.resize(r->Size);
    bool viewport_changed = sb.viewport.set_size(r->CurrentWindowSize, sb.size);
    viewport_changed = sb.viewport.clamp_to_buffer(sb.size) || viewport_changed;
    state.clamp_cursor_to_buffer();
    const auto new_viewport_size = sb.viewport.size();
    if (old_viewport_size.X != new_viewport_size.X || old_viewport_size.Y != new_viewport_size.Y)
    {
        bridge.vt_append_str("\x1b[8;"sv);
        bridge.vt_append_int(new_viewport_size.Y);
        bridge.vt_append_char(';');
        bridge.vt_append_int(new_viewport_size.X);
        bridge.vt_append_str("t"sv);
    }
    if (viewport_changed || !viewport_covers_screen_buffer(sb))
        render_visible_viewport(state, sb, bridge);
    ucomplete(msg);
    return true;
}

inline bool api_set_sb_size(miniio::io_msg &msg, console_state &state, screen_buffer &sb, input_buffer &,
                            pipe_bridge &bridge)
{
    if (msg.descriptor.InputSize < sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_SETSCREENBUFFERSIZE_MSG))
    {
        miniio::prepare_completion(msg, status_invalid_parameter);
        return true;
    }

    auto *r = reinterpret_cast<CONSOLE_SETSCREENBUFFERSIZE_MSG *>(msg.body + sizeof(CONSOLE_MSG_HEADER));
    COORD new_size = r->Size;
    if (new_size.X < 1 || new_size.Y < 1 || new_size.X == SHRT_MAX || new_size.Y == SHRT_MAX ||
        new_size.X < sb.viewport.size().X || new_size.Y < sb.viewport.size().Y)
    {
        miniio::prepare_completion(msg, status_invalid_parameter);
        return true;
    }
    state.screen_buffer_size = new_size;
    sb.resize(new_size);
    const bool viewport_changed = sb.viewport.clamp_to_buffer(sb.size);
    state.clamp_cursor_to_buffer();
    if (viewport_changed || !viewport_covers_screen_buffer(sb))
        render_visible_viewport(state, sb, bridge);
    ucomplete(msg);
    return true;
}

inline bool api_set_cursor_pos(miniio::io_msg &msg, console_state &state, screen_buffer &sb, input_buffer &,
                               pipe_bridge &bridge)
{
    if (msg.descriptor.InputSize < sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_SETCURSORPOSITION_MSG))
    {
        miniio::prepare_completion(msg, status_invalid_parameter);
        return true;
    }

    auto *r = reinterpret_cast<CONSOLE_SETCURSORPOSITION_MSG *>(msg.body + sizeof(CONSOLE_MSG_HEADER));
    COORD new_pos = r->CursorPosition;
    if (new_pos.X < 0 || new_pos.X >= state.screen_buffer_size.X || new_pos.Y < 0 ||
        new_pos.Y >= state.screen_buffer_size.Y)
    {
        miniio::prepare_completion(msg, status_invalid_parameter);
        return true;
    }

    LOG("[api_set_cursor_pos] to=(%d,%d) was=(%d,%d)", new_pos.X, new_pos.Y, state.cursor.position.X,
        state.cursor.position.Y);
    if (new_pos.X == 0 && new_pos.Y == 0)
        // 清屏序列常先 SetCursorPos(0,0)，这时 Enter 的一次性换行标志已过期。
        bridge.reset_enter_newline();
    state.cursor.position = new_pos;
    if (sb.viewport.snap_to_cursor(state.cursor.position, state.screen_buffer_size))
        render_visible_viewport(state, sb, bridge);
    bridge.vt_write_cup_buffer(new_pos);
    bridge.vt_flush();
    // 该 API 直接改变终端光标，bridge 的行编辑光标必须同步到同一位置。
    bridge.sync_cursor_after_write(state.cursor.position);
    ucomplete(msg);
    return true;
}

inline bool api_largest_window(miniio::io_msg &msg, console_state &state, screen_buffer &, input_buffer &,
                               pipe_bridge &)
{
    if (msg.descriptor.InputSize < sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_GETLARGESTWINDOWSIZE_MSG))
    {
        miniio::prepare_completion(msg, status_invalid_parameter);
        return true;
    }

    auto *r = reinterpret_cast<CONSOLE_GETLARGESTWINDOWSIZE_MSG *>(msg.body + sizeof(CONSOLE_MSG_HEADER));
    r->Size = state.max_window_size;
    ucomplete_sz(msg, sizeof(CONSOLE_GETLARGESTWINDOWSIZE_MSG));
    return true;
}

inline bool api_scroll_sb(miniio::io_msg &msg, console_state &state, screen_buffer &sb, input_buffer &,
                          pipe_bridge &bridge)
{
    if (msg.descriptor.InputSize < sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_SCROLLSCREENBUFFER_MSG))
    {
        miniio::prepare_completion(msg, status_invalid_parameter);
        return true;
    }

    auto *r = reinterpret_cast<CONSOLE_SCROLLSCREENBUFFER_MSG *>(msg.body + sizeof(CONSOLE_MSG_HEADER));
    auto &sr = r->ScrollRectangle;
    LOG("[api_scroll_sb] sr=(%d,%d,%d,%d) clip=(%d,%d,%d,%d) dest=(%d,%d) fill_char=0x%X fill_attr=0x%X", sr.Left,
        sr.Top, sr.Right, sr.Bottom, r->ClipRectangle.Left, r->ClipRectangle.Top, r->ClipRectangle.Right,
        r->ClipRectangle.Bottom, r->DestinationOrigin.X, r->DestinationOrigin.Y, r->Fill.Char.UnicodeChar,
        r->Fill.Attributes);
    if ((sr.Left == r->DestinationOrigin.X && sr.Top == r->DestinationOrigin.Y) || sr.Left > sr.Right ||
        sr.Top > sr.Bottom)
    {
        // 原版把空 source 或移动到自身视为成功 no-op。
        ucomplete(msg);
        return true;
    }

    char32_t fill_char = static_cast<char32_t>(r->Fill.Char.UnicodeChar);
    WORD fill_attr = r->Fill.Attributes;
    if (!r->Unicode)
    {
        const char byte = r->Fill.Char.AsciiChar;
        convert_ansi_to_u32(&byte, 1, state.output_code_page ? state.output_code_page : CP_ACP, bridge.conv_u32(),
                            bridge.conv_wstr());
        if (!bridge.conv_u32().empty())
            fill_char = bridge.conv_u32().front();
    }
    if (fill_char == U'\0' && fill_attr == 0)
    {
        fill_char = U' ';
        fill_attr = state.default_attributes;
    }

    sb.scroll(r->ScrollRectangle, r->ClipRectangle, r->Clip != FALSE, r->DestinationOrigin, fill_char, fill_attr);

    SHORT buf_height = sb.size.Y;
    bool full_screen_scroll =
        (sr.Left <= 0 && sr.Top <= 0 && sr.Right >= sb.size.X - 1 && sr.Bottom >= buf_height - 1 &&
         r->DestinationOrigin.X == 0 && r->DestinationOrigin.Y <= -buf_height && r->Clip == FALSE);
    LOG("[api_scroll_sb] full_screen=%d buf_h=%d sr.Bottom=%d fill_char=0x%X", full_screen_scroll, buf_height,
        sr.Bottom, static_cast<unsigned>(fill_char));
    if (!viewport_covers_screen_buffer(sb))
    {
        render_visible_viewport(state, sb, bridge);
        ucomplete(msg);
        return true;
    }
    if (full_screen_scroll && fill_char == U' ')
    {
        // cls 清屏使用 ED2+ED3+Home，避免把整屏滚动翻译成大量 IL/DL；
        // ED3 清掉终端 scrollback。
        bridge.vt_append_str("\x1b[2J\x1b[3J\x1b[H"sv);
        bridge.vt_flush();
        // ── 同步 state 光标和终端光标到 (0,0) ──
        state.cursor.position = {0, 0};
        bridge.sync_cursor_after_write({0, 0});
        LOG("[api_scroll_sb] cls: sent ED2+ED3+H, cursor->(0,0)");
    }
    else
    {
        // 局部滚动用临时 scroll region 约束终端影响范围，并在结束后恢复光标。
        vt_message m_save{};
        bridge.vt_msg_send<vt_message_id::save_cursor>(m_save);

        SMALL_RECT cr = r->Clip
                            ? r->ClipRectangle
                            : SMALL_RECT{0, 0, static_cast<SHORT>(sb.size.X - 1), static_cast<SHORT>(sb.size.Y - 1)};
        vt_message m_region{};
        m_region.payload.scroll_region.top = static_cast<short>(cr.Top + 1);
        m_region.payload.scroll_region.bottom = static_cast<short>(cr.Bottom + 1);
        bridge.vt_msg_send<vt_message_id::set_scrolling_region>(m_region);
        vt_msg_apply_state(vt_message_id::set_scrolling_region, m_region, state, sb);

        vt_message m_cup{};
        m_cup.payload.position.row = static_cast<short>(sr.Bottom + 1);
        m_cup.payload.position.col = static_cast<short>(sr.Left + 1);
        bridge.vt_msg_send<vt_message_id::cursor_position>(m_cup);

        SHORT dy = r->DestinationOrigin.Y - sr.Top;
        // dy<0 表示目标在上方，需要插入行把内容向下推；dy>0 则删除行。
        if (dy < 0)
        {
            vt_message m_il{};
            m_il.payload.count.value = -dy;
            bridge.vt_msg_send<vt_message_id::insert_lines>(m_il);
            vt_msg_apply_state(vt_message_id::insert_lines, m_il, state, sb);
        }
        else if (dy > 0)
        {
            vt_message m_dl{};
            m_dl.payload.count.value = dy;
            bridge.vt_msg_send<vt_message_id::delete_lines>(m_dl);
            vt_msg_apply_state(vt_message_id::delete_lines, m_dl, state, sb);
        }

        vt_message m_reset{};
        m_reset.payload.scroll_region.top = 1;
        m_reset.payload.scroll_region.bottom = 0;
        bridge.vt_msg_send<vt_message_id::set_scrolling_region>(m_reset);

        vt_message m_restore{};
        bridge.vt_msg_send<vt_message_id::restore_cursor>(m_restore);
        bridge.vt_flush();
    } // end else (!full_screen_scroll)

    ucomplete(msg);
    return true;
}

inline bool api_set_text_attr(miniio::io_msg &msg, console_state &state, screen_buffer &sb, input_buffer &,
                              pipe_bridge &bridge)
{
    if (msg.descriptor.InputSize < sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_SETTEXTATTRIBUTE_MSG))
    {
        miniio::prepare_completion(msg, status_invalid_parameter);
        return true;
    }

    // SetConsoleTextAttribute 影响后续输出默认属性，同时立即同步宿主终端 SGR。
    auto *r = reinterpret_cast<CONSOLE_SETTEXTATTRIBUTE_MSG *>(msg.body + sizeof(CONSOLE_MSG_HEADER));
    static constexpr WORD valid_text_attributes = 0xDFFF;
    if ((r->Attributes & ~valid_text_attributes) != 0)
    {
        miniio::prepare_completion(msg, status_invalid_parameter);
        return true;
    }
    state.default_attributes = r->Attributes;
    vt_message m{};
    WORD attr = r->Attributes;
    set_sgr_from_win32_attr(m, attr);
    bridge.vt_msg_send<vt_message_id::sgr>(m);
    vt_msg_apply_state(vt_message_id::sgr, m, state, sb);
    bridge.vt_flush();
    ucomplete(msg);
    return true;
}

inline bool api_set_window_info(miniio::io_msg &msg, console_state &state, screen_buffer &sb, input_buffer &,
                                pipe_bridge &bridge)
{
    if (msg.descriptor.InputSize < sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_SETWINDOWINFO_MSG))
    {
        miniio::prepare_completion(msg, status_invalid_parameter);
        return true;
    }

    auto *r = reinterpret_cast<CONSOLE_SETWINDOWINFO_MSG *>(msg.body + sizeof(CONSOLE_MSG_HEADER));
    SMALL_RECT window = r->Window;
    if (!r->Absolute)
    {
        const auto current = sb.viewport.rect();
        window.Left = static_cast<SHORT>(window.Left + current.Left);
        window.Right = static_cast<SHORT>(window.Right + current.Right);
        window.Top = static_cast<SHORT>(window.Top + current.Top);
        window.Bottom = static_cast<SHORT>(window.Bottom + current.Bottom);
    }

    if (window.Right < window.Left || window.Bottom < window.Top)
    {
        miniio::prepare_completion(msg, status_invalid_parameter);
        return true;
    }

    const COORD requested_size{static_cast<SHORT>(window.Right - window.Left + 1),
                               static_cast<SHORT>(window.Bottom - window.Top + 1)};
    if (requested_size.X > state.screen_buffer_size.X || requested_size.Y > state.screen_buffer_size.Y)
    {
        state.screen_buffer_size.X = std::max(state.screen_buffer_size.X, requested_size.X);
        state.screen_buffer_size.Y = std::max(state.screen_buffer_size.Y, requested_size.Y);
        sb.resize(state.screen_buffer_size);
    }

    const auto old_size = sb.viewport.size();
    sb.viewport.set_rect(window, sb.size);
    const auto new_size = sb.viewport.size();
    if (old_size.X != new_size.X || old_size.Y != new_size.Y)
    {
        bridge.vt_append_str("\x1b[8;"sv);
        bridge.vt_append_int(new_size.Y);
        bridge.vt_append_char(';');
        bridge.vt_append_int(new_size.X);
        bridge.vt_append_str("t"sv);
    }
    render_visible_viewport(state, sb, bridge);
    ucomplete(msg);
    return true;
}

inline bool api_read_output_string(miniio::io_msg &msg, console_state &state, screen_buffer &sb, input_buffer &,
                                   pipe_bridge &bridge)
{
    if (msg.descriptor.InputSize < sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_READCONSOLEOUTPUTSTRING_MSG))
    {
        miniio::prepare_completion(msg, status_invalid_parameter);
        return true;
    }

    auto *r = reinterpret_cast<CONSOLE_READCONSOLEOUTPUTSTRING_MSG *>(msg.body + sizeof(CONSOLE_MSG_HEADER));
    r->NumRecords = 0;
    LOG("[api_read_output_string] at=(%d,%d) type=%d", r->ReadCoord.X, r->ReadCoord.Y, r->StringType);
    if (r->StringType != CONSOLE_ASCII && r->StringType != CONSOLE_REAL_UNICODE &&
        r->StringType != CONSOLE_FALSE_UNICODE && r->StringType != CONSOLE_ATTRIBUTE)
    {
        miniio::prepare_completion(msg, status_invalid_parameter);
        return true;
    }
    auto data_capacity = msg.descriptor.OutputSize > sizeof(CONSOLE_READCONSOLEOUTPUTSTRING_MSG)
                             ? msg.descriptor.OutputSize - sizeof(CONSOLE_READCONSOLEOUTPUTSTRING_MSG)
                             : 0;
    if (r->StringType == CONSOLE_ATTRIBUTE)
    {
        // ATTRIBUTE 读取 WORD 数组；其他字符类型都降级为 wchar_t 输出。
        auto *out = reinterpret_cast<WORD *>(msg.body + sizeof(CONSOLE_MSG_HEADER) +
                                             sizeof(CONSOLE_READCONSOLEOUTPUTSTRING_MSG));
        auto maxn = data_capacity / sizeof(WORD);
        r->NumRecords = static_cast<ULONG>(sb.read_attrs_linear(r->ReadCoord, out, maxn));
    }
    else if (r->StringType == CONSOLE_ASCII)
    {
        auto *out = reinterpret_cast<char *>(msg.body + sizeof(CONSOLE_MSG_HEADER) +
                                             sizeof(CONSOLE_READCONSOLEOUTPUTSTRING_MSG));
        auto maxb = data_capacity;
        auto &wbuf = bridge.conv_wstr();
        wbuf.resize(maxb);
        auto wchar_count = sb.read_wchars_linear(r->ReadCoord, wbuf.data(), wbuf.size());
        auto cp = state.output_code_page ? state.output_code_page : CP_ACP;
        int bytes = ::WideCharToMultiByte(cp, 0, wbuf.data(), static_cast<int>(wchar_count), out,
                                          static_cast<int>(maxb), nullptr, nullptr);
        r->NumRecords = bytes > 0 ? static_cast<ULONG>(bytes) : 0;
    }
    else
    {
        auto *out = reinterpret_cast<wchar_t *>(msg.body + sizeof(CONSOLE_MSG_HEADER) +
                                                sizeof(CONSOLE_READCONSOLEOUTPUTSTRING_MSG));
        auto maxn = data_capacity / sizeof(wchar_t);
        r->NumRecords = static_cast<ULONG>(sb.read_wchars_linear(r->ReadCoord, out, maxn));
    }
    ucomplete_sz(msg, static_cast<ULONG>(sizeof(CONSOLE_READCONSOLEOUTPUTSTRING_MSG) + data_capacity));
    return true;
}

inline bool api_write_console_input(miniio::io_msg &msg, console_state &state, screen_buffer &, input_buffer &inp,
                                    pipe_bridge &)
{
    if (msg.descriptor.InputSize < sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_WRITECONSOLEINPUT_MSG))
    {
        miniio::prepare_completion(msg, status_invalid_parameter);
        return true;
    }

    // W 事件原样入队；A 事件先按当前输入代码页转换 KEY_EVENT 字符。
    auto *r = reinterpret_cast<CONSOLE_WRITECONSOLEINPUT_MSG *>(msg.body + sizeof(CONSOLE_MSG_HEADER));
    r->NumRecords = 0;
    auto *records =
        reinterpret_cast<INPUT_RECORD *>(msg.body + sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_WRITECONSOLEINPUT_MSG));
    auto ib = msg.descriptor.InputSize - sizeof(CONSOLE_MSG_HEADER) - sizeof(CONSOLE_WRITECONSOLEINPUT_MSG);
    auto nrec = static_cast<size_t>(ib / sizeof(INPUT_RECORD));
    size_t written = 0;
    if (nrec > 0 && r->Unicode)
    {
        written = r->Append ? inp.write(records, nrec) : inp.prepend(records, nrec);
    }
    else if (nrec > 0)
    {
        std::vector<INPUT_RECORD> converted;
        converted.reserve(nrec);
        const auto cp = state.input_code_page ? state.input_code_page : CP_ACP;
        for (size_t i = 0; i < nrec; ++i)
        {
            auto rec = records[i];
            if (rec.EventType != KEY_EVENT)
            {
                converted.push_back(rec);
                continue;
            }

            char bytes[2]{rec.Event.KeyEvent.uChar.AsciiChar, 0};
            auto byte_count = 1;
            if (::IsDBCSLeadByteEx(cp, static_cast<BYTE>(bytes[0])) && i + 1 < nrec &&
                records[i + 1].EventType == KEY_EVENT)
            {
                bytes[1] = records[i + 1].Event.KeyEvent.uChar.AsciiChar;
                byte_count = 2;
                ++i;
            }

            wchar_t wide[2]{};
            const auto wide_count = ::MultiByteToWideChar(cp, 0, bytes, byte_count, wide, 2);
            for (int j = 0; j < wide_count; ++j)
            {
                rec.Event.KeyEvent.uChar.UnicodeChar = wide[j];
                converted.push_back(rec);
            }
        }
        if (!converted.empty())
            written = r->Append ? inp.write(converted.data(), converted.size())
                                : inp.prepend(converted.data(), converted.size());
    }
    r->NumRecords = static_cast<ULONG>(written);
    ucomplete_sz(msg, sizeof(CONSOLE_WRITECONSOLEINPUT_MSG));
    return true;
}

inline bool api_write_console_output(miniio::io_msg &msg, console_state &state, screen_buffer &sb, input_buffer &,
                                     pipe_bridge &bridge)
{
    if (msg.descriptor.InputSize < sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_WRITECONSOLEOUTPUT_MSG))
    {
        miniio::prepare_completion(msg, status_invalid_parameter);
        return true;
    }

    auto *r = reinterpret_cast<CONSOLE_WRITECONSOLEOUTPUT_MSG *>(msg.body + sizeof(CONSOLE_MSG_HEADER));
    SMALL_RECT &cr = r->CharRegion;
    if (cr.Left > cr.Right || cr.Top > cr.Bottom)
    {
        cr.Left = cr.Right = cr.Top = cr.Bottom = 0;
        ucomplete_sz(msg, sizeof(CONSOLE_WRITECONSOLEOUTPUT_MSG));
        return true;
    }
    SMALL_RECT orig = cr;
    // CHAR_INFO 输入按请求矩形宽度连续排列；本地 screen_buffer 逐行导入，
    // 超出当前缓冲区高度的行静默忽略。
    auto *data = reinterpret_cast<const CHAR_INFO *>(msg.body + sizeof(CONSOLE_MSG_HEADER) +
                                                     sizeof(CONSOLE_WRITECONSOLEOUTPUT_MSG));
    SHORT w = orig.Right - orig.Left + 1;
    auto h = orig.Bottom - orig.Top + 1;
    auto input_bytes = msg.descriptor.InputSize - sizeof(CONSOLE_MSG_HEADER) - sizeof(CONSOLE_WRITECONSOLEOUTPUT_MSG);
    auto required = static_cast<size_t>(w) * static_cast<size_t>(h) * sizeof(CHAR_INFO);
    if (input_bytes < required)
    {
        cr.Left = cr.Right = cr.Top = cr.Bottom = 0;
        miniio::prepare_completion(msg, status_invalid_parameter);
        return true;
    }
    std::vector<CHAR_INFO> unicode_data;
    if (!r->Unicode)
    {
        unicode_data.resize(static_cast<size_t>(w) * static_cast<size_t>(h));
        const auto cp = state.output_code_page ? state.output_code_page : CP_ACP;
        for (size_t i = 0; i < unicode_data.size(); ++i)
        {
            auto ch = static_cast<char>(data[i].Char.AsciiChar);
            wchar_t wc = L' ';
            ::MultiByteToWideChar(cp, MB_USEGLYPHCHARS, &ch, 1, &wc, 1);
            unicode_data[i].Char.UnicodeChar = wc;
            unicode_data[i].Attributes = data[i].Attributes;
        }
        data = unicode_data.data();
    }

    SMALL_RECT clipped = orig;
    if (clipped.Left < 0)
        clipped.Left = 0;
    if (clipped.Top < 0)
        clipped.Top = 0;
    if (clipped.Right >= sb.size.X)
        clipped.Right = static_cast<SHORT>(sb.size.X - 1);
    if (clipped.Bottom >= sb.size.Y)
        clipped.Bottom = static_cast<SHORT>(sb.size.Y - 1);
    if (clipped.Left > clipped.Right || clipped.Top > clipped.Bottom)
    {
        cr.Left = cr.Right = cr.Top = cr.Bottom = 0;
        ucomplete_sz(msg, sizeof(CONSOLE_WRITECONSOLEOUTPUT_MSG));
        return true;
    }

    for (SHORT y = clipped.Top; y <= clipped.Bottom; ++y)
    {
        const auto row_offset = static_cast<size_t>(y - orig.Top) * static_cast<size_t>(w);
        const auto col_offset = static_cast<size_t>(clipped.Left - orig.Left);
        sb.row_from_ci(y, data + row_offset + col_offset, static_cast<uint16_t>(clipped.Right - clipped.Left + 1),
                       static_cast<uint16_t>(clipped.Left));
    }
    cr = clipped;

    LOG("[api_write_console_output] region=(%d,%d)-(%d,%d)", clipped.Left, clipped.Top, clipped.Right, clipped.Bottom);
    if (!viewport_covers_screen_buffer(sb))
    {
        render_visible_viewport(state, sb, bridge);
        ucomplete_sz(msg, sizeof(CONSOLE_WRITECONSOLEOUTPUT_MSG));
        return true;
    }
    // WriteConsoleOutput 不移动应用光标。终端侧保存光标后按格子重绘，再恢复。
    vt_message m{};
    bridge.vt_msg_send<vt_message_id::save_cursor>(m);

    for (SHORT y = clipped.Top; y <= clipped.Bottom; ++y)
    {
        m = vt_message{};
        m.payload.position.row = static_cast<short>(y + 1);
        m.payload.position.col = static_cast<short>(clipped.Left + 1);
        bridge.vt_msg_send<vt_message_id::cursor_position>(m);

        for (SHORT x = clipped.Left; x <= clipped.Right; ++x)
        {
            // 当前实现逐格写 SGR+text，保证每个 CHAR_INFO 属性都能反映到终端；
            // 后续可在这里合并连续相同属性的 run。
            m = vt_message{};
            set_sgr_from_win32_attr(m, sb.attr_at({x, y}));
            bridge.vt_msg_send<vt_message_id::sgr>(m);

            char32_t ch = sb.at_u32({x, y});
            m = vt_message{};
            m.payload.text = std::u32string_view{&ch, 1};
            bridge.vt_msg_send<vt_message_id::text>(m);
        }
    }

    m = vt_message{};
    bridge.vt_msg_send<vt_message_id::restore_cursor>(m);
    bridge.vt_flush();
    bridge.sync_cursor_after_write(state.cursor.position);
    LOG("[api_write_console_output] done: tc_synced=(%d,%d)", state.cursor.position.X, state.cursor.position.Y);

    ucomplete_sz(msg, sizeof(CONSOLE_WRITECONSOLEOUTPUT_MSG));
    return true;
}

inline bool api_write_output_string(miniio::io_msg &msg, console_state &state, screen_buffer &sb, input_buffer &,
                                    pipe_bridge &bridge)
{
    if (msg.descriptor.InputSize < sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_WRITECONSOLEOUTPUTSTRING_MSG))
    {
        miniio::prepare_completion(msg, status_invalid_parameter);
        return true;
    }

    auto *r = reinterpret_cast<CONSOLE_WRITECONSOLEOUTPUTSTRING_MSG *>(msg.body + sizeof(CONSOLE_MSG_HEADER));
    r->NumRecords = 0;
    if (r->StringType != CONSOLE_ASCII && r->StringType != CONSOLE_REAL_UNICODE &&
        r->StringType != CONSOLE_FALSE_UNICODE && r->StringType != CONSOLE_ATTRIBUTE)
    {
        miniio::prepare_completion(msg, status_invalid_parameter);
        return true;
    }

    auto ib = msg.descriptor.InputSize - sizeof(CONSOLE_MSG_HEADER) - sizeof(CONSOLE_WRITECONSOLEOUTPUTSTRING_MSG);
    if (r->StringType == CONSOLE_ATTRIBUTE)
    {
        // WriteConsoleOutputAttribute 只更新本地属性模型；没有字符输出可发给终端。
        auto *attrs = reinterpret_cast<const WORD *>(msg.body + sizeof(CONSOLE_MSG_HEADER) +
                                                     sizeof(CONSOLE_WRITECONSOLEOUTPUTSTRING_MSG));
        r->NumRecords = static_cast<ULONG>(sb.write_attr_seq_linear(r->WriteCoord, attrs, ib / sizeof(WORD)));
        if (!viewport_covers_screen_buffer(sb))
        {
            render_visible_viewport(state, sb, bridge);
            ucomplete_sz(msg, sizeof(CONSOLE_WRITECONSOLEOUTPUTSTRING_MSG));
            return true;
        }
    }
    else
    {
        auto &u32text = bridge.conv_u32();
        if (r->StringType == CONSOLE_ASCII)
        {
            auto *in_a = reinterpret_cast<const char *>(msg.body + sizeof(CONSOLE_MSG_HEADER) +
                                                        sizeof(CONSOLE_WRITECONSOLEOUTPUTSTRING_MSG));
            convert_ansi_to_u32(in_a, ib, state.output_code_page ? state.output_code_page : CP_ACP, u32text,
                                bridge.conv_wstr());
        }
        else
        {
            auto *in_w = reinterpret_cast<const wchar_t *>(msg.body + sizeof(CONSOLE_MSG_HEADER) +
                                                           sizeof(CONSOLE_WRITECONSOLEOUTPUTSTRING_MSG));
            auto wlen = ib / sizeof(wchar_t);
            convert_utf16_to_u32(std::wstring_view{in_w, wlen}, u32text);
        }
        const auto written_u32 = sb.write_char32_linear(r->WriteCoord, u32text.data(), u32text.size());
        const auto written_text = std::u32string_view{u32text.data(), written_u32};
        if (r->StringType == CONSOLE_ASCII)
        {
            r->NumRecords = static_cast<ULONG>(
                u32_to_ansi_exact_len(written_text, state.output_code_page ? state.output_code_page : CP_ACP,
                                      bridge.conv_wstr()));
        }
        else
        {
            r->NumRecords = static_cast<ULONG>(u32_to_wide_exact_len(written_text));
        }

        if (!viewport_covers_screen_buffer(sb))
        {
            render_visible_viewport(state, sb, bridge);
            ucomplete_sz(msg, sizeof(CONSOLE_WRITECONSOLEOUTPUTSTRING_MSG));
            return true;
        }

        if (r->NumRecords > 0)
        {
            // 终端也要保持光标不动，因此使用 save/CUP/write/restore。
            vt_message m{};
            bridge.vt_msg_send<vt_message_id::save_cursor>(m);

            m = vt_message{};
            m.payload.position.row = static_cast<short>(r->WriteCoord.Y + 1);
            m.payload.position.col = static_cast<short>(r->WriteCoord.X + 1);
            bridge.vt_msg_send<vt_message_id::cursor_position>(m);

            m = vt_message{};
            set_sgr_from_win32_attr(m, state.default_attributes);
            bridge.vt_msg_send<vt_message_id::sgr>(m);

            m = vt_message{};
            m.payload.text = std::u32string_view{u32text.data(), written_u32};
            bridge.vt_msg_send<vt_message_id::text>(m);

            m = vt_message{};
            bridge.vt_msg_send<vt_message_id::restore_cursor>(m);
            bridge.vt_flush();
        }
    }

    ucomplete_sz(msg, sizeof(CONSOLE_WRITECONSOLEOUTPUTSTRING_MSG));
    return true;
}

inline bool api_read_console_output(miniio::io_msg &msg, console_state &state, screen_buffer &sb, input_buffer &,
                                    pipe_bridge &)
{
    if (msg.descriptor.InputSize < sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_READCONSOLEOUTPUT_MSG))
    {
        miniio::prepare_completion(msg, status_invalid_parameter);
        return true;
    }

    auto *r = reinterpret_cast<CONSOLE_READCONSOLEOUTPUT_MSG *>(msg.body + sizeof(CONSOLE_MSG_HEADER));
    SMALL_RECT &cr = r->CharRegion;
    LOG("[api_read_console_output] region=(%d,%d)-(%d,%d)", cr.Left, cr.Top, cr.Right, cr.Bottom);
    if (cr.Left > cr.Right || cr.Top > cr.Bottom)
    {
        cr.Left = cr.Right = cr.Top = cr.Bottom = 0;
        ucomplete_sz(msg, sizeof(CONSOLE_READCONSOLEOUTPUT_MSG));
        return true;
    }
    auto data_capacity = msg.descriptor.OutputSize > sizeof(CONSOLE_READCONSOLEOUTPUT_MSG)
                             ? msg.descriptor.OutputSize - sizeof(CONSOLE_READCONSOLEOUTPUT_MSG)
                             : 0;
    SMALL_RECT orig = cr;
    SHORT w = cr.Right - cr.Left + 1;
    auto h = cr.Bottom - cr.Top + 1;
    auto required = static_cast<size_t>(w) * static_cast<size_t>(h) * sizeof(CHAR_INFO);
    if (required > data_capacity)
    {
        cr.Left = cr.Right = cr.Top = cr.Bottom = 0;
        miniio::prepare_completion(msg, status_invalid_parameter);
        return true;
    }

    SMALL_RECT clipped = orig;
    if (clipped.Left < 0)
        clipped.Left = 0;
    if (clipped.Top < 0)
        clipped.Top = 0;
    if (clipped.Right >= sb.size.X)
        clipped.Right = static_cast<SHORT>(sb.size.X - 1);
    if (clipped.Bottom >= sb.size.Y)
        clipped.Bottom = static_cast<SHORT>(sb.size.Y - 1);
    if (clipped.Left > clipped.Right || clipped.Top > clipped.Bottom)
    {
        cr.Left = cr.Right = cr.Top = cr.Bottom = 0;
        ucomplete_sz(msg, static_cast<ULONG>(sizeof(CONSOLE_READCONSOLEOUTPUT_MSG) + data_capacity));
        return true;
    }

    for (SHORT y = clipped.Top; y <= clipped.Bottom; ++y)
    {
        // row_to_ci 导出整行后只复制请求区间；这样保留 row 层对宽字符和属性
        // 的 CHAR_INFO 降级逻辑。
        std::vector<CHAR_INFO> tmp(static_cast<size_t>(sb.size.X));
        sb.row_to_ci(y, tmp.data());
        auto *dst = reinterpret_cast<CHAR_INFO *>(msg.body + sizeof(CONSOLE_MSG_HEADER) +
                                                  sizeof(CONSOLE_READCONSOLEOUTPUT_MSG)) +
                    (y - orig.Top) * w + (clipped.Left - orig.Left);
        if (r->Unicode)
        {
            std::memcpy(dst, tmp.data() + clipped.Left,
                        static_cast<size_t>(clipped.Right - clipped.Left + 1) * sizeof(CHAR_INFO));
        }
        else
        {
            const auto cp = state.output_code_page ? state.output_code_page : CP_ACP;
            for (SHORT x = clipped.Left; x <= clipped.Right; ++x)
            {
                auto &src = tmp[static_cast<size_t>(x)];
                char mb = ' ';
                ::WideCharToMultiByte(cp, 0, &src.Char.UnicodeChar, 1, &mb, 1, nullptr, nullptr);
                auto &out = dst[x - clipped.Left];
                out.Char.AsciiChar = mb;
                out.Attributes = src.Attributes;
            }
        }
    }
    cr = clipped;
    ucomplete_sz(msg, static_cast<ULONG>(sizeof(CONSOLE_READCONSOLEOUTPUT_MSG) + data_capacity));
    return true;
}

// ── GetTitle / SetTitle (char32_t ↔ wchar_t 边界) ──

inline bool api_get_title(miniio::io_msg &msg, console_state &state, screen_buffer &, input_buffer &, pipe_bridge &bridge)
{
    if (msg.descriptor.InputSize < sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_GETTITLE_MSG))
    {
        miniio::prepare_completion(msg, status_invalid_parameter);
        return true;
    }

    auto *r = reinterpret_cast<CONSOLE_GETTITLE_MSG *>(msg.body + sizeof(CONSOLE_MSG_HEADER));
    auto &src = r->Original ? state.original_title : state.title;
    const auto data_capacity = msg.descriptor.OutputSize > sizeof(CONSOLE_GETTITLE_MSG)
                                   ? msg.descriptor.OutputSize - sizeof(CONSOLE_GETTITLE_MSG)
                                   : 0;

    if (r->Unicode)
    {
        // TitleLength 返回不含结尾 NUL 的字节数；completion size 包含写入的 NUL。
        auto *out = reinterpret_cast<wchar_t *>(msg.body + sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_GETTITLE_MSG));
        auto maxc = std::min<size_t>(data_capacity,
                                     sizeof(msg.body) - sizeof(CONSOLE_MSG_HEADER) - sizeof(CONSOLE_GETTITLE_MSG)) /
                    sizeof(wchar_t);
        auto cp = convert_u32_to_wide_raw(src, out, maxc);
        if (cp < maxc)
            out[cp] = L'\0';
        r->TitleLength = static_cast<ULONG>(u32_to_wide_exact_len(src) * sizeof(wchar_t));
        const auto written = cp * sizeof(wchar_t) + (cp < maxc ? sizeof(wchar_t) : 0);
        ucomplete_sz(msg, static_cast<ULONG>(sizeof(CONSOLE_GETTITLE_MSG) + written));
    }
    else
    {
        // ANSI title 使用系统 ACP，和传统控制台标题 API 保持一致。
        auto *out = reinterpret_cast<char *>(msg.body + sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_GETTITLE_MSG));
        auto maxb = std::min<size_t>(data_capacity,
                                     sizeof(msg.body) - sizeof(CONSOLE_MSG_HEADER) - sizeof(CONSOLE_GETTITLE_MSG));
        const auto cp = state.input_code_page ? state.input_code_page : CP_ACP;
        const auto encoded_size = u32_to_ansi_exact_len(src, cp, bridge.conv_wstr());
        size_t written = 0;
        if (maxb >= encoded_size)
        {
            written = convert_u32_to_ansi_raw(src, cp, out, maxb, bridge.conv_wstr());
            if (written < maxb)
                out[written++] = '\0';
            r->TitleLength = static_cast<ULONG>(encoded_size);
        }
        else
        {
            if (maxb > 0)
            {
                out[0] = '\0';
                written = 1;
            }
            r->TitleLength = 0;
        }
        ucomplete_sz(msg, static_cast<ULONG>(sizeof(CONSOLE_GETTITLE_MSG) + written));
    }
    return true;
}

inline bool api_set_title(miniio::io_msg &msg, console_state &state, screen_buffer &, input_buffer &,
                          pipe_bridge &bridge)
{
    if (msg.descriptor.InputSize < sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_SETTITLE_MSG))
    {
        miniio::prepare_completion(msg, status_invalid_parameter);
        return true;
    }

    auto *req = reinterpret_cast<CONSOLE_SETTITLE_MSG *>(msg.body + sizeof(CONSOLE_MSG_HEADER));
    auto *input = msg.body + sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_SETTITLE_MSG);
    auto ib = msg.descriptor.InputSize - sizeof(CONSOLE_MSG_HEADER) - sizeof(CONSOLE_SETTITLE_MSG);
    std::u32string u32title;
    if (req->Unicode)
    {
        auto *in = reinterpret_cast<const wchar_t *>(input);
        auto ic = ib / sizeof(wchar_t);
        convert_utf16_to_u32(std::wstring_view{in, ic}, u32title);
    }
    else
    {
        convert_ansi_to_u32(reinterpret_cast<const char *>(input), ib,
                            state.input_code_page ? state.input_code_page : CP_ACP, u32title, bridge.conv_wstr());
    }
    if (state.title.empty())
        state.original_title = u32title;
    state.title = std::move(u32title);

    // 状态更新后立即用 OSC 0 同步宿主终端标题。
    vt_message m{};
    m.payload.title = state.title;
    bridge.vt_msg_send<vt_message_id::set_window_title>(m);
    bridge.vt_flush();

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
inline bool api_l3_get_mouse_info(miniio::io_msg &msg, console_state &state, screen_buffer &, input_buffer &,
                                  pipe_bridge &)
{
    if (msg.descriptor.InputSize < sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_GETMOUSEINFO_MSG))
    {
        miniio::prepare_completion(msg, status_invalid_parameter);
        return true;
    }

    auto *r = reinterpret_cast<CONSOLE_GETMOUSEINFO_MSG *>(msg.body + sizeof(CONSOLE_MSG_HEADER));
    r->NumButtons =
        state.mouse_buttons != 0 ? state.mouse_buttons : static_cast<ULONG>(::GetSystemMetrics(SM_CMOUSEBUTTONS));
    ucomplete_sz(msg, sizeof(CONSOLE_GETMOUSEINFO_MSG));
    return true;
}

// ── 0x03 GetFontSize ──
inline bool api_l3_get_font_size(miniio::io_msg &msg, console_state &state, screen_buffer &, input_buffer &,
                                 pipe_bridge &)
{
    if (msg.descriptor.InputSize < sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_GETFONTSIZE_MSG))
    {
        miniio::prepare_completion(msg, status_invalid_parameter);
        return true;
    }

    auto *r = reinterpret_cast<CONSOLE_GETFONTSIZE_MSG *>(msg.body + sizeof(CONSOLE_MSG_HEADER));
    if (r->FontIndex != 0)
    {
        r->FontSize = {};
        miniio::prepare_completion(msg, status_invalid_parameter);
        return true;
    }
    r->FontSize = state.font_size; // 简化: 返回当前字体尺寸 (所有 index 相同)
    ucomplete_sz(msg, sizeof(CONSOLE_GETFONTSIZE_MSG));
    return true;
}

// ── 0x04 GetCurrentFont ──
inline bool api_l3_get_current_font(miniio::io_msg &msg, console_state &state, screen_buffer &, input_buffer &,
                                    pipe_bridge &)
{
    if (msg.descriptor.InputSize < sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_CURRENTFONT_MSG))
    {
        miniio::prepare_completion(msg, status_invalid_parameter);
        return true;
    }

    auto *r = reinterpret_cast<CONSOLE_CURRENTFONT_MSG *>(msg.body + sizeof(CONSOLE_MSG_HEADER));
    r->FontIndex = state.font_index;
    r->FontSize = r->MaximumWindow ? state.max_window_size : state.font_size;
    r->FontFamily = state.font_family;
    r->FontWeight = state.font_weight;
    std::memcpy(r->FaceName, state.face_name, sizeof(state.face_name));
    ucomplete_sz(msg, sizeof(CONSOLE_CURRENTFONT_MSG));
    return true;
}

// ── 0x0D SetDisplayMode ──
inline bool api_l3_set_display_mode(miniio::io_msg &msg, console_state &state, screen_buffer &sb, input_buffer &,
                                    pipe_bridge &)
{
    if (msg.descriptor.InputSize < sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_SETDISPLAYMODE_MSG))
    {
        miniio::prepare_completion(msg, status_invalid_parameter);
        return true;
    }

    auto *r = reinterpret_cast<CONSOLE_SETDISPLAYMODE_MSG *>(msg.body + sizeof(CONSOLE_MSG_HEADER));
    if ((r->dwFlags & (CONSOLE_FULLSCREEN_MODE | CONSOLE_WINDOWED_MODE)) == 0)
    {
        miniio::prepare_completion(msg, status_invalid_parameter);
        return true;
    }
    state.display_mode = (r->dwFlags & CONSOLE_FULLSCREEN_MODE) != 0 ? CONSOLE_FULLSCREEN_MODE : 0;
    r->ScreenBufferDimensions = state.screen_buffer_size;
    (void)sb;
    ucomplete_sz(msg, sizeof(CONSOLE_SETDISPLAYMODE_MSG));
    return true;
}

// ── 0x11 GetDisplayMode ──
inline bool api_l3_get_display_mode(miniio::io_msg &msg, console_state &state, screen_buffer &, input_buffer &,
                                    pipe_bridge &)
{
    if (msg.descriptor.InputSize < sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_GETDISPLAYMODE_MSG))
    {
        miniio::prepare_completion(msg, status_invalid_parameter);
        return true;
    }

    auto *r = reinterpret_cast<CONSOLE_GETDISPLAYMODE_MSG *>(msg.body + sizeof(CONSOLE_MSG_HEADER));
    r->ModeFlags = state.display_mode;
    ucomplete_sz(msg, sizeof(CONSOLE_GETDISPLAYMODE_MSG));
    return true;
}

// ── 0x12 AddAlias ──
inline bool api_l3_add_alias(miniio::io_msg &msg, console_state &state, screen_buffer &, input_buffer &, pipe_bridge &)
{
    if (msg.descriptor.InputSize < sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_ADDALIAS_MSG))
    {
        miniio::prepare_completion(msg, status_invalid_parameter);
        return true;
    }

    auto *r = reinterpret_cast<CONSOLE_ADDALIAS_MSG *>(msg.body + sizeof(CONSOLE_MSG_HEADER));
    auto *db = msg.body + sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_ADDALIAS_MSG);

    // 消息格式为 Exe + Source + Target，长度字段都是字节数。
    auto exe_len_bytes = static_cast<size_t>(r->ExeLength);
    auto src_len_bytes = static_cast<size_t>(r->SourceLength);
    auto tgt_len_bytes = static_cast<size_t>(r->TargetLength);
    if (exe_len_bytes + src_len_bytes + tgt_len_bytes > message_input_tail_capacity(msg, sizeof(CONSOLE_ADDALIAS_MSG)))
    {
        miniio::prepare_completion(msg, status_invalid_parameter);
        return true;
    }

    if (src_len_bytes == 0)
    {
        miniio::prepare_completion(msg, status_invalid_parameter);
        return true;
    }

    if (r->Unicode)
    {
        if ((exe_len_bytes | src_len_bytes | tgt_len_bytes) % sizeof(wchar_t) != 0)
        {
            miniio::prepare_completion(msg, status_invalid_parameter);
            return true;
        }
        auto *exe = reinterpret_cast<const wchar_t *>(db);
        auto *src = reinterpret_cast<const wchar_t *>(db + exe_len_bytes);
        auto *tgt = reinterpret_cast<const wchar_t *>(db + exe_len_bytes + src_len_bytes);
        auto exe_chars = exe_len_bytes / sizeof(wchar_t);
        auto src_chars = src_len_bytes / sizeof(wchar_t);
        auto tgt_chars = tgt_len_bytes / sizeof(wchar_t);

        auto exe_key = lower_wstring(std::wstring_view{exe, exe_chars});
        auto src_key = lower_wstring(std::wstring_view{src, src_chars});
        if (tgt_chars == 0)
        {
            state.aliases.erase(src_key);
            if (auto exe_it = state.aliases_by_exe.find(exe_key); exe_it != state.aliases_by_exe.end())
                exe_it->second.erase(src_key);
        }
        else
        {
            std::wstring target{tgt, tgt_chars};
            state.aliases[src_key] = target;
            state.aliases_by_exe[std::move(exe_key)][std::move(src_key)] = std::move(target);
        }
    }
    else
    {
        // 原版 A 路径使用控制台输入代码页，而不是进程 ACP。
        auto *exe_a = reinterpret_cast<const char *>(db);
        auto *src_a = reinterpret_cast<const char *>(db + exe_len_bytes);
        auto *tgt_a = reinterpret_cast<const char *>(db + exe_len_bytes + src_len_bytes);
        std::wstring wexe, wsrc, wtgt;
        convert_ansi_to_wstr(exe_a, exe_len_bytes, state.input_code_page, wexe);
        convert_ansi_to_wstr(src_a, src_len_bytes, state.input_code_page, wsrc);
        convert_ansi_to_wstr(tgt_a, tgt_len_bytes, state.input_code_page, wtgt);
        if (wsrc.empty())
        {
            miniio::prepare_completion(msg, status_invalid_parameter);
            return true;
        }
        auto exe_key = lower_wstring(wexe);
        auto src_key = lower_wstring(wsrc);
        if (wtgt.empty())
        {
            state.aliases.erase(src_key);
            if (auto exe_it = state.aliases_by_exe.find(exe_key); exe_it != state.aliases_by_exe.end())
                exe_it->second.erase(src_key);
        }
        else
        {
            state.aliases[src_key] = wtgt;
            state.aliases_by_exe[std::move(exe_key)][std::move(src_key)] = std::move(wtgt);
        }
    }

    ucomplete(msg);
    return true;
}

inline void alias_ansi_to_wstring(const char *text, size_t bytes, UINT code_page, std::wstring &out)
{
    out.clear();
    if (bytes == 0)
        return;

    const UINT cp = code_page ? code_page : CP_ACP;
    const int chars = ::MultiByteToWideChar(cp, 0, text, static_cast<int>(bytes), nullptr, 0);
    assert(chars >= 0);
    if (chars == 0)
        return;

    out.resize(static_cast<size_t>(chars));
    const int written = ::MultiByteToWideChar(cp, 0, text, static_cast<int>(bytes), out.data(), chars);
    assert(written == chars);
}

[[nodiscard]] inline size_t alias_wstring_to_ansi_length(std::wstring_view text, UINT code_page) noexcept
{
    if (text.empty())
        return 0;

    const UINT cp = code_page ? code_page : CP_ACP;
    const int bytes =
        ::WideCharToMultiByte(cp, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    assert(bytes >= 0);
    return static_cast<size_t>(bytes);
}

inline size_t alias_wstring_to_ansi(std::wstring_view text, UINT code_page, char *out, size_t out_cap) noexcept
{
    if (text.empty())
        return 0;

    const auto needed = alias_wstring_to_ansi_length(text, code_page);
    assert(needed <= out_cap);
    if (needed == 0)
        return 0;

    const UINT cp = code_page ? code_page : CP_ACP;
    const int written = ::WideCharToMultiByte(cp, 0, text.data(), static_cast<int>(text.size()), out,
                                              static_cast<int>(out_cap), nullptr, nullptr);
    assert(written == static_cast<int>(needed));
    return needed;
}

// ── 0x13 GetAlias ──
inline bool api_l3_get_alias(miniio::io_msg &msg, console_state &state, screen_buffer &, input_buffer &, pipe_bridge &)
{
    if (msg.descriptor.InputSize < sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_GETALIAS_MSG))
    {
        miniio::prepare_completion(msg, status_invalid_parameter);
        return true;
    }

    auto *r = reinterpret_cast<CONSOLE_GETALIAS_MSG *>(msg.body + sizeof(CONSOLE_MSG_HEADER));
    auto *db = msg.body + sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_GETALIAS_MSG);

    // 返回值写回 Source 所在位置；调用方提供的 Exe 前缀区域不被覆盖。
    auto exe_len_bytes = static_cast<size_t>(r->ExeLength);
    auto src_len_bytes = static_cast<size_t>(r->SourceLength);
    if (exe_len_bytes + src_len_bytes > message_input_tail_capacity(msg, sizeof(CONSOLE_GETALIAS_MSG)))
    {
        r->TargetLength = 0;
        miniio::prepare_completion(msg, status_invalid_parameter);
        return true;
    }

    if (r->Unicode)
    {
        if ((exe_len_bytes | src_len_bytes) % sizeof(wchar_t) != 0)
        {
            r->TargetLength = 0;
            miniio::prepare_completion(msg, status_invalid_parameter);
            return true;
        }
        auto *exe = reinterpret_cast<const wchar_t *>(db);
        auto *src = reinterpret_cast<const wchar_t *>(db + exe_len_bytes);
        auto exe_chars = exe_len_bytes / sizeof(wchar_t);
        auto src_chars = src_len_bytes / sizeof(wchar_t);

        if (src_chars > 0)
        {
            if (auto *wval =
                    find_alias_value(state, std::wstring_view{exe, exe_chars}, std::wstring_view{src, src_chars}))
            {
                auto *tgt_out = reinterpret_cast<wchar_t *>(db + exe_len_bytes);
                auto available_bytes = message_output_tail_capacity(msg, sizeof(CONSOLE_GETALIAS_MSG));
                auto available_chars =
                    available_bytes > exe_len_bytes ? (available_bytes - exe_len_bytes) / sizeof(wchar_t) : 0;
                if (wval->size() + 1 <= available_chars)
                {
                    std::memcpy(tgt_out, wval->data(), wval->size() * sizeof(wchar_t));
                    tgt_out[wval->size()] = L'\0';
                    r->TargetLength = static_cast<USHORT>((wval->size() + 1) * sizeof(wchar_t));
                    ucomplete_sz(msg, sizeof(CONSOLE_GETALIAS_MSG) + r->TargetLength);
                }
                else
                {
                    r->TargetLength = static_cast<USHORT>(available_chars * sizeof(wchar_t));
                    ucomplete_status_sz(msg, status_buffer_too_small, sizeof(CONSOLE_GETALIAS_MSG));
                }
                return true;
            }
        }
    }
    else
    {
        auto *exe_a = reinterpret_cast<const char *>(db);
        auto *src_a = reinterpret_cast<const char *>(db + exe_len_bytes);
        if (src_len_bytes > 0)
        {
            std::wstring exe_key;
            std::wstring key;
            alias_ansi_to_wstring(exe_a, exe_len_bytes, state.input_code_page, exe_key);
            alias_ansi_to_wstring(src_a, src_len_bytes, state.input_code_page, key);
            if (!key.empty())
            {
                if (auto *wval = find_alias_value(state, exe_key, key))
                {
                    auto *tgt_out = reinterpret_cast<char *>(db + exe_len_bytes);
                    auto available_bytes = message_output_tail_capacity(msg, sizeof(CONSOLE_GETALIAS_MSG));
                    available_bytes = available_bytes > exe_len_bytes ? available_bytes - exe_len_bytes : 0;
                    auto needed =
                        alias_wstring_to_ansi_length(std::wstring_view{wval->data(), wval->size()}, state.input_code_page) +
                        1;
                    if (needed <= available_bytes)
                    {
                        auto n = alias_wstring_to_ansi(std::wstring_view{wval->data(), wval->size()},
                                                       state.input_code_page, tgt_out, available_bytes);
                        tgt_out[n] = '\0';
                        r->TargetLength = static_cast<USHORT>(n + 1);
                        ucomplete_sz(msg, sizeof(CONSOLE_GETALIAS_MSG) + r->TargetLength);
                    }
                    else
                    {
                        r->TargetLength = static_cast<USHORT>(available_bytes);
                        ucomplete_status_sz(msg, status_buffer_too_small, sizeof(CONSOLE_GETALIAS_MSG));
                    }
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
inline bool api_l3_get_aliases_length(miniio::io_msg &msg, console_state &state, screen_buffer &, input_buffer &,
                                      pipe_bridge &)
{
    if (msg.descriptor.InputSize < sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_GETALIASESLENGTH_MSG))
    {
        miniio::prepare_completion(msg, status_invalid_parameter);
        return true;
    }

    auto *r = reinterpret_cast<CONSOLE_GETALIASESLENGTH_MSG *>(msg.body + sizeof(CONSOLE_MSG_HEADER));
    auto *db = msg.body + sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_GETALIASESLENGTH_MSG);
    auto exe_len_bytes = message_input_tail_capacity(msg, sizeof(CONSOLE_GETALIASESLENGTH_MSG));
    std::wstring exe_name;
    if (r->Unicode)
    {
        exe_name.assign(reinterpret_cast<const wchar_t *>(db), exe_len_bytes / sizeof(wchar_t));
    }
    else
    {
        alias_ansi_to_wstring(reinterpret_cast<const char *>(db), exe_len_bytes, state.input_code_page, exe_name);
    }

    const auto *aliases = &state.aliases;
    auto exe_key = lower_wstring(exe_name);
    if (auto exe_it = state.aliases_by_exe.find(exe_key); exe_it != state.aliases_by_exe.end())
        aliases = &exe_it->second;

    ULONG total = 0;
    if (r->Unicode)
    {
        // 原版导出格式是 source=target\0；AliasesLength 按字节返回。
        for (const auto &[k, v] : *aliases)
            total += static_cast<ULONG>((k.size() + 1 + v.size() + 1) * sizeof(wchar_t));
    }
    else
    {
        for (const auto &[k, v] : *aliases)
        {
            const auto k_len = alias_wstring_to_ansi_length(std::wstring_view{k.data(), k.size()},
                                                            state.input_code_page);
            const auto v_len = alias_wstring_to_ansi_length(std::wstring_view{v.data(), v.size()},
                                                            state.input_code_page);
            total += static_cast<ULONG>(k_len + 1 + v_len + 1);
        }
    }
    r->AliasesLength = total;
    ucomplete_sz(msg, sizeof(CONSOLE_GETALIASESLENGTH_MSG));
    return true;
}

// ── 0x15 GetAliasExesLength ──
inline bool api_l3_get_alias_exes_length(miniio::io_msg &msg, console_state &state, screen_buffer &, input_buffer &,
                                         pipe_bridge &)
{
    if (msg.descriptor.InputSize < sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_GETALIASEXESLENGTH_MSG))
    {
        miniio::prepare_completion(msg, status_invalid_parameter);
        return true;
    }

    auto *r = reinterpret_cast<CONSOLE_GETALIASEXESLENGTH_MSG *>(msg.body + sizeof(CONSOLE_MSG_HEADER));
    ULONG total = 0;
    for (const auto &[exe, _] : state.aliases_by_exe)
    {
        auto len = r->Unicode ? exe.size()
                              : wstr_to_ansi_len(std::wstring_view{exe.data(), exe.size()}, state.input_code_page);
        total += static_cast<ULONG>(len + 1) * (r->Unicode ? sizeof(wchar_t) : 1);
    }
    r->AliasExesLength = total;
    ucomplete_sz(msg, sizeof(CONSOLE_GETALIASEXESLENGTH_MSG));
    return true;
}

// ── 0x16 GetAliases ──
inline bool api_l3_get_aliases(miniio::io_msg &msg, console_state &state, screen_buffer &, input_buffer &,
                               pipe_bridge &)
{
    if (msg.descriptor.InputSize < sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_GETALIASES_MSG))
    {
        miniio::prepare_completion(msg, status_invalid_parameter);
        return true;
    }

    auto *r = reinterpret_cast<CONSOLE_GETALIASES_MSG *>(msg.body + sizeof(CONSOLE_MSG_HEADER));
    auto *db = msg.body + sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_GETALIASES_MSG);
    auto exe_len_bytes = message_input_tail_capacity(msg, sizeof(CONSOLE_GETALIASES_MSG));
    std::wstring exe_name;
    if (r->Unicode)
        exe_name.assign(reinterpret_cast<const wchar_t *>(db), exe_len_bytes / sizeof(wchar_t));
    else
        alias_ansi_to_wstring(reinterpret_cast<const char *>(db), exe_len_bytes, state.input_code_page, exe_name);

    const auto *aliases = &state.aliases;
    auto exe_key = lower_wstring(exe_name);
    if (auto exe_it = state.aliases_by_exe.find(exe_key); exe_it != state.aliases_by_exe.end())
        aliases = &exe_it->second;

    ULONG written = 0;
    if (r->Unicode)
    {
        // 输出是连续的 source=target\0 列表；空间不足时停止在上一条完整记录。
        auto *out = reinterpret_cast<wchar_t *>(msg.body + sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_GETALIASES_MSG));
        auto maxw = message_output_tail_capacity(msg, sizeof(CONSOLE_GETALIASES_MSG)) / sizeof(wchar_t);
        for (const auto &[k, v] : *aliases)
        {
            ULONG need = static_cast<ULONG>(k.size() + 1 + v.size() + 1);
            if (written + need > maxw)
                break;
            std::memcpy(out + written, k.data(), k.size() * sizeof(wchar_t));
            written += static_cast<ULONG>(k.size());
            out[written++] = L'=';
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
        auto maxb = message_output_tail_capacity(msg, sizeof(CONSOLE_GETALIASES_MSG));
        for (const auto &[k, v] : *aliases)
        {
            const auto k_len = alias_wstring_to_ansi_length(std::wstring_view{k.data(), k.size()},
                                                            state.input_code_page);
            const auto v_len = alias_wstring_to_ansi_length(std::wstring_view{v.data(), v.size()},
                                                            state.input_code_page);
            ULONG need = static_cast<ULONG>(k_len + 1 + v_len + 1);
            if (written + need > maxb)
                break;
            written += static_cast<ULONG>(
                alias_wstring_to_ansi(std::wstring_view{k.data(), k.size()}, state.input_code_page, out + written,
                                      maxb - written));
            out[written++] = '=';
            written += static_cast<ULONG>(
                alias_wstring_to_ansi(std::wstring_view{v.data(), v.size()}, state.input_code_page, out + written,
                                      maxb - written));
            out[written++] = '\0';
        }
        r->AliasesBufferLength = written;
        ucomplete_sz(msg, sizeof(CONSOLE_GETALIASES_MSG) + written);
    }
    return true;
}

// ── 0x17 GetAliasExes ──
inline bool api_l3_get_alias_exes(miniio::io_msg &msg, console_state &state, screen_buffer &, input_buffer &,
                                  pipe_bridge &)
{
    if (msg.descriptor.InputSize < sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_GETALIASEXES_MSG))
    {
        miniio::prepare_completion(msg, status_invalid_parameter);
        return true;
    }

    auto *r = reinterpret_cast<CONSOLE_GETALIASEXES_MSG *>(msg.body + sizeof(CONSOLE_MSG_HEADER));
    ULONG written = 0;
    if (r->Unicode)
    {
        auto *out =
            reinterpret_cast<wchar_t *>(msg.body + sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_GETALIASEXES_MSG));
        auto maxw = message_output_tail_capacity(msg, sizeof(CONSOLE_GETALIASEXES_MSG)) / sizeof(wchar_t);
        for (const auto &[exe, _] : state.aliases_by_exe)
        {
            auto need = exe.size() + 1;
            if (written + need > maxw)
                break;
            std::memcpy(out + written, exe.data(), exe.size() * sizeof(wchar_t));
            written += static_cast<ULONG>(exe.size());
            out[written++] = L'\0';
        }
        r->AliasExesBufferLength = written * sizeof(wchar_t);
        ucomplete_sz(msg, sizeof(CONSOLE_GETALIASEXES_MSG) + r->AliasExesBufferLength);
    }
    else
    {
        auto *out = reinterpret_cast<char *>(msg.body + sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_GETALIASEXES_MSG));
        auto maxb = message_output_tail_capacity(msg, sizeof(CONSOLE_GETALIASEXES_MSG));
        std::string converted;
        for (const auto &[exe, _] : state.aliases_by_exe)
        {
            convert_wstr_to_ansi(exe, state.input_code_page, converted);
            auto need = converted.size() + 1;
            if (written + need > maxb)
                break;
            std::memcpy(out + written, converted.data(), converted.size());
            written += static_cast<ULONG>(converted.size());
            out[written++] = '\0';
        }
        r->AliasExesBufferLength = written;
        ucomplete_sz(msg, sizeof(CONSOLE_GETALIASEXES_MSG) + written);
    }
    return true;
}

// ── 0x18 ExpungeCommandHistory ──
inline size_t command_history_buffer_length(pipe_bridge &bridge, bool unicode, UINT code_page)
{
    size_t total = 0;
    for (const auto &command : bridge.history_commands())
    {
        if (unicode)
            total += u32_to_wide_exact_len(command) + 1;
        else
            total += u32_to_ansi_exact_len(command, code_page, bridge.conv_wstr()) + 1;
    }
    return total * (unicode ? sizeof(wchar_t) : 1);
}

inline size_t write_command_history_buffer(pipe_bridge &bridge, bool unicode, UINT code_page, BYTE *out,
                                           size_t out_cap)
{
    size_t written = 0;
    for (const auto &command : bridge.history_commands())
    {
        if (unicode)
        {
            const auto wide_len = u32_to_wide_exact_len(command);
            const auto need = (wide_len + 1) * sizeof(wchar_t);
            if (written + need > out_cap)
                return written;
            auto *wide_out = reinterpret_cast<wchar_t *>(out + written);
            const auto chars = convert_u32_to_wide_raw(command, wide_out, wide_len);
            written += chars * sizeof(wchar_t);
            const wchar_t terminator = L'\0';
            std::memcpy(out + written, &terminator, sizeof(terminator));
            written += sizeof(wchar_t);
        }
        else
        {
            const auto ansi_len = u32_to_ansi_exact_len(command, code_page, bridge.conv_wstr());
            const auto need = ansi_len + 1;
            if (written + need > out_cap)
                return written;
            written += convert_u32_to_ansi_raw(command, code_page, reinterpret_cast<char *>(out + written), ansi_len,
                                               bridge.conv_wstr());
            out[written++] = '\0';
        }
    }
    return written;
}

inline bool api_l3_expunge_history(miniio::io_msg &msg, console_state &, screen_buffer &, input_buffer &,
                                   pipe_bridge &bridge)
{
    if (msg.descriptor.InputSize < sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_EXPUNGECOMMANDHISTORY_MSG))
    {
        miniio::prepare_completion(msg, status_invalid_parameter);
        return true;
    }

    bridge.api_clear_history();
    ucomplete(msg);
    return true;
}

// ── 0x19 SetNumberOfCommands ──
inline bool api_l3_set_num_commands(miniio::io_msg &msg, console_state &state, screen_buffer &, input_buffer &,
                                    pipe_bridge &bridge)
{
    if (msg.descriptor.InputSize < sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_SETNUMBEROFCOMMANDS_MSG))
    {
        miniio::prepare_completion(msg, status_invalid_parameter);
        return true;
    }

    auto *r = reinterpret_cast<CONSOLE_SETNUMBEROFCOMMANDS_MSG *>(msg.body + sizeof(CONSOLE_MSG_HEADER));
    state.history_buffer_size = r->NumCommands;
    bridge.api_set_history_capacity(r->NumCommands);
    ucomplete(msg);
    return true;
}

// ── 0x1A GetCommandHistoryLength ──
inline bool api_l3_get_history_length(miniio::io_msg &msg, console_state &state, screen_buffer &, input_buffer &,
                                      pipe_bridge &bridge)
{
    if (msg.descriptor.InputSize < sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_GETCOMMANDHISTORYLENGTH_MSG))
    {
        miniio::prepare_completion(msg, status_invalid_parameter);
        return true;
    }

    auto *r = reinterpret_cast<CONSOLE_GETCOMMANDHISTORYLENGTH_MSG *>(msg.body + sizeof(CONSOLE_MSG_HEADER));
    r->CommandHistoryLength =
        static_cast<ULONG>(command_history_buffer_length(bridge, r->Unicode != FALSE, state.input_code_page));
    ucomplete_sz(msg, sizeof(CONSOLE_GETCOMMANDHISTORYLENGTH_MSG));
    return true;
}

// ── 0x1B GetCommandHistory ──
inline bool api_l3_get_history(miniio::io_msg &msg, console_state &state, screen_buffer &, input_buffer &,
                               pipe_bridge &bridge)
{
    if (msg.descriptor.InputSize < sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_GETCOMMANDHISTORY_MSG))
    {
        miniio::prepare_completion(msg, status_invalid_parameter);
        return true;
    }

    auto *r = reinterpret_cast<CONSOLE_GETCOMMANDHISTORY_MSG *>(msg.body + sizeof(CONSOLE_MSG_HEADER));
    auto *out = msg.body + sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_GETCOMMANDHISTORY_MSG);
    auto cap = message_output_tail_capacity(msg, sizeof(CONSOLE_GETCOMMANDHISTORY_MSG));
    auto needed = command_history_buffer_length(bridge, r->Unicode != FALSE, state.input_code_page);
    auto written = write_command_history_buffer(bridge, r->Unicode != FALSE, state.input_code_page, out, cap);
    r->CommandBufferLength = static_cast<ULONG>(written);
    if (written < needed)
        ucomplete_status_sz(msg, status_buffer_too_small, sizeof(CONSOLE_GETCOMMANDHISTORY_MSG));
    else
        ucomplete_sz(msg, sizeof(CONSOLE_GETCOMMANDHISTORY_MSG) + static_cast<ULONG>(written));
    return true;
}

// ── 0x1F GetConsoleWindow ──
inline bool api_l3_get_console_window(miniio::io_msg &msg, console_state &, screen_buffer &, input_buffer &,
                                      pipe_bridge &)
{
    if (msg.descriptor.InputSize < sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_GETCONSOLEWINDOW_MSG))
    {
        miniio::prepare_completion(msg, status_invalid_parameter);
        return true;
    }

    auto *r = reinterpret_cast<CONSOLE_GETCONSOLEWINDOW_MSG *>(msg.body + sizeof(CONSOLE_MSG_HEADER));
    r->hwnd = ::GetConsoleWindow(); // 返回实际 HWND (可能 NULL)
    ucomplete_sz(msg, sizeof(CONSOLE_GETCONSOLEWINDOW_MSG));
    return true;
}

// ── 0x28 GetSelectionInfo ──
inline bool api_l3_get_selection_info(miniio::io_msg &msg, console_state &state, screen_buffer &, input_buffer &,
                                      pipe_bridge &)
{
    if (msg.descriptor.InputSize < sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_GETSELECTIONINFO_MSG))
    {
        miniio::prepare_completion(msg, status_invalid_parameter);
        return true;
    }

    auto *r = reinterpret_cast<CONSOLE_GETSELECTIONINFO_MSG *>(msg.body + sizeof(CONSOLE_MSG_HEADER));
    r->SelectionInfo = state.selection_info;
    ucomplete_sz(msg, sizeof(CONSOLE_GETSELECTIONINFO_MSG));
    return true;
}

// ── 0x29 GetConsoleProcessList ──
inline bool api_l3_get_process_list(miniio::io_msg &msg, console_state &, screen_buffer &, input_buffer &,
                                    pipe_bridge &bridge)
{
    if (msg.descriptor.InputSize < sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_GETCONSOLEPROCESSLIST_MSG))
    {
        miniio::prepare_completion(msg, status_invalid_parameter);
        return true;
    }

    auto *r = reinterpret_cast<CONSOLE_GETCONSOLEPROCESSLIST_MSG *>(msg.body + sizeof(CONSOLE_MSG_HEADER));
    auto *out =
        reinterpret_cast<DWORD *>(msg.body + sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_GETCONSOLEPROCESSLIST_MSG));
    auto maxc = message_output_tail_capacity(msg, sizeof(CONSOLE_GETCONSOLEPROCESSLIST_MSG)) / sizeof(DWORD);
    size_t count = bridge.copy_process_list_newest_first(out, maxc);
    r->dwProcessCount = static_cast<ULONG>(bridge.process_count());
    ucomplete_sz(msg, sizeof(CONSOLE_GETCONSOLEPROCESSLIST_MSG) + static_cast<ULONG>(count * sizeof(DWORD)));
    return true;
}

// ── 0x2A GetHistory ──
inline bool api_l3_get_history_info(miniio::io_msg &msg, console_state &state, screen_buffer &, input_buffer &,
                                    pipe_bridge &)
{
    if (msg.descriptor.InputSize < sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_HISTORY_MSG))
    {
        miniio::prepare_completion(msg, status_invalid_parameter);
        return true;
    }

    auto *r = reinterpret_cast<CONSOLE_HISTORY_MSG *>(msg.body + sizeof(CONSOLE_MSG_HEADER));
    r->HistoryBufferSize = static_cast<ULONG>(state.history_buffer_size);
    r->NumberOfHistoryBuffers = static_cast<ULONG>(state.history_num_buffers);
    r->dwFlags = state.history_flags;
    ucomplete_sz(msg, sizeof(CONSOLE_HISTORY_MSG));
    return true;
}

// ── 0x2B SetHistory ──
inline bool api_l3_set_history_info(miniio::io_msg &msg, console_state &state, screen_buffer &, input_buffer &,
                                    pipe_bridge &bridge)
{
    if (msg.descriptor.InputSize < sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_HISTORY_MSG))
    {
        miniio::prepare_completion(msg, status_invalid_parameter);
        return true;
    }

    auto *r = reinterpret_cast<CONSOLE_HISTORY_MSG *>(msg.body + sizeof(CONSOLE_MSG_HEADER));
    if (r->HistoryBufferSize > SHRT_MAX || r->NumberOfHistoryBuffers > SHRT_MAX ||
        (r->dwFlags & ~HISTORY_NO_DUP_FLAG) != 0)
    {
        miniio::prepare_completion(msg, status_invalid_parameter);
        return true;
    }
    state.history_buffer_size = r->HistoryBufferSize;
    state.history_num_buffers = r->NumberOfHistoryBuffers;
    state.history_flags = r->dwFlags;
    bridge.api_set_history_capacity(r->HistoryBufferSize);
    ucomplete_sz(msg, sizeof(CONSOLE_HISTORY_MSG));
    return true;
}

// ── 0x2C SetCurrentFont ──
inline bool api_l3_set_current_font(miniio::io_msg &msg, console_state &state, screen_buffer &, input_buffer &,
                                    pipe_bridge &)
{
    if (msg.descriptor.InputSize < sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_CURRENTFONT_MSG))
    {
        miniio::prepare_completion(msg, status_invalid_parameter);
        return true;
    }

    auto *r = reinterpret_cast<CONSOLE_CURRENTFONT_MSG *>(msg.body + sizeof(CONSOLE_MSG_HEADER));
    // 字体设置只影响后续查询；WT/宿主终端字体不能通过该 API 同步修改。
    state.font_index = r->FontIndex;
    state.font_size = r->FontSize;
    state.font_family = r->FontFamily;
    state.font_weight = r->FontWeight;
    std::memcpy(state.face_name, r->FaceName, sizeof(state.face_name));
    ucomplete_sz(msg, sizeof(CONSOLE_CURRENTFONT_MSG));
    return true;
}

// ── 第二类 L3: 废弃 API (对标 ServerDeprecatedApi) ──
inline bool api_l3_deprecated(miniio::io_msg &msg, console_state &, screen_buffer &, input_buffer &, pipe_bridge &)
{
    // 原版 ServerDeprecatedApi 返回 E_NOTIMPL，经 ApiSorter 转为
    // STATUS_NOT_IMPLEMENTED。
    miniio::prepare_completion(msg, status_not_implemented);
    return true;
}

} // namespace conpty
