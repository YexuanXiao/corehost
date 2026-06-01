// ── conpty/conpty_vt_parser.hpp ──────────────────────────
// Layer 2: VT/CSI/OSC 序列解析器 (char32_t 输入, 零分配视图)
//
// 设计目标：
//   · 完全以 Unicode 码点 (char32_t) 作为输入，调用方负责 UTF-8 解码。
//   · 不丢弃任何输入字符；无法识别的序列以 unknown_sequence 返回，
//     msg.text 指向完整原文。
//   · vt_message 中的 title / text 为 std::u32string_view，指向内部缓冲区，
//     零拷贝，不产生额外的字符串内存分配。
//   · parse() 返回 vt_message_id，continue_ 表示尚未完成，text 表示文本消息，
//     unknown_sequence 表示未知/错误序列，其余为具体控制序列 id。
//   · 提供 reset(id) 方法，根据已消费的消息类型精确重置受污染的字段，
//     对于 text/unknown/title 消息还会清空内部缓冲区 _raw，兼顾性能与内存。
//
// 使用方式：
//   std::u32string raw;
//   vt_parser p{raw};
//   for (char32_t ch : code_points) {
//       vt_message_id id = p.parse(ch);
//       if (id != vt_message_id::continue_) {
//           auto &m = p.get();
//           switch (id) {
//               case vt_message_id::text:
//                   // 使用 m.text (u32string_view)
//                   break;
//               case vt_message_id::unknown_sequence:
//                   // 使用 m.text (完整未知序列)
//                   break;
//               case vt_message_id::set_window_title:
//                   // 使用 m.title (u32string_view)
//                   break;
//               ...
//           }
//           p.reset(id);   // 消费后精确清理
//       }
//   }
//
#pragma once
#include <cstdint>
#include <charconv>
#include <string>
#include <string_view>
#include <array>

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
    unknown_sequence,  // 无法识别或语法错误的 ESC/CSI/OSC/SS3 序列，msg.text 为完整原文
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
    resize_window, // \x1b[8;height;width t — terminal resize notification
    win32_input_key, // \x1b[Vk;Sc;Uc;Kd;Cs;Rc_ — Win32 Input Mode 键盘事件
    cpr_response,    // \x1b[Pl;PcR — 终端对 DSR CPR 的应答
    sgr,
};

// ── vt_message ───────────────────────────────────────
// 解析出的单条消息。所有字段均为值语义或指向内部缓冲区的视图。
// 视图仅在下一次 parse() 调用或 reset() 前有效。
struct vt_message
{
    // ── bool 标志组 ──
    bool sgr_reset = false;     // SGR 复位
    bool bold = false;          // 粗体
    bool faint = false;         // 弱化
    bool italic = false;        // 斜体
    bool underline = false;     // 下划线
    bool blink = false;         // 闪烁
    bool negative = false;      // 反显
    bool conceal = false;       // 隐藏
    bool strikethrough = false; // 删除线
    bool fg_is_default = false; // 前景色为默认
    bool bg_is_default = false; // 背景色为默认
    bool fg_is_rgb = false;     // 前景色为 RGB 直接色
    bool bg_is_rgb = false;     // 背景色为 RGB 直接色
    bool ctrl_mod = false;      // Ctrl 修饰键（键盘输入）

    // ── short 数值参数组 ──
    short count = 1;         // 重复次数（移动、删除、插入等）
    short row = 1;           // 行号 (1-based, CUP/HVP/VPA)
    short col = 1;           // 列号 (1-based, CUP/HVP/CHA)
    short erase_mode = 0;    // 擦除模式 (ED/EL)
    short scroll_top = 1;    // 滚动区域上边距
    short scroll_bottom = 0; // 滚动区域下边距 (0 = 视口底部)
    short fg_color = -1;     // 前景色索引 (-1 = 未设置)
    short bg_color = -1;     // 背景色索引
    short cursor_shape = 0;  // 光标形状 (DECSCUSR)
    short palette_index = 0; // 调色板索引 (OSC 4)
    short window_width = 0;  // 屏幕宽度 (80 或 132)
    short resize_rows = 0;   // \x1b[8;rows;cols t 的 rows
    short resize_cols = 0;   // \x1b[8;rows;cols t 的 cols

    // ── uint8_t RGB 分量组 ──
    uint8_t fg_r = 0, fg_g = 0, fg_b = 0;                // 前景 RGB 颜色
    uint8_t bg_r = 0, bg_g = 0, bg_b = 0;                // 背景 RGB 颜色
    uint8_t palette_r = 0, palette_g = 0, palette_b = 0; // 调色板 RGB

    // ── 字符串视图（零拷贝） ──
    std::u32string_view title; // 窗口标题内容 (仅 set_window_title 时有效)
    std::u32string_view text;  // 普通文本 / 非法序列原文

    // ── Win32 Input Mode 键盘字段 (win32_input_key) ──
    unsigned short win32_vk = 0; // 虚拟键码
    unsigned short win32_sc = 0; // 扫描码
    wchar_t win32_uc = 0;        // Unicode 字符
    bool win32_kd = false;       // TRUE=按下, FALSE=释放
    unsigned long win32_cs = 0;  // ControlKeyState
    unsigned short win32_rc = 1; // 重复次数

    // ── CPR 应答字段 (cpr_response) ──
    short cpr_row = 0; // 终端汇报的行号 (1-based)
    short cpr_col = 0; // 终端汇报的列号 (1-based)
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
    explicit vt_parser(std::u32string &raw) : _raw(raw)
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

    [[nodiscard]] size_t consume_ground_text_run(std::u32string_view text)
    {
        if (_pending_control != vt_message_id::continue_ || _mode != parser_mode::ground || text.empty())
            return 0;

        size_t count = 0;
        while (count < text.size() && _is_ground_printable(text[count]))
            ++count;
        if (count == 0)
            return 0;

        if (_ground_text_start == npos)
            _ground_text_start = _raw.size();
        _raw.append(text.data(), count);
        _should_echo = true;
        return count;
    }

    // 释放累积文本为 text 消息并返回 text id；无残留文本时返回 continue_
    [[nodiscard]] vt_message_id flush_text()
    {
        // flush_text 只交付地面态累积文本；正在解析 ESC/CSI/OSC 时不能调用它
        // 来强行结束序列，否则非法序列会丢失原文。
        if (_ground_text_start == npos || _raw.empty())
            return vt_message_id::continue_;
        _msg.text = {_raw.data() + _ground_text_start, _raw.size() - _ground_text_start};
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
            _msg.text = {};
            if (id == vt_message_id::cursor_forward_tab)
                _msg.count = 1;
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
            _raw += U'\x1B';
            _seq_start = _raw.size() - 1;
            _mode = parser_mode::esc;
        }

        // 所有字符先写入 _raw。合法序列用结构化字段返回；非法序列则用
        // _raw 的切片作为 text 透传，避免吞字节。
        _raw += ch;

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
                    _osc_buf[_osc_len++] = static_cast<char>(ch);
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
                    _msg.text = {_raw.data() + _ground_text_start, _raw.size() - 1 - _ground_text_start};
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
                    _msg.text = {_raw.data() + _ground_text_start, _raw.size() - _ground_text_start};
                    _ground_text_start = npos;
                    _pending_control = cid;
                    return vt_message_id::text;
                }
                _msg.text = {};
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
                    _msg.text = {_raw.data() + _ground_text_start, _raw.size() - 1 - _ground_text_start};
                    _raw.pop_back(); // 去掉 \\t，下次交付
                    _ground_text_start = npos;
                    _pending_control = vt_message_id::cursor_forward_tab;
                    return vt_message_id::text;
                }
                _raw.pop_back();
                _msg.count = 1;
                _msg.text = {};
                _ground_text_start = npos;
                return vt_message_id::cursor_forward_tab;
            }

            // ── 其他控制字符（BS→char_del, NUL→char_nul, SUB→char_sub, DEL→char_del）──
            // 有前导文本时先交付文本，控制字符延迟
            if (has_text)
            {
                _msg.text = {_raw.data() + _ground_text_start, _raw.size() - 1 - _ground_text_start};
                _raw.pop_back();
                _ground_text_start = npos;
                // 延迟交付控制字符
                _pending_control = _classify_control(ch);
                return vt_message_id::text;
            }
            _raw.pop_back();
            _ground_text_start = npos;
            _msg.text = {};
            return _classify_control(ch);
        }

        // 可打印字符，继续累积
        return vt_message_id::continue_text;
    }

  public:
    // 根据已消费的消息类型重置受污染的字段与解析器状态。
    void reset(vt_message_id id)
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
        _msg.count = 1;
    }

    void _reset_row() noexcept
    {
        _msg.row = 1;
    }

    void _reset_col() noexcept
    {
        _msg.col = 1;
    }

    void _reset_position() noexcept
    {
        _msg.row = 1;
        _msg.col = 1;
    }

    void _reset_cursor_shape() noexcept
    {
        _msg.cursor_shape = 0;
    }

    void _reset_erase_mode() noexcept
    {
        _msg.erase_mode = 0;
    }

    void _reset_palette_color() noexcept
    {
        _msg.palette_index = 0;
        _msg.palette_r = _msg.palette_g = _msg.palette_b = 0;
    }

    void _reset_scrolling_region() noexcept
    {
        _msg.scroll_top = 1;
        _msg.scroll_bottom = 0;
    }

    void _reset_window_width() noexcept
    {
        _msg.window_width = 0;
    }

    void _reset_resize_window() noexcept
    {
        _msg.resize_rows = 0;
        _msg.resize_cols = 0;
    }

    void _reset_win32_input_key() noexcept
    {
        _msg.win32_vk = 0;
        _msg.win32_sc = 0;
        _msg.win32_uc = 0;
        _msg.win32_kd = false;
        _msg.win32_cs = 0;
        _msg.win32_rc = 1;
    }

    void _reset_cpr_response() noexcept
    {
        _msg.cpr_row = 0;
        _msg.cpr_col = 0;
    }

    void _reset_sgr() noexcept
    {
        _msg.sgr_reset = false;
        _msg.bold = false;
        _msg.faint = false;
        _msg.italic = false;
        _msg.underline = false;
        _msg.blink = false;
        _msg.negative = false;
        _msg.conceal = false;
        _msg.strikethrough = false;
        _msg.fg_color = -1;
        _msg.bg_color = -1;
        _msg.fg_is_default = false;
        _msg.bg_is_default = false;
        _msg.fg_is_rgb = false;
        _msg.bg_is_rgb = false;
        _msg.fg_r = _msg.fg_g = _msg.fg_b = 0;
        _msg.bg_r = _msg.bg_g = _msg.bg_b = 0;
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
    std::u32string &_raw;

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
    std::array<char, 32> _osc_buf{}; // OSC 4 调色板参数窄字符缓冲
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
        _msg.text = {_raw.data() + start, _raw.size() - start};
        _ground_text_start = _raw.size();
    }

    // 完成一个序列；continue_ 表示 dispatch 未识别，整段序列按 unknown 返回。
    vt_message_id _finish_seq(vt_message_id id)
    {
        if (id != vt_message_id::continue_)
        {
            _msg.text = {};            // 成功时 text 留空
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
        while (pos < s.size() && s[pos] == U' ')
            ++pos;
        size_t start = pos;
        // 收集连续的数字字符，最多 5 个（保证不溢出 short）
        while (pos < s.size() && s[pos] >= U'0' && s[pos] <= U'9' && (pos - start) < 5)
            ++pos;
        if (pos == start)
            return 0; // 无数字

        // 转换为窄字符数组供 std::from_chars 使用
        char numbuf[6]{};
        for (size_t i = 0; i < pos - start; ++i)
            numbuf[i] = static_cast<char>(s[start + i]);

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
        while (pos < buf.size() && (pos - start) < 2 &&
               ((buf[pos] >= U'0' && buf[pos] <= U'9') || (buf[pos] >= U'A' && buf[pos] <= U'F') ||
                (buf[pos] >= U'a' && buf[pos] <= U'f')))
            ++pos;
        if (pos == start)
            return 0; // 无有效十六进制字符

        // 转换为窄字符数组
        char hexbuf[3]{};
        for (size_t i = 0; i < pos - start; ++i)
            hexbuf[i] = static_cast<char>(buf[start + i]);

        // 跳过可能存在的分隔符 '/'
        while (pos < buf.size() && buf[pos] == U'/')
            ++pos;

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
        size_t end_pos = std::u32string_view::npos;
        for (size_t i = pos; i < seq.size(); ++i)
        {
            if (seq[i] == 0x07)
            {
                end_pos = i;
                break;
            }
            if (seq[i] == 0x1B && i + 1 < seq.size() && seq[i + 1] == U'\\')
            {
                end_pos = i;
                break;
            }
        }
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
            m.title = payload; // 直接指向原始缓冲区
            return vt_message_id::set_window_title;

        case 4: {
            // 设置调色板颜色：索引;rgb:r/g/b
            size_t p = 0;
            m.palette_index = _parse_osc_decimal_8(payload, p);
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
                m.palette_r = _parse_osc_hex_8(payload, p);
                m.palette_g = _parse_osc_hex_8(payload, p);
                m.palette_b = _parse_osc_hex_8(payload, p);
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
            m.count = _clamp(n);
            _should_echo = false;
            return vt_message_id::cursor_up;
        case U'B':
            if (priv)
                return vt_message_id::continue_;
            m.count = _clamp(n);
            _should_echo = false;
            return vt_message_id::cursor_down;
        case U'C':
            if (priv)
                return vt_message_id::continue_;
            m.count = _clamp(n);
            _should_echo = false;
            return vt_message_id::cursor_forward;
        case U'D':
            if (priv)
                return vt_message_id::continue_;
            m.count = _clamp(n);
            _should_echo = false;
            return vt_message_id::cursor_backward;
        case U'E':
            if (priv)
                return vt_message_id::continue_;
            m.count = _clamp(n);
            _should_echo = false;
            return vt_message_id::cursor_next_line;
        case U'F':
            if (priv)
                return vt_message_id::continue_;
            m.count = _clamp(n);
            _should_echo = false;
            return vt_message_id::cursor_prev_line;
        // 绝对/坐标型光标定位（来自应用程序输出），不回显原始字节：dispatch 生成钳制后的 CUP
        case U'G':
            if (priv)
                return vt_message_id::continue_;
            m.col = _clamp(_get_param(0, 1));
            _should_echo = false;
            return vt_message_id::cursor_horiz_absolute;
        case U'd':
            if (priv)
                return vt_message_id::continue_;
            m.row = _clamp(_get_param(0, 1));
            _should_echo = false;
            return vt_message_id::cursor_vert_absolute;
        case U'H':
        case U'f':
            if (priv)
                return vt_message_id::continue_;
            m.row = _clamp(_get_param(0, 1));
            m.col = _clamp(_get_param(1, 1));
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
            m.count = _clamp(n);
            return vt_message_id::scroll_up;
        case U'T':
            m.count = _clamp(n);
            return vt_message_id::scroll_down;

        // 窗口操作 (CSI … t)
        case U't': {
            // CSI 8 ; height ; width t — resize text area (WT sends this on resize)
            auto p0 = _get_param(0);
            if (p0 == 8)
            {
                m.resize_rows = _clamp(_get_param(1));
                m.resize_cols = _clamp(_get_param(2));
                return (m.resize_rows > 0 && m.resize_cols > 0) ? vt_message_id::resize_window
                                                                : vt_message_id::continue_;
            }
            // CSI 4 ; height ; width t — resize in pixels (not supported, become text)
            return vt_message_id::continue_;
        }

        // 文本修改
        case U'@':
            m.count = _clamp(n);
            return vt_message_id::insert_characters;
        case U'P':
            m.count = _clamp(n);
            return vt_message_id::delete_characters;
        case U'X':
            m.count = _clamp(n);
            return vt_message_id::erase_characters;
        case U'L':
            m.count = _clamp(n);
            return vt_message_id::insert_lines;
        case U'M':
            m.count = _clamp(n);
            return vt_message_id::delete_lines;
        case U'J':
            m.erase_mode = _clamp(_get_param(0));
            return vt_message_id::erase_in_display;
        case U'K':
            m.erase_mode = _clamp(_get_param(0));
            return vt_message_id::erase_in_line;

        // SGR
        case U'm':
            m.sgr_reset = false;
            m.bold = false;
            m.faint = false;
            m.italic = false;
            m.underline = false;
            m.blink = false;
            m.negative = false;
            m.conceal = false;
            m.strikethrough = false;
            m.fg_color = -1;
            m.bg_color = -1;
            m.fg_is_default = false;
            m.bg_is_default = false;
            m.fg_is_rgb = false;
            m.bg_is_rgb = false;
            for (size_t i = 0; i < _param_index; ++i)
            {
                short v = _params[i];
                if (v == 0)
                    m.sgr_reset = true;
                else if (v == 1)
                    m.bold = true;
                else if (v == 2)
                    m.faint = true;
                else if (v == 3)
                    m.italic = true;
                else if (v == 4)
                    m.underline = true;
                else if (v == 5)
                    m.blink = true;
                else if (v == 7)
                    m.negative = true;
                else if (v == 8)
                    m.conceal = true;
                else if (v == 9)
                    m.strikethrough = true;
                else if (v == 22)
                {
                    m.bold = false;
                    m.faint = false;
                }
                else if (v == 23)
                    m.italic = false;
                else if (v == 24)
                    m.underline = false;
                else if (v == 25)
                    m.blink = false;
                else if (v == 27)
                    m.negative = false;
                else if (v == 28)
                    m.conceal = false;
                else if (v == 29)
                    m.strikethrough = false;
                else if (v >= 30 && v <= 37)
                    m.fg_color = v - 30;
                else if (v == 38 && i + 1 < _param_index)
                {
                    if (_params[i + 1] == 5 && i + 2 < _param_index)
                    {
                        m.fg_color = _params[i + 2];
                        i += 2;
                    }
                    else if (_params[i + 1] == 2 && i + 4 < _param_index)
                    {
                        m.fg_is_rgb = true;
                        m.fg_r = static_cast<uint8_t>(_params[i + 2]);
                        m.fg_g = static_cast<uint8_t>(_params[i + 3]);
                        m.fg_b = static_cast<uint8_t>(_params[i + 4]);
                        i += 4;
                    }
                    else
                        return vt_message_id::continue_;
                }
                else if (v == 39)
                {
                    m.fg_is_default = true;
                    m.fg_color = -1;
                }
                else if (v >= 40 && v <= 47)
                    m.bg_color = v - 40;
                else if (v == 48 && i + 1 < _param_index)
                {
                    if (_params[i + 1] == 5 && i + 2 < _param_index)
                    {
                        m.bg_color = _params[i + 2];
                        i += 2;
                    }
                    else if (_params[i + 1] == 2 && i + 4 < _param_index)
                    {
                        m.bg_is_rgb = true;
                        m.bg_r = static_cast<uint8_t>(_params[i + 2]);
                        m.bg_g = static_cast<uint8_t>(_params[i + 3]);
                        m.bg_b = static_cast<uint8_t>(_params[i + 4]);
                        i += 4;
                    }
                    else
                        return vt_message_id::continue_;
                }
                else if (v == 49)
                {
                    m.bg_is_default = true;
                    m.bg_color = -1;
                }
                else if (v >= 90 && v <= 97)
                    m.fg_color = v - 90 + 8;
                else if (v >= 100 && v <= 107)
                    m.bg_color = v - 100 + 8;
            }
            return vt_message_id::sgr;

        // 光标形状
        case U'q':
            if (_intermediate == ' ')
            {
                m.cursor_shape = _clamp(_get_param(0));
                return vt_message_id::set_cursor_shape;
            }
            return vt_message_id::continue_;

        // 制表符
        case U'I':
            m.count = _clamp(n);
            return vt_message_id::cursor_forward_tab;
        case U'Z':
            m.count = _clamp(n);
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
            m.scroll_top = _clamp(_get_param(0, 1));
            m.scroll_bottom = _clamp(_get_param(1, 0));
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
                m.cpr_row = _clamp(_get_param(0, 1));
                m.cpr_col = _clamp(_get_param(1, 1));
                return (m.cpr_row > 0 && m.cpr_col > 0) ? vt_message_id::cpr_response
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
            m.win32_vk = static_cast<unsigned short>(_clamp(_get_param(0, 0)));
            m.win32_sc = static_cast<unsigned short>(_clamp(_get_param(1, 0)));
            m.win32_uc = static_cast<wchar_t>(_clamp(_get_param(2, 0)));
            m.win32_kd = _get_param(3, 0) != 0;
            m.win32_cs = static_cast<unsigned long>(_clamp(_get_param(4, 0)));
            m.win32_rc = static_cast<unsigned short>(_clamp(_get_param(5, 1)));
            if (m.win32_rc == 0)
                m.win32_rc = 1;
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
