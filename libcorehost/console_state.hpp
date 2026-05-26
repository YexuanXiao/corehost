// ── conpty/console_state.hpp ──────────────────────
// Layer 4: 控制台状态 (char32_t 内部化版本)
//
// 与 conpty/console_state.hpp 的区别:
//   - title/original_title 使用 std::u32string 替代 wchar_t[256]
//   - command_history 使用 std::vector<std::u32string>
//   - aliases 使用 std::flat_map<std::wstring,std::wstring> (ConDrv 边界零拷贝)
//   - dec_to_unicode 返回 char32_t
//   - text_measurement 使用强类型枚举 text_measurement_mode
//   - face_name 保留 WCHAR[32] (Windows API 边界)
//
// ConDrv 边界处的 wchar_t 直接使用，无需转码。
#pragma once
#include <windows.h>
#include <string>
#include <vector>
#include <flat_map>
#include "default_console_size.hpp"
#include "text_measurement_mode.hpp"

namespace conpty
{

inline constexpr DWORD DEFAULT_CONSOLE_MODE = ENABLE_PROCESSED_INPUT | ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT |
                                              ENABLE_PROCESSED_OUTPUT | ENABLE_WRAP_AT_EOL_OUTPUT;

struct cursor_state
{
    ULONG size = 25; // 1-100 (百分比)
    bool visible = true;
    COORD position{0, 0};
};

struct console_state
{
    // ── 模式 ──
    DWORD input_mode = DEFAULT_CONSOLE_MODE;
    DWORD output_mode = DEFAULT_CONSOLE_MODE;

    // ── 代码页 ──
    UINT input_code_page = 0;  // GetACP()
    UINT output_code_page = 0; // GetOEMCP()

    // ── 光标 ──
    cursor_state cursor;

    // ── Screen Buffer 信息 ──
    COORD screen_buffer_size{default_console_size};
    COORD current_window_size{default_console_size};
    COORD max_window_size{default_console_size};
    WORD default_attributes = 0x07;
    WORD popup_attributes = 0x07;
    COLORREF color_table[16]{};

    // ── 标题 (char32_t 内部存储) ──
    std::u32string title;
    std::u32string original_title; // 对标 gci.GetOriginalTitle()

    // ── 字体信息 (对标 CONSOLE_FONT_INFO) ──
    // face_name 保留 WCHAR: Windows API 边界, 由 api_handlers 转换
    UINT font_index = 0;
    COORD font_size{8, 12};
    UINT font_family = 0;
    UINT font_weight = 400;
    WCHAR face_name[32] = L"Consolas";

    // ── 鼠标信息 ──
    ULONG mouse_buttons = 0; // GetNumberOfConsoleMouseButtons

    // ── 显示模式 ──
    ULONG display_mode = 0; // CONSOLE_FULLSCREEN / CONSOLE_WINDOWED

    // ── 选择信息 ──
    CONSOLE_SELECTION_INFO selection_info{}; // GetConsoleSelectionInfo

    // ── 语言 ID ──
    LANGID lang_id = 0;

    // ── 命令历史 (char32_t) ──
    std::vector<std::u32string> command_history;
    size_t history_buffer_size = 50;
    size_t history_num_buffers = 4;
    DWORD history_flags = 0;

    // ── DOSKEY 别名 (wchar_t, ConDrv 边界零拷贝) ──
    std::flat_map<std::wstring, std::wstring> aliases;

    // ── 文本测量模式 ──
    text_measurement_mode text_measurement = text_measurement_mode::console;
    bool ambiguous_is_wide = false; // --ambiguousIsWide: 模糊宽度字符(EAW=A)视为宽字符(2列)

    // ── PowerShell shim 状态 ──
    bool pending_clear_screen = false;

    // ── ConPTY 光标同步 ──
    bool cursor_position_dirty = false;

    // ── DECSC 保存的光标状态 ──
    struct saved_cursor
    {
        COORD position{0, 0};
        WORD attributes = 0x07;
        bool has_state = false;
    } decsc_cursor;

    // ── 标记光标可能已脏 ──
    void mark_cursor_dirty() noexcept
    {
        cursor_position_dirty = true;
    }

    // ── Tab 停靠位 ──
    static constexpr SHORT tab_width = 8;
    bool tab_stops[512]{};

    void init_tab_stops()
    {
        for (SHORT i = 0; i < 512; i += tab_width)
            tab_stops[i] = true;
    }
    void set_tab_stop(SHORT col)
    {
        if (col >= 0 && col < 512)
            tab_stops[col] = true;
    }
    void clear_tab_stop(SHORT col)
    {
        if (col >= 0 && col < 512)
            tab_stops[col] = false;
    }
    void clear_all_tab_stops()
    {
        for (auto &ts : tab_stops)
            ts = false;
    }
    SHORT next_tab_stop(SHORT col) const noexcept
    {
        for (SHORT c = col + 1; c < 512; ++c)
            if (tab_stops[c])
                return c;
        return col;
    }
    SHORT prev_tab_stop(SHORT col) const noexcept
    {
        for (SHORT c = col - 1; c >= 0; --c)
            if (tab_stops[c])
                return c;
        return 0;
    }

    // ── DEC 行绘制字符集 (ESC(0 → Unicode 框线字符) ──
    bool dec_line_drawing_mode = false;

    // DEC → Unicode 映射表 (返回 char32_t)
    static char32_t dec_to_unicode(unsigned char ch) noexcept
    {
        switch (ch)
        {
        case 0x6a:
            return U'\x2518'; // ┘
        case 0x6b:
            return U'\x2510'; // ┐
        case 0x6c:
            return U'\x250C'; // ┌
        case 0x6d:
            return U'\x2514'; // └
        case 0x6e:
            return U'\x253C'; // ┼
        case 0x6f:
            return U'\x23BA'; // ⎺
        case 0x70:
            return U'\x23BB'; // ⎻
        case 0x71:
            return U'\x2500'; // ─
        case 0x72:
            return U'\x23BC'; // ⎼
        case 0x73:
            return U'\x23BD'; // ⎽
        case 0x74:
            return U'\x251C'; // ├
        case 0x75:
            return U'\x2524'; // ┤
        case 0x76:
            return U'\x2534'; // ┴
        case 0x77:
            return U'\x252C'; // ┬
        case 0x78:
            return U'\x2502'; // │
        case 0x79:
            return U'\x2264'; // ≤
        case 0x7a:
            return U'\x2265'; // ≥
        case 0x7e:
            return U'\x00B7'; // ·
        default:
            return static_cast<char32_t>(ch);
        }
    }

    console_state()
    {
        input_code_page = ::GetACP();
        output_code_page = ::GetOEMCP();
        lang_id = ::GetSystemDefaultLangID();
        init_tab_stops();
    }
};

} // namespace conpty
