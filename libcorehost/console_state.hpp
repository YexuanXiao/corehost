// ── conpty/console_state.hpp ──────────────────────
// Layer 4: 控制台状态。
//
// 功能分解：
// 1. 保存 Win32 Console API 可查询/可修改的模式、光标、窗口、字体、标题、
//    历史和别名状态。
// 2. 作为 VT 输出和 Console API 之间的共享状态源；screen_buffer 保存格子，
//    console_state 保存当前模式和游标类元数据。
// 3. DEC line drawing、tab stops、saved cursor 等 VT 状态放在这里，使
//    api_handlers 和 vt_msg_dispatch 看到同一份状态。
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
    // 1-100 是 Win32 控制台光标高度百分比；默认 25 与传统 conhost 一致。
    ULONG size = 25;

    // true 表示终端应显示光标；false 对应 SetConsoleCursorInfo 隐藏光标。
    bool visible = true;

    // 0-based 控制台坐标；必须保持在当前 screen_buffer_size 范围内。
    COORD position{0, 0};
};

struct console_state
{
    // ── 模式 ──
    // 取值为 ENABLE_* 标志位组合；默认同时适用于输入和输出兼容路径。
    DWORD input_mode = DEFAULT_CONSOLE_MODE;
    DWORD output_mode = DEFAULT_CONSOLE_MODE;

    // ── 代码页 ──
    // 构造函数中填充。0 只在构造尚未运行的零初始化状态下出现。
    UINT input_code_page = 0;
    UINT output_code_page = 0;

    // ── 光标 ──
    cursor_state cursor;

    // ── Screen Buffer 信息 ──
    COORD screen_buffer_size{default_console_size};
    COORD current_window_size{default_console_size};
    COORD max_window_size{default_console_size};
    // 0x07 是传统控制台白前景/黑背景属性。
    WORD default_attributes = 0x07;
    WORD popup_attributes = 0x07;

    // 16 项对应 Win32 控制台颜色表索引。
    COLORREF color_table[16]{};

    // ── 标题 (char32_t 内部存储) ──
    std::u32string title;
    // original_title 为空表示尚未捕获初始标题。
    std::u32string original_title;

    // ── 字体信息 ──
    // font_index 目前只有 0；font_size 是字符像素尺寸；font_weight 使用
    // LOGFONT 权重值，400 表示 normal。
    UINT font_index = 0;
    COORD font_size{8, 12};
    UINT font_family = 0;
    UINT font_weight = 400;

    // 固定 32 WCHAR 是 CONSOLE_FONT_INFOEX 的 FaceName 长度。
    WCHAR face_name[32] = L"Consolas";

    // ── 鼠标信息 ──
    // 0 表示尚未查询或没有鼠标按钮信息。
    ULONG mouse_buttons = 0;

    // ── 显示模式 ──
    // 0 表示未设置；否则为 CONSOLE_FULLSCREEN_MODE 或 CONSOLE_WINDOWED_MODE。
    ULONG display_mode = 0;

    // ── 选择信息 ──
    // 全零表示当前没有选择区域。
    CONSOLE_SELECTION_INFO selection_info{};

    // ── 语言 ID ──
    // 构造函数中填充系统默认 LANGID；0 只表示尚未初始化。
    LANGID lang_id = 0;

    // ── 命令历史 (char32_t) ──
    std::vector<std::u32string> command_history;
    // 默认值匹配传统控制台历史设置；0 仍是有效输入但表示禁用相应容量。
    size_t history_buffer_size = 50;
    size_t history_num_buffers = 4;
    DWORD history_flags = 0;

    // ── DOSKEY 别名 (wchar_t, ConDrv 边界零拷贝) ──
    // aliases 是当前行编辑器使用的扁平视图；aliases_by_exe 保留 Console API
    // 要求的 exe 分桶。AddAlias 同时维护两者，测试或旧路径直接填 aliases 时
    // GetAlias/GetAliases 仍会把它当作兼容回退。
    std::flat_map<std::wstring, std::wstring> aliases;
    std::flat_map<std::wstring, std::flat_map<std::wstring, std::wstring>> aliases_by_exe;

    // ── 文本测量模式 ──
    text_measurement_mode text_measurement = text_measurement_mode::console;
    // false 按西文终端处理 EAW=A；true 按 CJK 环境将其视为 2 列。
    bool ambiguous_is_wide = false;

    // ── PowerShell shim 状态 ──
    // true 表示已识别到即将由后续 API 完成的清屏序列。
    bool pending_clear_screen = false;

    // ── ConPTY 光标同步 ──
    // true 表示 state.cursor 已变更，下一次输出前需要把终端光标同步过去。
    bool cursor_position_dirty = false;

    // ── DECSC 保存的光标状态 ──
    struct saved_cursor
    {
        // has_state=false 时 position/attributes 的值无意义。
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
    // tab_stops 覆盖 0..511 列；超出范围的 tab 操作会被忽略。
    static constexpr SHORT tab_width = 8;
    bool tab_stops[512]{};

    void init_tab_stops()
    {
        // DEC 默认每 8 列一个 tab stop，包含第 0 列；后续 HTS/TBC 会在这张
        // 固定表上增删。
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
        // 返回第一个严格大于 col 的停靠位；找不到时返回原列，调用者据此保持
        // 光标不动而不是跳到行尾。
        for (SHORT c = col + 1; c < 512; ++c)
            if (tab_stops[c])
                return c;
        return col;
    }
    SHORT prev_tab_stop(SHORT col) const noexcept
    {
        // 返回第一个严格小于 col 的停靠位；找不到时回到 0，匹配 CBT 的左边界。
        for (SHORT c = col - 1; c >= 0; --c)
            if (tab_stops[c])
                return c;
        return 0;
    }

    // ── DEC 行绘制字符集 (ESC(0 → Unicode 框线字符) ──
    bool dec_line_drawing_mode = false;

    // DEC → Unicode 映射表 (返回 char32_t)
    char32_t dec_to_unicode(unsigned char ch) const noexcept
    {
        // ESC(0 只重映射 DEC special graphics 区间；未列出的字节保持 ASCII
        // 码点，避免普通文本被误转换。
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
        // 代码页和语言 ID 依赖当前系统环境，只能在运行时初始化；其余字段使用
        // 成员默认值即可保持可预测的控制台初始状态。
        input_code_page = ::GetACP();
        output_code_page = ::GetOEMCP();
        lang_id = ::GetSystemDefaultLangID();
        init_tab_stops();
    }
};

} // namespace conpty
