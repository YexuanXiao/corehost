// ── conpty/conpty_vt_parser.hpp ──────────────────────────
// Layer 2: VT/CSI/OSC 序列解析器 (char32_t 输入, 零分配视图)
//
// 设计目标：
//   · 完全以 Unicode 码点 (char32_t) 作为输入，调用方负责 UTF-8 解码。
//   · 不丢弃任何输入字符；无法识别的序列以 unknown_sequence 返回，
//     msg.payload.text 指向完整原文。
//   · vt_message 中的 title / text 为 std::u32string_view，指向内部缓冲区，
//     零拷贝，不产生额外的字符串内存分配。
//   · parse() 返回 vt_message_id，continue_ 表示尚未完成，text 表示文本消息，
//     unknown_sequence 表示未知/错误序列，其余为具体控制序列 id。
//   · 提供 reset<id>() 方法，根据已消费的消息类型精确重置受污染的字段，
//     对于 text/unknown/title 消息还会清空内部缓冲区 _raw，兼顾性能与内存。
//
// 使用方式：
//   raw_u32_buffer raw;
//   vt_parser p{raw};
//   for (char32_t ch : code_points) {
//       vt_message_id id = p.parse(ch);
//       if (id != vt_message_id::continue_) {
//           auto &m = p.get();
//           switch (id) {
//               case vt_message_id::text:
//                   // 使用 m.payload.text (u32string_view)
//                   break;
//               case vt_message_id::unknown_sequence:
//                   // 使用 m.payload.text (完整未知序列)
//                   break;
//               case vt_message_id::set_window_title:
//                   // 使用 m.payload.title (u32string_view)
//                   break;
//               ...
//           }
//           // case vt_message_id::text: p.reset<vt_message_id::text>();
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
#include "utility/raw_byte_allocator.hpp"

namespace conpty
{

// ── vt_message_id ────────────────────────────────────
// 所有可能的解析结果标识。
// continue_: 需要继续喂入字符，当前未产出完整消息。
// text:      普通文本。
// unknown_sequence: 无法识别或语法错误的控制序列原文。
enum class vt_message_id
{
    continue_text = 0, // 可打印字符 → echo + 插入行缓冲
    continue_ = 1,     // 转义内部状态 → 无操作
    text = 2,          // 纯可打印文本消息（不含控制字符）
    unknown_sequence,  // 无法识别或语法错误的 ESC/CSI/OSC/SS3 序列，msg.payload.text 为完整原文
    carriage_return,   // \r
    line_feed,         // \n
    reverse_index,
    save_cursor,
    restore_cursor,
    horizontal_tab_set,
    keypad_app_mode,
    keypad_numeric_mode,
    designate_charset_line_drawing,
    designate_charset_ascii,
    ansi_save_cursor,
    ansi_restore_cursor,
    cursor_enable_blinking,
    cursor_disable_blinking,
    cursor_show,
    cursor_hide,
    cursor_keys_app_mode,
    cursor_keys_normal_mode,
    report_cursor_position,
    device_attributes,
    tab_clear_current,
    tab_clear_all,
    set_window_title,
    use_alternate_buffer,
    use_main_buffer,
    soft_reset,
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

    cursor_up,
    cursor_down,
    cursor_forward,
    cursor_backward,
    cursor_next_line,
    cursor_prev_line,
    scroll_up,
    scroll_down,
    insert_characters,
    delete_characters,
    erase_characters,
    insert_lines,
    delete_lines,
    cursor_forward_tab,
    cursor_backward_tab,

    cursor_vert_absolute,
    cursor_horiz_absolute,
    cursor_position,
    set_cursor_shape,
    erase_in_display,
    erase_in_line,
    set_palette_color,
    set_scrolling_region,
    set_columns_132,
    set_columns_80,
    resize_window,   // \x1b[8;height;width t — terminal resize notification
    win32_input_key, // \x1b[Vk;Sc;Uc;Kd;Cs;Rc_ — Win32 Input Mode 键盘事件
    cpr_response,    // \x1b[Pl;PcR — 终端对 DSR CPR 的应答
    sgr,
};

struct vt_count_payload
{
    short value = 1;
};

struct vt_position_payload
{
    short row = 1;
    short col = 1;
};

struct vt_scroll_region_payload
{
    short top = 1;
    short bottom = 0;
};

struct vt_resize_payload
{
    short rows = 0;
    short cols = 0;
};

struct vt_palette_payload
{
    short index = 0;
    uint8_t r = 0;
    uint8_t g = 0;
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

[[nodiscard]] constexpr uint16_t vt_sgr_flag_bit(vt_sgr_flag flag) noexcept
{
    return static_cast<uint16_t>(flag);
}

enum class vt_sgr_color_kind : uint8_t
{
    none,
    default_,
    indexed,
    rgb,
};

struct vt_sgr_color_payload
{
    vt_sgr_color_kind kind = vt_sgr_color_kind::none;
    uint8_t value = 0;
    uint8_t g = 0;
    uint8_t b = 0;

    void set_default() noexcept
    {
        kind = vt_sgr_color_kind::default_;
        value = g = b = 0;
    }

    void set_index(short index) noexcept
    {
        kind = vt_sgr_color_kind::indexed;
        value = static_cast<uint8_t>(index);
        g = b = 0;
    }

    void set_rgb(uint8_t r, uint8_t green, uint8_t blue) noexcept
    {
        kind = vt_sgr_color_kind::rgb;
        value = r;
        g = green;
        b = blue;
    }

    [[nodiscard]] bool is_default() const noexcept
    {
        return kind == vt_sgr_color_kind::default_;
    }

    [[nodiscard]] bool is_indexed() const noexcept
    {
        return kind == vt_sgr_color_kind::indexed;
    }

    [[nodiscard]] bool is_rgb() const noexcept
    {
        return kind == vt_sgr_color_kind::rgb;
    }
};

struct vt_sgr_payload
{
    uint16_t set_flags = 0;
    uint16_t clear_flags = 0;
    vt_sgr_color_payload fg;
    vt_sgr_color_payload bg;

    void set(vt_sgr_flag flag) noexcept
    {
        const auto bit = vt_sgr_flag_bit(flag);
        set_flags |= bit;
        clear_flags &= static_cast<uint16_t>(~bit);
    }

    void clear(vt_sgr_flag flag) noexcept
    {
        const auto bit = vt_sgr_flag_bit(flag);
        clear_flags |= bit;
        set_flags &= static_cast<uint16_t>(~bit);
    }

    [[nodiscard]] bool has(vt_sgr_flag flag) const noexcept
    {
        return (set_flags & vt_sgr_flag_bit(flag)) != 0;
    }

    [[nodiscard]] bool clears(vt_sgr_flag flag) const noexcept
    {
        return (clear_flags & vt_sgr_flag_bit(flag)) != 0;
    }

    [[nodiscard]] bool has_reset() const noexcept
    {
        return has(vt_sgr_flag::reset);
    }
};

struct vt_win32_key_payload
{
    unsigned short vk = 0;
    unsigned short sc = 0;
    wchar_t uc = 0;
    bool key_down = false;
    unsigned long control_state = 0;
    unsigned short repeat_count = 1;
};

union vt_message_payload
{
    std::u32string_view text;
    std::u32string_view title;
    vt_count_payload count;
    vt_position_payload position;
    short erase_mode;
    vt_scroll_region_payload scroll_region;
    short cursor_shape;
    short window_width;
    vt_resize_payload resize;
    vt_palette_payload palette;
    vt_position_payload cpr;
    vt_sgr_payload sgr;
    vt_win32_key_payload win32_key;

    constexpr vt_message_payload() noexcept : sgr{} {}
};

// ── vt_message ───────────────────────────────────────
// 解析出的单条消息。payload 成员由 vt_message_id 决定；视图仅在下一次
// parse() 调用或 reset() 前有效。
struct vt_message
{
    vt_message_payload payload;
};

enum class vt_parse_consumption
{
    consumed,
    retry,
};

struct vt_parse_result
{
    vt_message_id id = vt_message_id::continue_;
    vt_parse_consumption consumption = vt_parse_consumption::consumed;
};

// ── vt_parser ────────────────────────────────────────
class vt_parser
{
    // CSI 参数数量上限。超过该数量时标记 overflow，整条序列按文本处理。
    static constexpr size_t MAX_PARAMS = 16;

    enum class parser_mode
    {
        ground,
        esc,
        csi,
        osc,
        ss3,
        osc_st,
        pending_esc,
    };

  public:
    explicit vt_parser(raw_u32_buffer &raw) : _raw(raw)
    {
    }

    // 访问最后产出的消息（不含 id，id 由 parse() 返回值提供）
    const vt_message &get() const noexcept
    {
        return _msg;
    }
    vt_message &get() noexcept
    {
        return _msg;
    }

    // parse() 内部已判定当前字符是否该回显（地面态可打印/可见控制字符）
    [[nodiscard]] bool should_echo_last() const noexcept
    {
        return _should_echo;
    }

    [[nodiscard]] std::u32string_view raw_sequence() const noexcept
    {
        if (_seq_start >= _raw.size() || _raw[_seq_start] != U'\x1b')
            return {};
        return {_raw.data() + _seq_start, _raw.size() - _seq_start};
    }

    // 是否有未交付的累积文本（纯可打印字符无控制字符终止时残留）
    [[nodiscard]] bool has_pending_text() const noexcept
    {
        return _ground_text_start != npos && !_raw.empty();
    }

    [[nodiscard]] bool can_accept_direct_ground_text() const noexcept
    {
        return _pending_control == vt_message_id::continue_ && _mode == parser_mode::ground;
    }

    [[nodiscard]] size_t direct_ground_text_run_length(std::u32string_view text) const noexcept
    {
        if (!can_accept_direct_ground_text())
            return 0;

        const auto it = std::ranges::find_if(text, [](char32_t ch) {
            return ch <= 0x1F || ch == 0x7F;
        });
        return static_cast<size_t>(it - text.begin());
    }

    // 释放累积文本为 text 消息并返回 text id；无残留文本时返回 continue_
    [[nodiscard]] vt_message_id flush_text()
    {
        // flush_text 只交付地面态累积文本；正在解析 ESC/CSI/OSC 时不能调用它
        // 来强行结束序列，否则非法序列会丢失原文。
        if (_ground_text_start == npos || _raw.empty())
            return vt_message_id::continue_;
        _msg.payload.text = {_raw.data() + _ground_text_start, _raw.size() - _ground_text_start};
        _ground_text_start = npos;
        return vt_message_id::text;
    }

    [[nodiscard]] vt_parse_result parse_with_consumption(char32_t ch)
    {
        // parse() 的旧接口只能返回一个 message id。当前一个输入字符同时结束
        // text 并代表控制消息时，parser 会先交付 text，把控制消息放入
        // _pending_control。这个接口把“交付 pending control”和“消费新字符”
        // 分开表达：pending control 先返回，retry 要求调用者下一轮
        // 重新提交同一个 ch。
        if (_pending_control != vt_message_id::continue_)
        {
            auto id = _pending_control;
            _pending_control = vt_message_id::continue_;
            _msg.payload.text = {};
            if (id == vt_message_id::cursor_forward_tab)
                _msg.payload.count.value = 1;
            return {id, vt_parse_consumption::retry};
        }

        return {_parse_consuming(ch), vt_parse_consumption::consumed};
    }

    // 兼容旧调用方：只返回 message id，pending control 会像旧实现一样消费
    // 传入字符。需要知道字符是否被消费的新路径应使用 parse_with_consumption。
    [[nodiscard]] vt_message_id parse(char32_t ch)
    {
        auto result = parse_with_consumption(ch);
        if (result.consumption == vt_parse_consumption::retry)
        {
            if (result.id == vt_message_id::carriage_return && ch == U'\n')
                _pending_control = vt_message_id::line_feed;
            return result.id;
        }
        return result.id;
    }

  private:
    [[nodiscard]] static constexpr bool _is_ground_printable(char32_t ch) noexcept
    {
        return ch > 0x1F && ch != 0x7F;
    }

    // 解析单个码点，返回当前产生的消息 id，continue_ 表示尚未完成
    [[nodiscard]] vt_message_id _parse_consuming(char32_t ch)
    {
        // U'\0' 是 drain sentinel：无排队消息时立即返回 continue_，不产 char_nul
        if (ch == U'\0')
            return vt_message_id::continue_;

        // 如果上次因 text→ESC 过渡而暂存了 ESC，先还原。
        if (_mode == parser_mode::pending_esc)
        {
            // 上次在普通文本后遇到 ESC，为了先返回 text 曾把 ESC 从 _raw 弹出。
            // 现在恢复它并进入 ESC 状态，保证序列原文从 ESC 开始。
            _raw.push_back(U'\x1B');
            _seq_start = _raw.size() - 1;
            _mode = parser_mode::esc;
        }

        // 所有字符先写入 _raw。合法序列用结构化字段返回；非法序列则用
        // _raw 的切片作为 text 透传，避免吞字节。
        _raw.push_back(ch);

        // ── echo 判定：仅地面态可打印字符及有视觉效果的 C0 字符回显 ──
        _should_echo = false;
        if (_mode == parser_mode::ground)
        {
            if (_is_ground_printable(ch))
                _should_echo = true; // 可打印
            // ground-state printable: echo, bridge inserts into _cooked_buf at cursor
            else if (ch == U'\r' || ch == U'\n' || ch == U'\t')
                _should_echo = true; // 可见控制字符
            // \b ESC 及其他 C0/DEL：不回显（Bridge 在 char_del 中处理 echo）
        }

        // 根据当前状态更新文本起点或跳过
        if (_mode != parser_mode::ground)
        {
            // 已在某个序列内部，无需额外操作
        }
        else
        {
            // 处于 Ground 状态，记录普通文本的起点
            if (_ground_text_start == npos)
                _ground_text_start = _raw.size() - 1;
        }

        // ── OSC 字符串模式 ──
        if (_mode == parser_mode::osc)
        {
            // 控制字符（BEL 和 ESC 除外）中断 OSC
            if (ch <= 0x1F || ch == 0x7F)
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
                // 其他控制字符：OSC 被打断，序列转为文本，再处理该控制字符
                _set_unknown_sequence(_seq_start);
                _mode = parser_mode::ground;
                _osc_code = 0;
                _osc_len = 0;
                _osc_had_semi = false;
                // 控制字符中断序列，文本已设，返回 text 供透传
                return vt_message_id::unknown_sequence;
            }

            // 尚未遇到分号，解析 OSC 数字操作码
            if (!_osc_had_semi)
            {
                // OSC 操作码只解析分号前的十进制数字。未知操作码也继续收集，
                // 最终 dispatch 失败后作为 text 透传。
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

            // 已进入 payload 区域
            if (_osc_code == 0 || _osc_code == 2)
            {
                // 标题 payload 可能包含非 ASCII，直接保留在 _raw，最终返回
                // u32string_view。
                // 标题内容直接留在 raw 中，dispatch 时再定位
            }
            else
            {
                // 调色板等参数：ASCII 写入窄缓冲供解析，非 ASCII 仅保留在 raw 中
                if (_osc_len < _osc_buf.size() - 1 && ch < 0x80)
                    _osc_buf[_osc_len++] = static_cast<char8_t>(ch);
            }
            return vt_message_id::continue_;
        }

        // ST 终止符检测 (ESC \) — 仅当 ESC 来自 OSC 终止时触发
        if (_mode == parser_mode::osc_st)
        {
            if (ch == U'\\')
            {
                auto id = _dispatch_osc();
                _mode = parser_mode::ground;
                return _finish_seq(id);
            }
            // 不是 \：ESC 退化为普通序列（如 OSC 被非 ST 字符打断）
            _mode = parser_mode::esc;
        }
        if (_mode == parser_mode::csi)
        {
            // 控制字符中断 CSI
            if (ch <= 0x1F || ch == 0x7F)
            {
                _set_unknown_sequence(_seq_start);
                _mode = parser_mode::ground;
                _reset_params();
                return vt_message_id::unknown_sequence;
            }
            if (ch >= U'0' && ch <= U'9')
            {
                // CSI 参数按 short 存储；异常大的数字会在 dispatch/default 逻辑中
                // 被截断或导致序列按文本处理。
                _current_param = static_cast<short>(_current_param * 10 + (ch - U'0'));
                _has_param = true;
                return vt_message_id::continue_;
            }
            if (ch == U';')
            {
                _add_param();
                return vt_message_id::continue_;
            }
            if (ch == U'?')
            {
                _private_marker = true;
                return vt_message_id::continue_;
            }
            if (ch >= 0x20 && ch <= 0x2F)
            {
                // intermediate 只保留最后一个字节；当前支持的序列都只需要一个
                // intermediate，例如 DECSTR 的 '!p'。
                _intermediate = static_cast<char>(ch);
                return vt_message_id::continue_;
            }
            if (ch >= 0x40 && ch <= 0x7E)
            {
                _add_param();
                auto id = _dispatch_csi(ch);
                _mode = parser_mode::ground;
                _reset_params();
                return _finish_seq(id);
            }
            // 非法 CSI 字符：整段作为文本输出
            _set_unknown_sequence(_seq_start);
            _mode = parser_mode::ground;
            _reset_params();
            return vt_message_id::unknown_sequence;
        }

        // ── SS3 模式 (ESC O) ──
        if (_mode == parser_mode::ss3)
        {
            if (ch <= 0x1F || ch == 0x7F)
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

        // ── ESC 状态 ──
        if (_mode == parser_mode::esc)
        {
            if (ch <= 0x1F || ch == 0x7F)
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

        // ── Ground：普通文本 / 控制字符 ──
        if (ch <= 0x1F || ch == 0x7F)
        {
            bool has_text = (_ground_text_start != npos && _raw.size() > 1);

            if (ch == 0x1B)
            {
                if (has_text)
                {
                    // 普通文本后紧跟 ESC 时，先交付 ESC 前的文本。ESC 留给下次
                    // parse 处理，避免一个返回值同时表示 text 和控制序列。
                    _msg.payload.text = {_raw.data() + _ground_text_start, _raw.size() - 1 - _ground_text_start};
                    _raw.pop_back(); // 弹出 ESC，下次 parse 再还原
                    _mode = parser_mode::pending_esc;
                    _ground_text_start = npos;
                    return vt_message_id::text;
                }
                _seq_start = _raw.size() - 1;
                _mode = parser_mode::esc;
                _ground_text_start = npos;
                return vt_message_id::continue_;
            }

            // ── \r \n：专用消息，前导文本先交付 ──
            if (ch == U'\r' || ch == U'\n')
            {
                // CR/LF 不进入 _raw 文本视图；它们作为独立控制消息交付。
                _raw.pop_back(); // 控制字符不进入 raw
                vt_message_id cid = (ch == U'\r') ? vt_message_id::carriage_return : vt_message_id::line_feed;
                if (has_text)
                {
                    _msg.payload.text = {_raw.data() + _ground_text_start, _raw.size() - _ground_text_start};
                    _ground_text_start = npos;
                    _pending_control = cid;
                    return vt_message_id::text;
                }
                _msg.payload.text = {};
                _ground_text_start = npos;
                return cid;
            }

            // ── \\t：cursor_forward_tab；有前导文本时先交付文本，tab 延迟 ──
            if (ch == U'\t')
            {
                // Tab 在状态层表现为 cursor_forward_tab，在输入层表现为 VK_TAB。
                // 有前导文本时同样先返回 text，再延迟交付 tab。
                if (has_text)
                {
                    _msg.payload.text = {_raw.data() + _ground_text_start, _raw.size() - 1 - _ground_text_start};
                    _raw.pop_back(); // 去掉 \\t，下次交付
                    _ground_text_start = npos;
                    _pending_control = vt_message_id::cursor_forward_tab;
                    return vt_message_id::text;
                }
                _raw.pop_back();
                _msg.payload.count.value = 1;
                _ground_text_start = npos;
                return vt_message_id::cursor_forward_tab;
            }

            // ── 其他控制字符（BS→char_del, NUL→char_nul, SUB→char_sub, DEL→char_del）──
            // 有前导文本时先交付文本，控制字符延迟
            if (has_text)
            {
                _msg.payload.text = {_raw.data() + _ground_text_start, _raw.size() - 1 - _ground_text_start};
                _raw.pop_back();
                _ground_text_start = npos;
                // 延迟交付控制字符
                _pending_control = _classify_control(ch);
                return vt_message_id::text;
            }
            _raw.pop_back();
            _ground_text_start = npos;
            _msg.payload.text = {};
            return _classify_control(ch);
        }

        // 可打印字符，继续累积
        return vt_message_id::continue_text;
    }

  public:
    // 根据已消费的消息类型重置受污染的字段与解析器状态。
    template <vt_message_id id>
    void reset()
    {
        switch (id)
        {
        case vt_message_id::cursor_up:
        case vt_message_id::cursor_down:
        case vt_message_id::cursor_forward:
        case vt_message_id::cursor_backward:
        case vt_message_id::cursor_next_line:
        case vt_message_id::cursor_prev_line:
        case vt_message_id::scroll_up:
        case vt_message_id::scroll_down:
        case vt_message_id::insert_characters:
        case vt_message_id::delete_characters:
        case vt_message_id::erase_characters:
        case vt_message_id::insert_lines:
        case vt_message_id::delete_lines:
        case vt_message_id::cursor_forward_tab:
        case vt_message_id::cursor_backward_tab:
            _reset_count();
            break;

        case vt_message_id::cursor_vert_absolute:
            _reset_row();
            break;

        case vt_message_id::cursor_horiz_absolute:
            _reset_col();
            break;

        case vt_message_id::cursor_position:
            _reset_position();
            break;

        case vt_message_id::set_cursor_shape:
            _reset_cursor_shape();
            break;

        case vt_message_id::erase_in_display:
        case vt_message_id::erase_in_line:
            _reset_erase_mode();
            break;

        case vt_message_id::set_palette_color:
            _reset_palette_color();
            break;

        case vt_message_id::set_scrolling_region:
            _reset_scrolling_region();
            break;

        case vt_message_id::set_columns_132:
        case vt_message_id::set_columns_80:
            _reset_window_width();
            break;

        case vt_message_id::resize_window:
            _reset_resize_window();
            break;

        case vt_message_id::win32_input_key:
            _reset_win32_input_key();
            break;

        case vt_message_id::cpr_response:
            _reset_cpr_response();
            break;

        case vt_message_id::sgr:
            _reset_sgr();
            break;

        default:
            break;
        }

        // 清空中央缓冲区。pending_esc 表示 text→ESC 过渡，下一次 parse()
        // 会先补回 ESC；其它模式在消息交付后都回到 ground。
        _reset_parser_state_after_message();
        // _pending_control 必须保留—延迟交付的 CR/LF/TAB 由下次 parse() 开头消费
    }

  private:
    void _reset_count() noexcept
    {
        _msg.payload.count.value = 1;
    }

    void _reset_row() noexcept
    {
        _msg.payload.position.row = 1;
    }

    void _reset_col() noexcept
    {
        _msg.payload.position.col = 1;
    }

    void _reset_position() noexcept
    {
        _msg.payload.position.row = 1;
        _msg.payload.position.col = 1;
    }

    void _reset_cursor_shape() noexcept
    {
        _msg.payload.cursor_shape = 0;
    }

    void _reset_erase_mode() noexcept
    {
        _msg.payload.erase_mode = 0;
    }

    void _reset_palette_color() noexcept
    {
        _msg.payload.palette.index = 0;
        _msg.payload.palette.r = _msg.payload.palette.g = _msg.payload.palette.b = 0;
    }

    void _reset_scrolling_region() noexcept
    {
        _msg.payload.scroll_region.top = 1;
        _msg.payload.scroll_region.bottom = 0;
    }

    void _reset_window_width() noexcept
    {
        _msg.payload.window_width = 0;
    }

    void _reset_resize_window() noexcept
    {
        _msg.payload.resize.rows = 0;
        _msg.payload.resize.cols = 0;
    }

    void _reset_win32_input_key() noexcept
    {
        _msg.payload.win32_key.vk = 0;
        _msg.payload.win32_key.sc = 0;
        _msg.payload.win32_key.uc = 0;
        _msg.payload.win32_key.key_down = false;
        _msg.payload.win32_key.control_state = 0;
        _msg.payload.win32_key.repeat_count = 1;
    }

    void _reset_cpr_response() noexcept
    {
        _msg.payload.cpr.row = 0;
        _msg.payload.cpr.col = 0;
    }

    void _reset_sgr() noexcept
    {
        _msg.payload.sgr = {};
    }

    void _reset_parser_state_after_message()
    {
        _raw.clear();
        _ground_text_start = npos;
        _seq_start = 0;
        if (_mode != parser_mode::pending_esc)
            _mode = parser_mode::ground;
    }

    // 解析出的消息体；消息类型由 parse() 返回值给出。
    vt_message _msg;

    // ── 中央缓冲区与视图位置 ──
    // _raw 保存当前未消费输入；message 里的 string_view 指向该缓冲。
    raw_u32_buffer &_raw;

    // npos 表示没有有效偏移。
    static constexpr size_t npos = ~size_t{0};

    // 当前普通文本段在 _raw 中的起始偏移；npos 表示没有累积文本。
    size_t _ground_text_start = npos;

    // 当前 ESC/CSI/OSC 序列在 _raw 中的起始偏移。
    size_t _seq_start = 0;

    // ── 解析状态 ──
    parser_mode _mode = parser_mode::ground;
    bool _private_marker = false; // true 表示 CSI '?' private marker
    bool _should_echo = false;    // 最近一次 parse() 的字符是否该回显

    // ── 延迟交付：has_text+控制字符时先交付文本，控制消息下次返回 ──
    // continue_ 表示没有排队消息。
    vt_message_id _pending_control = vt_message_id::continue_;

    // ── CSI 参数收集 ──
    std::array<short, MAX_PARAMS> _params{};
    size_t _param_index = 0;    // 当前已收集参数个数，范围 0..MAX_PARAMS
    short _current_param = 0;   // 正在解析的当前参数值
    bool _has_param = false;    // 当前参数是否被显式赋值（用于区分默认值）
    char _intermediate = 0;     // 0 表示没有 CSI/ESC intermediate 字节
    bool _csi_overflow = false; // 参数数量或数值溢出标记

    // ── OSC 参数收集 ──
    short _osc_code = 0;             // OSC 操作码；0 表示尚未解析
    std::array<char8_t, 32> _osc_buf{}; // OSC 4 调色板参数窄字符缓冲
    size_t _osc_len = 0;             // _osc_buf 的有效长度，范围 0.._osc_buf.size()
    bool _osc_had_semi = false;      // true 表示已进入 payload

    // ── 内部辅助函数 ──

    // 重置 CSI 参数收集状态
    void _reset_params()
    {
        _params.fill(0);
        _param_index = 0;
        _current_param = 0;
        _has_param = false;
        _private_marker = false;
        _intermediate = 0;
        _csi_overflow = false;
    }

    // 将当前正在收集的参数值保存到参数数组
    void _add_param()
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

    // 获取第 i 个参数，若不存在则返回默认值 d
    short _get_param(size_t i, short d = 0) const
    {
        return (i < _param_index) ? _params[i] : d;
    }

    // 限制参数值在有效范围内（32767）
    short _clamp(short v)
    {
        return v > 32767 ? static_cast<short>(32767) : v;
    }

    void _set_unknown_sequence(size_t start)
    {
        _msg.payload.text = {_raw.data() + start, _raw.size() - start};
        _ground_text_start = _raw.size();
    }

    // 完成一个序列；continue_ 表示 dispatch 未识别，整段序列按 unknown 返回。
    vt_message_id _finish_seq(vt_message_id id)
    {
        if (id != vt_message_id::continue_)
        {
            _ground_text_start = npos; // 无累积文本
        }
        else
        {
            // 未识别/非法序列按独立消息返回，调用方可以选择透传或丢弃。
            _set_unknown_sequence(_seq_start);
            id = vt_message_id::unknown_sequence;
        }
        _seq_start = 0;
        return id;
    }

    // 处理 Ground 状态下遇到的控制字符（C0 和 DEL），返回对应的消息 id。
    // 注意：\\r \\n \\t 和 ESC 已在调用方分流，不会进入此函数。
    vt_message_id _classify_control(char32_t ch)
    {
        switch (ch)
        {
        case 0x00:
            return vt_message_id::char_nul;
        case 0x08:
            return vt_message_id::char_del; // BS
        case 0x09:
            return vt_message_id::cursor_forward_tab; // \\t (fallback)
        case 0x0A:
            return vt_message_id::line_feed;
        case 0x0D:
            return vt_message_id::carriage_return;
        case 0x1A:
            return vt_message_id::char_sub;
        case 0x7F:
            return vt_message_id::char_del; // DEL
        default:
            return vt_message_id::text; // 其他 C0：透明化为空 text
        }
    }

    // ── 分发函数 ──

    // 普通 ESC 序列（ESC + 单个字符）
    vt_message_id _dispatch_esc(char32_t code)
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

    // SS3 键盘序列（ESC O + 一个字符），不回显原始字节：dispatch 生成钳制 CUP
    vt_message_id _dispatch_ss3(char32_t code)
    {
        switch (code)
        {
        case U'A':
            _should_echo = false;
            return vt_message_id::key_up;
        case U'B':
            _should_echo = false;
            return vt_message_id::key_down;
        case U'C':
            _should_echo = false;
            return vt_message_id::key_right;
        case U'D':
            _should_echo = false;
            return vt_message_id::key_left;
        case U'H':
            _should_echo = false;
            return vt_message_id::key_home;
        case U'F':
            _should_echo = false;
            return vt_message_id::key_end;
        case U'P':
            _should_echo = false;
            return vt_message_id::key_f1;
        case U'Q':
            _should_echo = false;
            return vt_message_id::key_f2;
        case U'R':
            _should_echo = false;
            return vt_message_id::key_f3;
        case U'S':
            _should_echo = false;
            return vt_message_id::key_f4;
        default:
            return vt_message_id::continue_;
        }
    }

    // 字符集选择 (ESC ( ...)
    vt_message_id _dispatch_charset(char32_t final)
    {
        if (final == U'0')
            return vt_message_id::designate_charset_line_drawing;
        if (final == U'B')
            return vt_message_id::designate_charset_ascii;
        return vt_message_id::continue_;
    }

    // ── OSC 载荷解析辅助函数（接受 u32string_view，无异常） ──

    // 解析十进制数（short），从 pos 处开始，遇非数字停止，失败返回 0
    short _parse_osc_decimal_8(std::u32string_view s, size_t &pos) const
    {
        // 跳过前导空格
        auto first = std::ranges::find_if(s.begin() + pos, s.end(), [](char32_t ch) {
            return ch != U' ';
        });
        pos = static_cast<size_t>(first - s.begin());
        size_t start = pos;
        // 收集连续的数字字符，最多 5 个（保证不溢出 short）
        const auto digits_last = s.begin() + std::min(s.size(), start + size_t{5});
        auto digits_end = std::ranges::find_if(s.begin() + start, digits_last, [](char32_t ch) {
            return ch < U'0' || ch > U'9';
        });
        pos = static_cast<size_t>(digits_end - s.begin());
        if (pos == start)
            return 0; // 无数字

        // 转换为窄字符数组供 std::from_chars 使用
        char numbuf[6]{};
        std::transform(s.begin() + start, digits_end, numbuf, [](char32_t ch) {
            return static_cast<char>(ch);
        });

        short value = 0;
        auto res = std::from_chars(numbuf, numbuf + (pos - start), value, 10);
        if (res.ec != std::errc{})
            return 0;
        return value;
    }

    // 解析两位十六进制数（uint8_t），从 pos 处开始，自动跳过 '/'
    uint8_t _parse_osc_hex_8(std::u32string_view buf, size_t &pos) const
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
        std::transform(buf.begin() + start, digits_end, hexbuf, [](char32_t ch) {
            return static_cast<char>(ch);
        });

        // 跳过可能存在的分隔符 '/'
        auto slash_end = std::ranges::find_if(buf.begin() + pos, buf.end(), [](char32_t ch) {
            return ch != U'/';
        });
        pos = static_cast<size_t>(slash_end - buf.begin());

        uint8_t value = 0;
        auto res = std::from_chars(hexbuf, hexbuf + (pos - start), value, 16);
        if (res.ec != std::errc{})
            return 0;
        return value;
    }

    // ── OSC 序列分发 ──
    vt_message_id _dispatch_osc()
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
                    return vt_message_id::continue_;
                return vt_message_id::set_palette_color;
            }
            return vt_message_id::continue_;
        }
        default:
            return vt_message_id::continue_;
        }
    }

    // ── CSI 终态分发 ──
    vt_message_id _dispatch_csi(char32_t terminator)
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
        // 相对光标移动（来自终端键盘），不回显原始字节：dispatch 生成钳制 CUP
        case U'A':
            if (priv)
                return vt_message_id::continue_;
            m.payload.count.value = _clamp(n);
            _should_echo = false;
            return vt_message_id::cursor_up;
        case U'B':
            if (priv)
                return vt_message_id::continue_;
            m.payload.count.value = _clamp(n);
            _should_echo = false;
            return vt_message_id::cursor_down;
        case U'C':
            if (priv)
                return vt_message_id::continue_;
            m.payload.count.value = _clamp(n);
            _should_echo = false;
            return vt_message_id::cursor_forward;
        case U'D':
            if (priv)
                return vt_message_id::continue_;
            m.payload.count.value = _clamp(n);
            _should_echo = false;
            return vt_message_id::cursor_backward;
        case U'E':
            if (priv)
                return vt_message_id::continue_;
            m.payload.count.value = _clamp(n);
            _should_echo = false;
            return vt_message_id::cursor_next_line;
        case U'F':
            if (priv)
                return vt_message_id::continue_;
            m.payload.count.value = _clamp(n);
            _should_echo = false;
            return vt_message_id::cursor_prev_line;
        // 绝对/坐标型光标定位（来自应用程序输出），不回显原始字节：dispatch 生成钳制后的 CUP
        case U'G':
            if (priv)
                return vt_message_id::continue_;
            m.payload.position.col = _clamp(_get_param(0, 1));
            _should_echo = false;
            return vt_message_id::cursor_horiz_absolute;
        case U'd':
            if (priv)
                return vt_message_id::continue_;
            m.payload.position.row = _clamp(_get_param(0, 1));
            _should_echo = false;
            return vt_message_id::cursor_vert_absolute;
        case U'H':
        case U'f':
            if (priv)
                return vt_message_id::continue_;
            m.payload.position.row = _clamp(_get_param(0, 1));
            m.payload.position.col = _clamp(_get_param(1, 1));
            _should_echo = false;
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
            for (size_t i = 0; i < _param_index; ++i)
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
                        m.payload.sgr.fg.set_rgb(static_cast<uint8_t>(_params[i + 2]), static_cast<uint8_t>(_params[i + 3]),
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
                        m.payload.sgr.bg.set_rgb(static_cast<uint8_t>(_params[i + 2]), static_cast<uint8_t>(_params[i + 3]),
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
                return (m.payload.cpr.row > 0 && m.payload.cpr.col > 0) ? vt_message_id::cpr_response : vt_message_id::continue_;
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

} // namespace conpty
