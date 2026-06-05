// ── conpty/vt_parser.hpp ──────────────────────────
// Layer 2: VT/CSI/OSC 序列解析器 (char32_t 输入, 零分配视图)
//
// 设计目标：
//   · 完全以 Unicode 码点 (char32_t) 作为输入，调用方负责 UTF-8 解码。
//   · 无法识别的序列以 unknown_sequence 返回，raw_sequence 指向完整原文；
//     对 OSC 这类可以安全降级的序列，msg.payload.text 只指向可显示部分。
//   · vt_message 中的 title/text 为 std::u32string_view。continue_text 指向
//     本次 parse 输入范围；text/unknown_sequence/set_window_title 指向内部
//     _raw，因为这些消息需要跨字符累计或保留完整控制序列原文。
//   · parse(range) 返回 vt_parse_result，continue_ 表示没有可消费消息，
//     continue_text/text 表示普通文本，unknown_sequence 表示未知/错误序列，
//     其余为具体控制序列 id。
//   · 提供 reset() 方法，在调用方消费 result 后统一清空当前消息和内部
//     raw 缓冲区，让下一次 parse(range) 从 ground 状态继续。
//
// 使用方式：
//   raw_u32_buffer raw;
//   vt_parser p{raw};
//   while (!code_points.empty()) {
//       vt_parse_result result = p.parse(code_points);
//       code_points.remove_prefix(result.consumed);
//       if (result.id != vt_message_id::continue_) {
//           switch (result.id) {
//               case vt_message_id::text:
//                   // 使用 result.message.payload.text (u32string_view)
//                   // p.reset();
//                   break;
//               case vt_message_id::unknown_sequence:
//                   // 使用 result.raw_sequence 诊断完整原文；payload.text 是可显示降级文本
//                   // p.reset();
//                   break;
//               case vt_message_id::set_window_title:
//                   // 使用 result.message.payload.title (u32string_view)
//                   // p.reset();
//                   break;
//               ...
//           }
//       }
//   }
//
#pragma once
#include <algorithm>
#include <ranges>
#include <cstdint>
#include <charconv>
#include <string>
#include <string_view>
#include <array>
#include <vector>
#include "raw_byte_allocator.hpp"

namespace corehost::conpty
{

// ── vt_message_id ────────────────────────────────────
// 所有可能的解析结果标识。
// continue_: 需要继续喂入字符，当前未产出完整消息。
// text:      普通文本。
// unknown_sequence: 无法识别或语法错误的控制序列。
enum class vt_message_id
{
    continue_text = 0, // 可打印字符 → echo + 插入行缓冲
    continue_ = 1,     // 转义内部状态 → 无操作
    text = 2,          // 纯可打印文本消息（不含控制字符）
    unknown_sequence,  // 无法识别或语法错误的 ESC/CSI/OSC/SS3 序列

    // 文本之外的 OSC/窗口元数据。title 会写入本地状态；OSC 8 只透传给
    // 宿主终端，corehost 不建模 hyperlink 属性。
    set_window_title,
    osc8_hyperlink,

    // C0/ESC 基础控制。输入方向用于行编辑和 RAW_READ 行终止判断；输出方向会
    // 更新本地 cursor/screen 并序列化为对应 VT。
    carriage_return, // \r
    line_feed,       // \n
    reverse_index,

    // 光标移动和光标状态。相对移动的 count payload 默认 1；绝对定位保持
    // VT 1-based 坐标，调用方进入 console_state 时再转为本地坐标。
    cursor_up,
    cursor_down,
    cursor_forward,
    cursor_backward,
    cursor_next_line,
    cursor_prev_line,
    cursor_forward_tab,
    cursor_backward_tab,
    cursor_vert_absolute,
    cursor_horiz_absolute,
    cursor_position,
    save_cursor,
    restore_cursor,
    ansi_save_cursor,
    ansi_restore_cursor,
    set_cursor_shape,
    cursor_enable_blinking,
    cursor_disable_blinking,
    cursor_show,
    cursor_hide,
    cursor_keys_app_mode,
    cursor_keys_normal_mode,

    // Tab、keypad 和 G0 charset 模式。它们影响终端输入/字符解释或本地 tab
    // stop，不属于屏幕内容本身。
    horizontal_tab_set,
    tab_clear_current,
    tab_clear_all,
    keypad_app_mode,
    keypad_numeric_mode,
    designate_charset_line_drawing,
    designate_charset_ascii,

    // 屏幕缓冲区编辑和终端缓冲区模式。这些消息通常同时影响宿主终端 VT
    // 输出和 corehost 的本地 Console API 可见状态。
    scroll_up,
    scroll_down,
    insert_characters,
    delete_characters,
    erase_characters,
    insert_lines,
    delete_lines,
    erase_in_display,
    erase_in_line,
    set_scrolling_region,
    use_alternate_buffer,
    use_main_buffer,
    set_columns_132,
    set_columns_80,
    resize_window, // \x1b[8;height;width t — terminal resize notification

    // 图形属性修改。payload.sgr 只保存本条消息显式设置/清除的属性，不是完整
    // 当前属性快照；OSC 4 调色板只透传给宿主终端，本地只保存 message payload。
    sgr,
    set_palette_color,

    // 终端命令、查询和响应。查询会注入响应或透传给终端；响应来自终端，不是
    // 用户输入。
    soft_reset,
    report_cursor_position,
    device_attributes,
    cpr_response, // \x1b[Pl;PcR — 终端对 DSR CPR 的应答

    // 键盘输入消息。输入方向会转换为 INPUT_RECORD 或 cooked 编辑动作；输出
    // 方向不应把这些 id 写给 vt_out。
    win32_input_key, // \x1b[Vk;Sc;Uc;Kd;Cs;Rc_ — Win32 Input Mode 键盘事件
    key_up,
    key_down,
    key_right,
    key_left,
    key_home,
    key_end,
    key_insert,
    key_delete,
    key_page_up,
    key_page_down,
    key_f1,
    key_f2,
    key_f3,
    key_f4,
    key_f5,
    key_f6,
    key_f7,
    key_f8,
    key_f9,
    key_f10,
    key_f11,
    key_f12,
    key_ctrl_up,
    key_ctrl_down,
    key_ctrl_right,
    key_ctrl_left,
    char_del,
    char_sub,
    char_esc,
    char_nul,
};

struct vt_count_payload
{
    // VT count 参数。默认 1 表示序列未显式提供数量或提供 0 时按 1 处理。
    short value = 1;
};

struct vt_position_payload
{
    // VT row 参数，1-based。0 不是合法位置；reset 后恢复到 1。
    short row = 1;
    // VT column 参数，1-based。调用方进入 console_state 时再转为 0-based。
    short col = 1;
};

struct vt_scroll_region_payload
{
    // DECSTBM top margin，1-based viewport-relative 行号。
    short top = 1;
    // DECSTBM bottom margin，0 表示未显式提供，调用方使用 viewport 最后一行。
    short bottom = 0;
};

struct vt_resize_payload
{
    // CSI 8 ; rows ; cols t 的行数。0 表示没有有效 resize payload。
    short rows = 0;
    // CSI 8 ; rows ; cols t 的列数。0 表示没有有效 resize payload。
    short cols = 0;
};

struct vt_palette_payload
{
    // OSC 4 palette index。
    short index = 0;
    // OSC 4 RGB 红色分量。
    uint8_t r = 0;
    // OSC 4 RGB 绿色分量。
    uint8_t g = 0;
    // OSC 4 RGB 蓝色分量。
    uint8_t b = 0;
};

enum class vt_sgr_flag : uint16_t
{
    reset = 1 << 0,
    bold = 1 << 1,
    faint = 1 << 2,
    italic = 1 << 3,
    underline = 1 << 4,
    blink = 1 << 5,
    negative = 1 << 6,
    conceal = 1 << 7,
    strikethrough = 1 << 8,
};

// 把 SGR 属性枚举转换成 bit mask，供 set_flags/clear_flags 共用同一位定义。
[[nodiscard]] constexpr uint16_t vt_sgr_flag_bit(vt_sgr_flag flag) noexcept
{
    return static_cast<uint16_t>(flag);
}

enum class vt_sgr_color_kind : uint8_t
{
    // 本条 SGR 没有修改该颜色通道。
    none,
    // 本条 SGR 要求恢复默认色，来自 39/49。
    default_,
    // value 保存 ANSI 16 色或 256 色索引。
    indexed,
    // value/g/b 保存 RGB 真彩色。
    rgb,
};

struct vt_sgr_color_payload
{
    // 当前颜色 payload 类型；none 时 value/g/b 无意义。
    vt_sgr_color_kind kind = vt_sgr_color_kind::none;
    // indexed 时是颜色索引；rgb 时是红色分量。
    uint8_t value = 0;
    // rgb 时是绿色分量。
    uint8_t g = 0;
    // rgb 时是蓝色分量。
    uint8_t b = 0;

    // 记录 SGR 39/49 语义：恢复终端默认前景/背景色。
    void set_default() noexcept
    {
        kind = vt_sgr_color_kind::default_;
        value = g = b = 0;
    }

    // 记录 ANSI 16 色或 256 色索引；value 保存索引，g/b 不参与读取。
    void set_index(short index) noexcept
    {
        kind = vt_sgr_color_kind::indexed;
        value = static_cast<uint8_t>(index);
        g = b = 0;
    }

    // 记录 SGR 38;2/48;2 RGB 真彩色参数。
    void set_rgb(uint8_t r, uint8_t green, uint8_t blue) noexcept
    {
        kind = vt_sgr_color_kind::rgb;
        value = r;
        g = green;
        b = blue;
    }

    // true 表示该颜色参数要求恢复默认色，而不是设置具体颜色。
    [[nodiscard]] bool is_default() const noexcept
    {
        return kind == vt_sgr_color_kind::default_;
    }

    // true 表示 value 保存的是 ANSI/256 色索引。
    [[nodiscard]] bool is_indexed() const noexcept
    {
        return kind == vt_sgr_color_kind::indexed;
    }

    // true 表示 value/g/b 三个字段保存 RGB 分量。
    [[nodiscard]] bool is_rgb() const noexcept
    {
        return kind == vt_sgr_color_kind::rgb;
    }
};

struct vt_sgr_payload
{
    // 本条 SGR 显式打开的属性 bit 集合。
    uint16_t set_flags = 0;
    // 本条 SGR 显式关闭的属性 bit 集合。
    uint16_t clear_flags = 0;
    // 前景色修改；kind==none 表示本条消息不改前景。
    vt_sgr_color_payload fg;
    // 背景色修改；kind==none 表示本条消息不改背景。
    vt_sgr_color_payload bg;

    // 标记某个文本属性需要启用，并取消同一属性的清除标记。
    void set(vt_sgr_flag flag) noexcept
    {
        const auto bit = vt_sgr_flag_bit(flag);
        set_flags |= bit;
        clear_flags &= static_cast<uint16_t>(~bit);
    }

    // 标记某个文本属性需要关闭，并取消同一属性的启用标记。
    void clear(vt_sgr_flag flag) noexcept
    {
        const auto bit = vt_sgr_flag_bit(flag);
        clear_flags |= bit;
        set_flags &= static_cast<uint16_t>(~bit);
    }

    // 查询本条 SGR 消息是否显式启用某属性。
    [[nodiscard]] bool has(vt_sgr_flag flag) const noexcept
    {
        return (set_flags & vt_sgr_flag_bit(flag)) != 0;
    }

    // 查询本条 SGR 消息是否显式关闭某属性。
    [[nodiscard]] bool clears(vt_sgr_flag flag) const noexcept
    {
        return (clear_flags & vt_sgr_flag_bit(flag)) != 0;
    }

    // true 表示本条 SGR 包含 0，调用方应重置全部图形属性。
    [[nodiscard]] bool has_reset() const noexcept
    {
        return has(vt_sgr_flag::reset);
    }
};

struct vt_win32_key_payload
{
    // Win32 virtual-key code。
    unsigned short vk = 0;
    // Win32 scan code。
    unsigned short sc = 0;
    // KEY_EVENT_RECORD::uChar.UnicodeChar；0 表示该键没有字符载荷。
    wchar_t uc = 0;
    // true 表示 KEY_DOWN，false 表示 KEY_UP。
    bool key_down = false;
    // KEY_EVENT_RECORD::dwControlKeyState 修饰键和键盘状态标志。
    unsigned long control_state = 0;
    // KEY_EVENT_RECORD::wRepeatCount；Win32Input 序列未提供时至少为 1。
    unsigned short repeat_count = 1;
};

union vt_message_payload {
    // continue_text/text/unknown_sequence 的字符视图；continue_text 指向
    // 本次 parse 输入，其它文本消息指向 parser raw 缓冲。
    std::u32string_view text;
    // OSC 0/2 标题视图，指向 parser raw 缓冲。
    std::u32string_view title;
    // 共用 count 参数，适用于 cursor/scroll/insert/delete 等消息。
    vt_count_payload count;
    // 共用 1-based row/col 参数，适用于 CUP/HVP/CHA/VPA/CPR。
    vt_position_payload position;
    // ED/EL erase mode，0/1/2/3 由对应消息解释。
    short erase_mode;
    // DECSTBM 滚动区域参数。
    vt_scroll_region_payload scroll_region;
    // DECSCUSR 光标形状参数，0 表示未指定形状。
    short cursor_shape;
    // DECCOLM 目标宽度；当前主要由 message id 表达 80/132。
    short window_width;
    // 终端 resize 通知参数。
    vt_resize_payload resize;
    // OSC 4 调色板参数。
    vt_palette_payload palette;
    // CPR 响应坐标，1-based。
    vt_position_payload cpr;
    // SGR 图形属性修改。
    vt_sgr_payload sgr;
    // Windows Terminal Win32 Input Mode 键盘事件。
    vt_win32_key_payload win32_key;

    // 默认构造为 sgr 分支，使 union 内含对象处于可析构/可赋值的平凡状态。
    constexpr vt_message_payload() noexcept : sgr{}
    {
    }
};

// ── vt_message ───────────────────────────────────────
// 解析出的单条消息。payload 成员由 vt_message_id 决定；视图仅在下一次
// parse(range) 调用或 reset() 前有效。
struct vt_message
{
    vt_message_payload payload;
};

struct vt_parse_result
{
    // 本次产出的消息类型；continue_ 表示还没有完整消息。
    vt_message_id id = vt_message_id::continue_;
    // 本次从输入范围中实际消费的 codepoint 数。continue_ 时可能等于输入
    // 长度；其它消息表示该消息结束位置。
    size_t consumed = 0;
    // 可透传的完整原始 ESC/CSI/OSC/SS3 序列。只有已完成的控制序列可能非空；
    // 普通 text/ground 控制字符为空。它和 message 中的 string_view 生命周期一致。
    std::u32string_view raw_sequence;
    // 当前 result 对应的结构化 payload。continue_ 时内容无效。调用方必须在
    // 下一次 parse/reset 前消费其中的 string_view。
    vt_message message;
};

// ── vt_parser ────────────────────────────────────────
class vt_parser
{
    // CSI 参数数量上限。超过该数量时标记 overflow，整条序列按文本处理。
    static constexpr size_t MAX_PARAMS = 16;

    enum class parser_mode
    {
        // 普通文本状态，可累积 payload.text。
        ground,
        // 已读取 ESC，等待 final/intermediate 或 CSI/OSC/SS3 入口。
        esc,
        // CSI 参数收集状态，等待 final 字符。
        csi,
        // OSC 字符串状态，等待 BEL 或 ST 终止。
        osc,
        // SS3 键盘序列状态。
        ss3,
        // OSC 中读到 ESC，等待 '\' 确认 ST。
        osc_st,
    };

  public:
    // raw 由调用方持有，parser 把当前消息文本/标题 view 指向该缓冲。
    explicit vt_parser(raw_u32_buffer &raw) noexcept : _raw(raw)
    {
    }

    [[nodiscard]] vt_parse_result parse(std::u32string_view input) noexcept
    {
        if (input.empty())
            return {};

        size_t i = 0;
        if (_mode == parser_mode::ground)
        {
            if (const auto text_run = _direct_ground_text_run_length(input); text_run != 0)
            {
                _msg.payload.text = input.substr(0, text_run);
                return _make_result(vt_message_id::continue_text, text_run);
            }

            auto id = _parse_ground_control(input[0]);
            if (id != vt_message_id::continue_)
                return _make_result(id, 1);
            i = 1;
        }

        for (; i != input.size(); ++i)
        {
            auto id = _parse_sequence_char(input[i]);
            if (id != vt_message_id::continue_)
                return _make_result(id, i + 1);
        }
        return {vt_message_id::continue_, input.size()};
    }

  private:
    // 返回输入开头可直接作为 ground 文本消费的 codepoint 数。调用点已经
    // 确认 parser 处于 ground；函数只负责找到首个 C0/DEL 控制字符。
    [[nodiscard]] size_t _direct_ground_text_run_length(std::u32string_view text) const noexcept
    {
        const auto it = std::ranges::find_if(text, [](char32_t ch) { return ch <= 0x1F || ch == 0x7F; });
        return static_cast<size_t>(it - text.begin());
    }

    // 返回当前正在解析的 ESC 序列原文；不在 ESC 序列内时返回空 view。
    [[nodiscard]] std::u32string_view _raw_sequence() const noexcept
    {
        if (_seq_start >= _raw.size() || _raw[_seq_start] != U'\x1b')
            return {};
        return {_raw.data() + _seq_start, _raw.size() - _seq_start};
    }

    [[nodiscard]] vt_parse_result _make_result(vt_message_id id, size_t consumed) const noexcept
    {
        auto message = vt_message{};
        auto raw_sequence = std::u32string_view{};
        if (id != vt_message_id::continue_)
        {
            message = _msg;
            raw_sequence = _raw_sequence();
        }
        return {
            id,
            consumed,
            raw_sequence,
            message,
        };
    }

    enum class ground_char_kind
    {
        printable,
        esc,
        carriage_return,
        line_feed,
        tab,
        nul,
        backspace,
        sub,
        del,
        other_control,
    };

    enum class csi_char_kind
    {
        control,
        digit,
        semicolon,
        private_marker,
        intermediate,
        final,
        invalid,
    };

    [[nodiscard]] static constexpr bool _is_c0_or_del(char32_t ch) noexcept
    {
        return ch <= 0x1F || ch == 0x7F;
    }

    // Ground 状态只分类一次，结果同时服务 echo、文本累计和控制消息生成。
    [[nodiscard]] static constexpr ground_char_kind _classify_ground_char(char32_t ch) noexcept
    {
        if (ch > 0x1F && ch != 0x7F)
            return ground_char_kind::printable;
        switch (ch)
        {
        case 0x00:
            return ground_char_kind::nul;
        case 0x08:
            return ground_char_kind::backspace;
        case U'\t':
            return ground_char_kind::tab;
        case U'\n':
            return ground_char_kind::line_feed;
        case U'\r':
            return ground_char_kind::carriage_return;
        case U'\x1b':
            return ground_char_kind::esc;
        case 0x1A:
            return ground_char_kind::sub;
        case 0x7F:
            return ground_char_kind::del;
        default:
            return ground_char_kind::other_control;
        }
    }

    [[nodiscard]] static constexpr vt_message_id _control_id_from_ground_kind(ground_char_kind kind) noexcept
    {
        switch (kind)
        {
        case ground_char_kind::nul:
            return vt_message_id::char_nul;
        case ground_char_kind::backspace:
        case ground_char_kind::del:
            return vt_message_id::char_del;
        case ground_char_kind::sub:
            return vt_message_id::char_sub;
        case ground_char_kind::tab:
            return vt_message_id::cursor_forward_tab;
        case ground_char_kind::carriage_return:
            return vt_message_id::carriage_return;
        case ground_char_kind::line_feed:
            return vt_message_id::line_feed;
        default:
            return vt_message_id::text;
        }
    }

    // CSI 状态下的字符类别互斥。dispatch 只在 final 类别发生，参数收集只在
    // digit/semicolon/private/intermediate 类别发生。
    [[nodiscard]] static constexpr csi_char_kind _classify_csi_char(char32_t ch) noexcept
    {
        if (_is_c0_or_del(ch))
            return csi_char_kind::control;
        if (ch >= U'0' && ch <= U'9')
            return csi_char_kind::digit;
        if (ch == U';')
            return csi_char_kind::semicolon;
        if (ch == U'?')
            return csi_char_kind::private_marker;
        if (ch >= 0x20 && ch <= 0x2F)
            return csi_char_kind::intermediate;
        if (ch >= 0x40 && ch <= 0x7E)
            return csi_char_kind::final;
        return csi_char_kind::invalid;
    }

    // 消费 ESC/CSI/OSC/SS3 序列中的一个 codepoint。_raw 只保存控制序列
    // 原文，ground 文本和 ground 控制字符不进入该缓冲。
    [[nodiscard]] vt_message_id _parse_sequence_char(char32_t ch) noexcept
    {
        // 合法序列用结构化字段返回；非法序列则用 _raw 切片作为 text
        // 透传，避免吞字节。
        _raw.push_back(ch);

        // 每个输入字符只按当前 mode 分派一次；各 mode helper 内部再做
        // 互斥字符分类，避免同一个位置反复检查 parser 状态。
        switch (_mode)
        {
        case parser_mode::ground:
            std::unreachable();
        case parser_mode::esc:
            return _parse_esc(ch);
        case parser_mode::csi:
            return _parse_csi(ch);
        case parser_mode::osc:
            return _parse_osc(ch);
        case parser_mode::osc_st:
            return _parse_osc_st(ch);
        case parser_mode::ss3:
            return _parse_ss3(ch);
        }
        std::unreachable();
    }

    [[nodiscard]] vt_message_id _parse_osc(char32_t ch) noexcept
    {
        const bool control = _is_c0_or_del(ch);
        if (control)
        {
            if (ch == 0x07) // BEL 正常结束 OSC
            {
                // OSC 以 BEL 结束时，BEL 本身保留在 _raw 里，dispatch 通过
                // 终止符位置计算 payload 范围。
                auto id = _dispatch_osc();
                _mode = parser_mode::ground;
                return _finish_seq(id);
            }
            if (ch == 0x1B) // ST 的开始 (ESC \)
            {
                // ESC 可能是 OSC ST 的第一字节，也可能是非法中断。先切换到
                // ESC 状态，下一字符为 '\' 时才完成 OSC。
                _mode = parser_mode::osc_st;
                return vt_message_id::continue_;
            }
            // 其他控制字符：OSC 被打断，序列转为文本。
            _set_unknown_sequence(_seq_start);
            _mode = parser_mode::ground;
            _osc_code = 0;
            _osc_len = 0;
            _osc_had_semi = false;
            return vt_message_id::unknown_sequence;
        }

        // 尚未遇到分号，解析 OSC 数字操作码。未知操作码也继续收集，
        // 最终 dispatch 失败后作为 text 透传。
        if (!_osc_had_semi)
        {
            if (ch >= U'0' && ch <= U'9')
            {
                _osc_code = static_cast<short>(_osc_code * 10 + (ch - U'0'));
                return vt_message_id::continue_;
            }
            if (ch == U';')
            {
                _osc_had_semi = true;
                return vt_message_id::continue_;
            }
            return vt_message_id::continue_; // 忽略其他字符（如空格）
        }

        // 标题内容直接留在 raw 中，dispatch 时再定位。调色板等参数需要窄
        // ASCII 缓冲供解析，非 ASCII 仅保留在 raw 中。
        if (_osc_code != 0 && _osc_code != 2 && _osc_len < _osc_buf.size() - 1 && ch < 0x80)
            _osc_buf[_osc_len++] = static_cast<char8_t>(ch);
        return vt_message_id::continue_;
    }

    // ST 终止符检测 (ESC \) — 仅当 ESC 来自 OSC 终止时触发。若不是 ST，
    // 同一个字符立刻按普通 ESC final 处理，不回到 parse 主入口重复分派。
    [[nodiscard]] vt_message_id _parse_osc_st(char32_t ch) noexcept
    {
        if (ch == U'\\')
        {
            auto id = _dispatch_osc();
            _mode = parser_mode::ground;
            return _finish_seq(id);
        }

        // 不是 \：ESC 退化为普通序列（如 OSC 被非 ST 字符打断）。
        _mode = parser_mode::esc;
        return _parse_esc(ch);
    }

    [[nodiscard]] vt_message_id _parse_csi(char32_t ch) noexcept
    {
        switch (_classify_csi_char(ch))
        {
        case csi_char_kind::control:
            _set_unknown_sequence(_seq_start);
            _mode = parser_mode::ground;
            _reset_params();
            return vt_message_id::unknown_sequence;
        case csi_char_kind::digit:
            // CSI 参数按 short 存储；异常大的数字会在 dispatch/default 逻辑中
            // 被截断或导致序列按文本处理。
            _current_param = static_cast<short>(_current_param * 10 + (ch - U'0'));
            _has_param = true;
            return vt_message_id::continue_;
        case csi_char_kind::semicolon:
            _add_param();
            return vt_message_id::continue_;
        case csi_char_kind::private_marker:
            _private_marker = true;
            return vt_message_id::continue_;
        case csi_char_kind::intermediate:
            // intermediate 只保留最后一个字节；当前支持的序列都只需要一个
            // intermediate，例如 DECSTR 的 '!p'。
            _intermediate = static_cast<char>(ch);
            return vt_message_id::continue_;
        case csi_char_kind::final: {
            _add_param();
            auto id = _dispatch_csi(ch);
            _mode = parser_mode::ground;
            _reset_params();
            return _finish_seq(id);
        }
        case csi_char_kind::invalid:
            _set_unknown_sequence(_seq_start);
            _mode = parser_mode::ground;
            _reset_params();
            return vt_message_id::unknown_sequence;
        }
        std::unreachable();
    }

    [[nodiscard]] vt_message_id _parse_ss3(char32_t ch) noexcept
    {
        if (_is_c0_or_del(ch))
        {
            _set_unknown_sequence(_seq_start);
            _mode = parser_mode::ground;
            return vt_message_id::unknown_sequence;
        }
        if (ch >= 0x40 && ch <= 0x7E)
        {
            auto id = _dispatch_ss3(ch);
            _mode = parser_mode::ground;
            return _finish_seq(id);
        }
        // 非法 SS3 终态
        _set_unknown_sequence(_seq_start);
        _mode = parser_mode::ground;
        return vt_message_id::unknown_sequence;
    }

    [[nodiscard]] vt_message_id _parse_esc(char32_t ch) noexcept
    {
        if (_is_c0_or_del(ch))
        {
            _set_unknown_sequence(_seq_start);
            _mode = parser_mode::ground;
            return vt_message_id::unknown_sequence;
        }

        switch (ch)
        {
        case U'[':
            _mode = parser_mode::csi;
            _reset_params();
            return vt_message_id::continue_;
        case U']':
            _mode = parser_mode::osc;
            _osc_code = 0;
            _osc_len = 0;
            _osc_had_semi = false;
            return vt_message_id::continue_;
        case U'O':
            _mode = parser_mode::ss3;
            return vt_message_id::continue_;
        case U'(':
            _intermediate = '(';
            return vt_message_id::continue_; // 等待字符集终态
        default:
            _mode = parser_mode::ground;
            if (_intermediate == '(')
            {
                _intermediate = 0;
                auto id = _dispatch_charset(ch);
                return _finish_seq(id);
            }
            if (_intermediate != 0)
            {
                _set_unknown_sequence(_seq_start);
                _intermediate = 0;
                return vt_message_id::unknown_sequence;
            }
            // 普通 ESC 序列
            auto id = _dispatch_esc(ch);
            return _finish_seq(id);
        }
    }

    [[nodiscard]] vt_message_id _parse_ground_control(char32_t ch) noexcept
    {
        const auto kind = _classify_ground_char(ch);
        if (kind == ground_char_kind::printable)
            std::unreachable();

        if (kind == ground_char_kind::esc)
        {
            _raw.push_back(ch);
            _seq_start = _raw.size() - 1;
            _mode = parser_mode::esc;
            return vt_message_id::continue_;
        }

        const auto control_id = _control_id_from_ground_kind(kind);
        if (kind == ground_char_kind::carriage_return || kind == ground_char_kind::line_feed)
        {
            _msg.payload.text = {};
            return control_id;
        }

        if (kind == ground_char_kind::tab)
        {
            // Tab 在状态层表现为 cursor_forward_tab，在输入层表现为 VK_TAB。
            _msg.payload.count.value = 1;
            return control_id;
        }

        // 其他 C0/DEL 控制字符。
        _msg.payload.text = {};
        return control_id;
    }

  public:
    // 调用方消费完 vt_parse_result 后调用。vt_message 很小，直接整体重置比
    // 按 id 清理 union 分支更简单；string_view payload 在 reset 后全部失效。
    void reset() noexcept
    {
        _msg = {};
        _reset_parser_state_after_message();
    }

  private:
    // 消息被消费后清理中央 raw 序列，并把 parser 拉回 ground。
    void _reset_parser_state_after_message() noexcept
    {
        _raw.clear();
        _seq_start = 0;
        _mode = parser_mode::ground;
    }

    // 解析出的消息体；消息类型由 parse(range) 返回的 vt_parse_result::id 给出。
    // union payload 中只有与该 id 对应的分支有效，reset() 后全部失效。
    vt_message _msg;

    // ── 中央缓冲区与视图位置 ──
    // _raw 保存当前跨字符累计的文本或控制序列；text-before-control、
    // unknown_sequence、OSC title 的 view 指向该缓冲。continue_text 不使用它。
    raw_u32_buffer &_raw;

    // 当前 ESC/CSI/OSC 序列在 _raw 中的起始偏移。vt_parse_result::raw_sequence
    // 和 unknown_sequence 都依赖它返回完整原文。
    size_t _seq_start = 0;

    // ── 解析状态 ──
    // 当前 VT 状态机模式。ground 表示可接收普通文本。
    parser_mode _mode = parser_mode::ground;
    // true 表示当前 CSI 使用 '?' private marker；只在 _mode==csi 时有意义。
    bool _private_marker = false;
    // ── CSI 参数收集 ──
    // _params 保存已经被 ';' 结束的参数；_current_param 保存正在累积的参数。
    // _param_index 是已提交参数数量，范围 0..MAX_PARAMS。
    std::array<short, MAX_PARAMS> _params{};
    size_t _param_index = 0;
    short _current_param = 0;
    // false 表示当前参数为空，dispatch 通过 _get_param 的默认值补 VT 默认参数。
    bool _has_param = false;
    // CSI/ESC intermediate 字节；0 表示当前序列没有 intermediate。
    char _intermediate = 0;
    // 参数数量或数值溢出标记；为 true 时整条 CSI 作为 unknown_sequence 交付。
    bool _csi_overflow = false;

    // ── OSC 参数收集 ──
    // OSC 操作码；0 既可能表示尚未解析，也可能是 OSC 0 标题，是否进入 payload
    // 由 _osc_had_semi 区分。
    short _osc_code = 0;
    // OSC 4 调色板参数的窄字符缓冲。标题 OSC 不使用它，标题直接 view 到 _raw。
    std::array<char8_t, 32> _osc_buf{};
    // _osc_buf 的有效长度，范围 0.._osc_buf.size()。
    size_t _osc_len = 0;
    // true 表示已经读到 OSC 操作码后的 ';'，后续字符属于 payload。
    bool _osc_had_semi = false;

    // ── 内部辅助函数 ──

    // 重置 CSI 参数收集状态；每条 CSI 结束或非法中断后必须调用。
    void _reset_params() noexcept
    {
        _params.fill(0);
        _param_index = 0;
        _current_param = 0;
        _has_param = false;
        _private_marker = false;
        _intermediate = 0;
        _csi_overflow = false;
    }

    // 将当前正在收集的 CSI 参数保存到参数数组；参数过多时只标记 overflow。
    void _add_param() noexcept
    {
        // 空参数通过 _has_param=false 表示；这里仍保存 0，dispatch 使用
        // _get_param(i, default) 决定默认值。
        if (_param_index < MAX_PARAMS)
            _params[_param_index++] = _current_param;
        else
            _csi_overflow = true;
        _current_param = 0;
        _has_param = false;
    }

    // 获取第 i 个已收集 CSI 参数；未提供该参数时返回调用方指定的默认值。
    short _get_param(size_t i, short d = 0) const noexcept
    {
        return (i < _param_index) ? _params[i] : d;
    }

    // VT 的 1-based 位置参数把缺省值和显式 0 都解释为默认位置。
    short _get_positive_param(size_t i, short d) const noexcept
    {
        const auto value = _get_param(i, d);
        return value == 0 ? d : _clamp(value);
    }

    // 把解析出的 short 参数限制在 VT handler 可接受的正数范围内。
    static short _clamp(short v) noexcept
    {
        return v > 32767 ? static_cast<short>(32767) : v;
    }

    // 把从 start 开始的原文标记为 unknown_sequence payload；非 OSC 错误
    // 没有安全的可见降级文本，因此 text 仍指向完整原文。
    void _set_unknown_sequence(size_t start) noexcept
    {
        _msg.payload.text = {_raw.data() + start, _raw.size() - start};
    }

    // 标记一个已完整解析但 corehost 不支持的 OSC。raw_sequence 仍由
    // _raw_sequence() 提供完整控制序列；text 只保存该 OSC 能安全降级显示的
    // 内容。OSC 8 hyperlink 的控制序列本身不可见，所以这里为空。
    void _set_unknown_osc_text(std::u32string_view text) noexcept
    {
        _msg.payload.text = text;
    }

    // 完成一个序列；continue_ 表示 dispatch 未识别，整段序列按 unknown 返回。
    vt_message_id _finish_seq(vt_message_id id) noexcept
    {
        if (id == vt_message_id::continue_)
        {
            // 未识别/非法序列按独立消息返回，调用方可以选择记录
            // raw_sequence，或显示 payload.text 中的降级文本。
            _set_unknown_sequence(_seq_start);
            id = vt_message_id::unknown_sequence;
        }
        _seq_start = 0;
        return id;
    }

    // ── 分发函数 ──

    // 分发普通 ESC 序列（ESC + 单个 final 字符）。
    vt_message_id _dispatch_esc(char32_t code) noexcept
    {
        switch (code)
        {
        case U'M':
            return vt_message_id::reverse_index;
        case U'7':
            return vt_message_id::save_cursor;
        case U'8':
            return vt_message_id::restore_cursor;
        case U'H':
            return vt_message_id::horizontal_tab_set;
        case U'=':
            return vt_message_id::keypad_app_mode;
        case U'>':
            return vt_message_id::keypad_numeric_mode;
        default:
            return vt_message_id::continue_;
        }
    }

    // 分发 SS3 键盘序列（ESC O + final）；这些输入应转换为结构化键消息。
    vt_message_id _dispatch_ss3(char32_t code) noexcept
    {
        switch (code)
        {
        case U'A':
            return vt_message_id::key_up;
        case U'B':
            return vt_message_id::key_down;
        case U'C':
            return vt_message_id::key_right;
        case U'D':
            return vt_message_id::key_left;
        case U'H':
            return vt_message_id::key_home;
        case U'F':
            return vt_message_id::key_end;
        case U'P':
            return vt_message_id::key_f1;
        case U'Q':
            return vt_message_id::key_f2;
        case U'R':
            return vt_message_id::key_f3;
        case U'S':
            return vt_message_id::key_f4;
        default:
            return vt_message_id::continue_;
        }
    }

    // 分发 G0 字符集选择；当前只建模 DEC line drawing 和 ASCII。
    vt_message_id _dispatch_charset(char32_t final) noexcept
    {
        if (final == U'0')
            return vt_message_id::designate_charset_line_drawing;
        if (final == U'B')
            return vt_message_id::designate_charset_ascii;
        return vt_message_id::continue_;
    }

    // ── OSC 载荷解析辅助函数（接受 u32string_view，无异常） ──

    // 从 OSC payload 的 pos 位置解析最多 5 位十进制数；pos 返回首个非数字位置。
    short _parse_osc_decimal_8(std::u32string_view s, size_t &pos) const noexcept
    {
        // 跳过前导空格
        auto first = std::ranges::find_if(s.begin() + pos, s.end(), [](char32_t ch) { return ch != U' '; });
        pos = static_cast<size_t>(first - s.begin());
        size_t start = pos;
        // 收集连续的数字字符，最多 5 个（保证不溢出 short）
        const auto digits_last = s.begin() + std::min(s.size(), start + size_t{5});
        auto digits_end =
            std::ranges::find_if(s.begin() + start, digits_last, [](char32_t ch) { return ch < U'0' || ch > U'9'; });
        pos = static_cast<size_t>(digits_end - s.begin());
        if (pos == start)
            return 0; // 无数字

        // 转换为窄字符数组供 std::from_chars 使用
        char numbuf[6]{};
        std::transform(s.begin() + start, digits_end, numbuf, [](char32_t ch) { return static_cast<char>(ch); });

        short value = 0;
        auto res = std::from_chars(numbuf, numbuf + (pos - start), value, 10);
        if (res.ec != std::errc{})
            return 0;
        return value;
    }

    // 从 OSC RGB payload 的 pos 位置解析最多 2 位十六进制分量，并跳过后续 '/'。
    uint8_t _parse_osc_hex_8(std::u32string_view buf, size_t &pos) const noexcept
    {
        // 收集最多 2 个十六进制数字
        size_t start = pos;
        const auto digits_last = buf.begin() + std::min(buf.size(), start + size_t{2});
        auto digits_end = std::ranges::find_if(buf.begin() + start, digits_last, [](char32_t ch) {
            return !((ch >= U'0' && ch <= U'9') || (ch >= U'A' && ch <= U'F') || (ch >= U'a' && ch <= U'f'));
        });
        pos = static_cast<size_t>(digits_end - buf.begin());
        if (pos == start)
            return 0; // 无有效十六进制字符

        // 转换为窄字符数组
        char hexbuf[3]{};
        std::transform(buf.begin() + start, digits_end, hexbuf, [](char32_t ch) { return static_cast<char>(ch); });

        // 跳过可能存在的分隔符 '/'
        auto slash_end = std::ranges::find_if(buf.begin() + pos, buf.end(), [](char32_t ch) { return ch != U'/'; });
        pos = static_cast<size_t>(slash_end - buf.begin());

        uint8_t value = 0;
        auto res = std::from_chars(hexbuf, hexbuf + (pos - start), value, 16);
        if (res.ec != std::errc{})
            return 0;
        return value;
    }

    // 分发完整 OSC 序列。支持标题和 OSC 4 调色板；未知 OSC 保留原文返回 unknown。
    vt_message_id _dispatch_osc() noexcept
    {
        auto &m = _msg;
        // seq 覆盖完整 OSC 序列，包括 ESC ] 和终止符；payload 在后面根据
        // 操作码后的分号与终止符位置切片出来。
        std::u32string_view seq(_raw.data() + _seq_start, _raw.size() - _seq_start);
        if (seq.size() < 3 || seq[0] != U'\x1B' || seq[1] != U']')
            return vt_message_id::continue_;

        // 解析操作码（数字）
        size_t pos = 2;
        short code = 0;
        while (pos < seq.size() && seq[pos] >= U'0' && seq[pos] <= U'9')
        {
            code = static_cast<short>(code * 10 + (seq[pos] - U'0'));
            ++pos;
        }
        if (pos >= seq.size() || seq[pos] != U';')
            return vt_message_id::continue_;
        ++pos; // 跳过分号

        // 查找终止符：BEL (0x07) 或 ST (ESC \)
        auto indices = std::views::iota(pos, seq.size());
        auto terminator = std::ranges::find_if(indices, [seq](size_t i) {
            return seq[i] == 0x07 || (seq[i] == 0x1B && i + 1 < seq.size() && seq[i + 1] == U'\\');
        });
        const auto end_pos = terminator == indices.end() ? std::u32string_view::npos : *terminator;
        if (end_pos == std::u32string_view::npos)
            return vt_message_id::continue_;

        // payload 在 _raw 中的绝对位置和长度
        size_t payload_start = _seq_start + pos;
        size_t payload_len = end_pos - pos;
        std::u32string_view payload(_raw.data() + payload_start, payload_len);

        switch (code)
        {
        case 8:
            _set_unknown_osc_text({});
            return vt_message_id::osc8_hyperlink;

        case 0:
        case 2:
            // OSC 0 和 OSC 2 都作为窗口标题处理；icon title 不单独建模。
            m.payload.title = payload; // 直接指向原始缓冲区
            return vt_message_id::set_window_title;

        case 4: {
            // 设置调色板颜色：索引;rgb:r/g/b
            size_t p = 0;
            m.payload.palette.index = _parse_osc_decimal_8(payload, p);
            // 跳过分号（如果存在）
            if (p < payload.size() && payload[p] == U';')
                ++p;
            // 期望 "rgb:" 前缀
            if (p + 4 <= payload.size() && (payload[p] == U'r' || payload[p] == U'R') &&
                (payload[p + 1] == U'g' || payload[p + 1] == U'G') &&
                (payload[p + 2] == U'b' || payload[p + 2] == U'B') && payload[p + 3] == U':')
            {
                p += 4;
                size_t p_before = p;
                m.payload.palette.r = _parse_osc_hex_8(payload, p);
                m.payload.palette.g = _parse_osc_hex_8(payload, p);
                m.payload.palette.b = _parse_osc_hex_8(payload, p);
                // 至少有一个颜色分量被成功解析才认为成功
                if (p == p_before)
                {
                    _set_unknown_osc_text({});
                    return vt_message_id::unknown_sequence;
                }
                return vt_message_id::set_palette_color;
            }
            _set_unknown_osc_text({});
            return vt_message_id::unknown_sequence;
        }
        default:
            _set_unknown_osc_text({});
            return vt_message_id::unknown_sequence;
        }
    }

    // 根据 CSI final 字符和已收集参数生成消息；不支持的组合返回 continue_，
    // 由 _finish_seq 转成 unknown_sequence 供调用方决定是否透传。
    vt_message_id _dispatch_csi(char32_t terminator) noexcept
    {
        auto &m = _msg;
        if (_csi_overflow)
            return vt_message_id::continue_; // 参数溢出，序列非法
        bool priv = _private_marker;

        // 默认 count = 第一个参数，若未提供则为 1，0 也视为 1
        auto n = (_param_index > 0) ? _get_param(0, 1) : static_cast<short>(1);
        if (n == 0)
            n = 1;

        switch (terminator)
        {
        // 相对光标移动；dispatch 生成钳制后的 count payload。
        case U'A':
            if (priv)
                return vt_message_id::continue_;
            m.payload.count.value = _clamp(n);
            return vt_message_id::cursor_up;
        case U'B':
            if (priv)
                return vt_message_id::continue_;
            m.payload.count.value = _clamp(n);
            return vt_message_id::cursor_down;
        case U'C':
            if (priv)
                return vt_message_id::continue_;
            m.payload.count.value = _clamp(n);
            return vt_message_id::cursor_forward;
        case U'D':
            if (priv)
                return vt_message_id::continue_;
            m.payload.count.value = _clamp(n);
            return vt_message_id::cursor_backward;
        case U'E':
            if (priv)
                return vt_message_id::continue_;
            m.payload.count.value = _clamp(n);
            return vt_message_id::cursor_next_line;
        case U'F':
            if (priv)
                return vt_message_id::continue_;
            m.payload.count.value = _clamp(n);
            return vt_message_id::cursor_prev_line;
        // 绝对/坐标型光标定位；dispatch 生成钳制后的 position payload。
        case U'G':
            if (priv)
                return vt_message_id::continue_;
            m.payload.position.col = _get_positive_param(0, 1);
            return vt_message_id::cursor_horiz_absolute;
        case U'd':
            if (priv)
                return vt_message_id::continue_;
            m.payload.position.row = _get_positive_param(0, 1);
            return vt_message_id::cursor_vert_absolute;
        case U'H':
        case U'f':
            if (priv)
                return vt_message_id::continue_;
            m.payload.position.row = _get_positive_param(0, 1);
            m.payload.position.col = _get_positive_param(1, 1);
            return vt_message_id::cursor_position;
        case U's':
            if (!priv)
                return vt_message_id::ansi_save_cursor;
            return vt_message_id::continue_;
        case U'u':
            if (!priv)
                return vt_message_id::ansi_restore_cursor;
            return vt_message_id::continue_;

        // DEC private h/l
        case U'h':
            if (!priv)
                return vt_message_id::continue_;
            switch (_get_param(0))
            {
            case 12:
                return vt_message_id::cursor_enable_blinking;
            case 25:
                return vt_message_id::cursor_show;
            case 1:
                return vt_message_id::cursor_keys_app_mode;
            case 3:
                return vt_message_id::set_columns_132;
            case 1049:
                return vt_message_id::use_alternate_buffer;
            default:
                return vt_message_id::continue_;
            }
        case U'l':
            if (!priv)
                return vt_message_id::continue_;
            switch (_get_param(0))
            {
            case 12:
                return vt_message_id::cursor_disable_blinking;
            case 25:
                return vt_message_id::cursor_hide;
            case 1:
                return vt_message_id::cursor_keys_normal_mode;
            case 3:
                return vt_message_id::set_columns_80;
            case 1049:
                return vt_message_id::use_main_buffer;
            default:
                return vt_message_id::continue_;
            }

        // 滚动
        case U'S':
            m.payload.count.value = _clamp(n);
            return vt_message_id::scroll_up;
        case U'T':
            m.payload.count.value = _clamp(n);
            return vt_message_id::scroll_down;

        // 窗口操作 (CSI … t)
        case U't': {
            // CSI 8 ; height ; width t — resize text area (WT sends this on resize)
            auto p0 = _get_param(0);
            if (p0 == 8)
            {
                m.payload.resize.rows = _clamp(_get_param(1));
                m.payload.resize.cols = _clamp(_get_param(2));
                return (m.payload.resize.rows > 0 && m.payload.resize.cols > 0) ? vt_message_id::resize_window
                                                                                : vt_message_id::continue_;
            }
            // CSI 4 ; height ; width t — resize in pixels (not supported, become text)
            return vt_message_id::continue_;
        }

        // 文本修改
        case U'@':
            m.payload.count.value = _clamp(n);
            return vt_message_id::insert_characters;
        case U'P':
            m.payload.count.value = _clamp(n);
            return vt_message_id::delete_characters;
        case U'X':
            m.payload.count.value = _clamp(n);
            return vt_message_id::erase_characters;
        case U'L':
            m.payload.count.value = _clamp(n);
            return vt_message_id::insert_lines;
        case U'M':
            m.payload.count.value = _clamp(n);
            return vt_message_id::delete_lines;
        case U'J':
            m.payload.erase_mode = _clamp(_get_param(0));
            return vt_message_id::erase_in_display;
        case U'K':
            m.payload.erase_mode = _clamp(_get_param(0));
            return vt_message_id::erase_in_line;

        // SGR
        case U'm':
            m.payload.sgr = {};
            for (size_t i = 0; i != _param_index; ++i)
            {
                short v = _params[i];
                if (v == 0)
                    m.payload.sgr.set(vt_sgr_flag::reset);
                else if (v == 1)
                    m.payload.sgr.set(vt_sgr_flag::bold);
                else if (v == 2)
                    m.payload.sgr.set(vt_sgr_flag::faint);
                else if (v == 3)
                    m.payload.sgr.set(vt_sgr_flag::italic);
                else if (v == 4)
                    m.payload.sgr.set(vt_sgr_flag::underline);
                else if (v == 5)
                    m.payload.sgr.set(vt_sgr_flag::blink);
                else if (v == 7)
                    m.payload.sgr.set(vt_sgr_flag::negative);
                else if (v == 8)
                    m.payload.sgr.set(vt_sgr_flag::conceal);
                else if (v == 9)
                    m.payload.sgr.set(vt_sgr_flag::strikethrough);
                else if (v == 22)
                {
                    m.payload.sgr.clear(vt_sgr_flag::bold);
                    m.payload.sgr.clear(vt_sgr_flag::faint);
                }
                else if (v == 23)
                    m.payload.sgr.clear(vt_sgr_flag::italic);
                else if (v == 24)
                    m.payload.sgr.clear(vt_sgr_flag::underline);
                else if (v == 25)
                    m.payload.sgr.clear(vt_sgr_flag::blink);
                else if (v == 27)
                    m.payload.sgr.clear(vt_sgr_flag::negative);
                else if (v == 28)
                    m.payload.sgr.clear(vt_sgr_flag::conceal);
                else if (v == 29)
                    m.payload.sgr.clear(vt_sgr_flag::strikethrough);
                else if (v >= 30 && v <= 37)
                    m.payload.sgr.fg.set_index(static_cast<short>(v - 30));
                else if (v == 38 && i + 1 < _param_index)
                {
                    if (_params[i + 1] == 5 && i + 2 < _param_index)
                    {
                        m.payload.sgr.fg.set_index(_params[i + 2]);
                        i += 2;
                    }
                    else if (_params[i + 1] == 2 && i + 4 < _param_index)
                    {
                        m.payload.sgr.fg.set_rgb(static_cast<uint8_t>(_params[i + 2]),
                                                 static_cast<uint8_t>(_params[i + 3]),
                                                 static_cast<uint8_t>(_params[i + 4]));
                        i += 4;
                    }
                    else
                        return vt_message_id::continue_;
                }
                else if (v == 39)
                    m.payload.sgr.fg.set_default();
                else if (v >= 40 && v <= 47)
                    m.payload.sgr.bg.set_index(static_cast<short>(v - 40));
                else if (v == 48 && i + 1 < _param_index)
                {
                    if (_params[i + 1] == 5 && i + 2 < _param_index)
                    {
                        m.payload.sgr.bg.set_index(_params[i + 2]);
                        i += 2;
                    }
                    else if (_params[i + 1] == 2 && i + 4 < _param_index)
                    {
                        m.payload.sgr.bg.set_rgb(static_cast<uint8_t>(_params[i + 2]),
                                                 static_cast<uint8_t>(_params[i + 3]),
                                                 static_cast<uint8_t>(_params[i + 4]));
                        i += 4;
                    }
                    else
                        return vt_message_id::continue_;
                }
                else if (v == 49)
                    m.payload.sgr.bg.set_default();
                else if (v >= 90 && v <= 97)
                    m.payload.sgr.fg.set_index(static_cast<short>(v - 90 + 8));
                else if (v >= 100 && v <= 107)
                    m.payload.sgr.bg.set_index(static_cast<short>(v - 100 + 8));
            }
            return vt_message_id::sgr;

        // 光标形状
        case U'q':
            if (_intermediate == ' ')
            {
                m.payload.cursor_shape = _clamp(_get_param(0));
                return vt_message_id::set_cursor_shape;
            }
            return vt_message_id::continue_;

        // 制表符
        case U'I':
            m.payload.count.value = _clamp(n);
            return vt_message_id::cursor_forward_tab;
        case U'Z':
            m.payload.count.value = _clamp(n);
            return vt_message_id::cursor_backward_tab;
        case U'g':
            switch (_get_param(0))
            {
            case 0:
                return vt_message_id::tab_clear_current;
            case 3:
                return vt_message_id::tab_clear_all;
            default:
                return vt_message_id::continue_;
            }

        // 滚动边距
        case U'r':
            m.payload.scroll_region.top = _clamp(_get_param(0, 1));
            m.payload.scroll_region.bottom = _clamp(_get_param(1, 0));
            return vt_message_id::set_scrolling_region;

        // 查询
        case U'n':
            if (_get_param(0) == 6)
                return vt_message_id::report_cursor_position;
            return vt_message_id::continue_;
        case U'c':
            return vt_message_id::device_attributes;

        // CPR 应答: CSI Pl ; Pc R — 终端对 DSR CPR (\x1b[6n) 的应答
        case U'R':
            if (!priv)
            {
                m.payload.cpr.row = _clamp(_get_param(0, 1));
                m.payload.cpr.col = _clamp(_get_param(1, 1));
                return (m.payload.cpr.row > 0 && m.payload.cpr.col > 0) ? vt_message_id::cpr_response
                                                                        : vt_message_id::continue_;
            }
            return vt_message_id::continue_;

        // 软复位
        case U'p':
            if (_intermediate == '!')
                return vt_message_id::soft_reset;
            return vt_message_id::continue_;

        // Win32 Input Mode 键盘事件: \x1b[Vk;Sc;Uc;Kd;Cs;Rc_
        case U'_': {
            // 参数格式: CSI params _  其中 params = Vk;Sc;Uc;Kd;Cs;Rc
            // 至少需要 Vk + KeyDown (2 个参数), 其余使用默认值
            m.payload.win32_key.vk = static_cast<unsigned short>(_clamp(_get_param(0, 0)));
            m.payload.win32_key.sc = static_cast<unsigned short>(_clamp(_get_param(1, 0)));
            m.payload.win32_key.uc = static_cast<wchar_t>(_clamp(_get_param(2, 0)));
            m.payload.win32_key.key_down = _get_param(3, 0) != 0;
            m.payload.win32_key.control_state = static_cast<unsigned long>(_clamp(_get_param(4, 0)));
            m.payload.win32_key.repeat_count = static_cast<unsigned short>(_clamp(_get_param(5, 1)));
            if (m.payload.win32_key.repeat_count == 0)
                m.payload.win32_key.repeat_count = 1;
            return vt_message_id::win32_input_key;
        }

        // 终端键盘功能键
        case U'~':
            switch (_get_param(0))
            {
            case 2:
                return vt_message_id::key_insert;
            case 3:
                return vt_message_id::key_delete;
            case 5:
                return vt_message_id::key_page_up;
            case 6:
                return vt_message_id::key_page_down;
            case 15:
                return vt_message_id::key_f5;
            case 17:
                return vt_message_id::key_f6;
            case 18:
                return vt_message_id::key_f7;
            case 19:
                return vt_message_id::key_f8;
            case 20:
                return vt_message_id::key_f9;
            case 21:
                return vt_message_id::key_f10;
            case 23:
                return vt_message_id::key_f11;
            case 24:
                return vt_message_id::key_f12;
            default:
                return vt_message_id::continue_;
            }
        default:
            return vt_message_id::continue_;
        }
    }
};

} // namespace corehost::conpty
