// ── tests/test_vt_parser.cpp ──────────────────────────────
// VT/CSI/OSC 解析器测试 (旧版, 基于字节流输入)
//
// 1. 正向测试 (positive): 随机生成正确的 vt_message -> 序列化为字节流 ->
//    解析器解析 -> 验证每个解析出的消息与原始消息一致。
// 2. 反向测试 (negative): 随机生成错误字节序列 -> 解析器解析 ->
//    验证 bad() 返回预期的 parse_bad_state。
//
// 注: 新版 UTF-32 解析器测试见 test_vt_parser_utf32.cpp
#include "test_common.hpp"
#include "conpty_vt_parser.hpp"
#include "utility/crtdbg.hpp"

#include <random>
#include <string>
#include <vector>
#include <sstream>
#include <iomanip>

using namespace conpty;

// ============================================================================
// 随机数工具
// ============================================================================
std::mt19937 rng(42); // 固定种子，可重现

short rand_short(short lo, short hi)
{
    return static_cast<short>(std::uniform_int_distribution<int>(lo, hi)(rng));
}

uint8_t rand_u8(uint8_t lo = 0, uint8_t hi = 255)
{
    return static_cast<uint8_t>(std::uniform_int_distribution<int>(lo, hi)(rng));
}

wchar_t rand_ascii_wchar()
{
    return static_cast<wchar_t>(rand_u8(0x20, 0x7E)); // printable ASCII
}

// ============================================================================
// 1. 正向测试：消息生成 → 序列化 → 解析 → 验证
// ============================================================================

// ── 序列化输出部件 ──
struct raw_seq
{
    std::vector<uint8_t> bytes;

    void add(uint8_t b)
    {
        bytes.push_back(b);
    }
    void add_str(const std::string &s)
    {
        for (auto c : s)
            add(static_cast<uint8_t>(c));
    }
    void add_number(short n)
    {
        if (n == 0)
        {
            add('0');
            return;
        }
        std::string s;
        while (n > 0)
        {
            s.insert(s.begin(), static_cast<char>('0' + (n % 10)));
            n /= 10;
        }
        add_str(s);
    }
    void add_esc()
    {
        add(0x1B);
    }
    void add_csi()
    {
        add_esc();
        add('[');
    }
    void add_osc()
    {
        add_esc();
        add(']');
    }
    void add_st()
    {
        add_esc();
        add('\\');
    }
    void add_wstr(const std::wstring &ws)
    {
        // Encode wstring as UTF-8
        for (auto w : ws)
        {
            if (w < 0x80)
                add(static_cast<uint8_t>(w));
            else if (w < 0x800)
            {
                add(static_cast<uint8_t>(0xC0 | (w >> 6)));
                add(static_cast<uint8_t>(0x80 | (w & 0x3F)));
            }
            else
            {
                add(static_cast<uint8_t>(0xE0 | (w >> 12)));
                add(static_cast<uint8_t>(0x80 | ((w >> 6) & 0x3F)));
                add(static_cast<uint8_t>(0x80 | (w & 0x3F)));
            }
        }
    }
};

// ── 随机消息生成器 ──
struct msg_gen
{
    vt_message msg;

    // 生成一个随机 vt_message 并填充 msg，同时返回序列化字节流
    raw_seq serialize()
    {
        raw_seq r;
        msg = vt_message{}; // 清空

        // 随机选择消息类型，均匀分布
        int choice = std::uniform_int_distribution<int>(0, 51)(rng);
        switch (choice)
        {
        // ── Simple ESC ──
        case 0:
            msg.id = vt_message_id::reverse_index;
            r.add_esc();
            r.add('M');
            break;
        case 1:
            msg.id = vt_message_id::save_cursor;
            r.add_esc();
            r.add('7');
            break;
        case 2:
            msg.id = vt_message_id::restore_cursor;
            r.add_esc();
            r.add('8');
            break;
        case 3:
            msg.id = vt_message_id::horizontal_tab_set;
            r.add_esc();
            r.add('H');
            break;
        case 4:
            msg.id = vt_message_id::keypad_app_mode;
            r.add_esc();
            r.add('=');
            break;
        case 5:
            msg.id = vt_message_id::keypad_numeric_mode;
            r.add_esc();
            r.add('>');
            break;

        // ── Character Set ──
        case 6:
            msg.id = vt_message_id::designate_charset_line_drawing;
            r.add_esc();
            r.add('(');
            r.add('0');
            break;
        case 7:
            msg.id = vt_message_id::designate_charset_ascii;
            r.add_esc();
            r.add('(');
            r.add('B');
            break;

        // ── CSI Cursor Movement ──
        case 8:
            msg.id = vt_message_id::cursor_up;
            msg.count = rand_short(1, 99);
            r.add_csi();
            r.add_number(msg.count);
            r.add('A');
            break;
        case 9:
            msg.id = vt_message_id::cursor_down;
            msg.count = rand_short(1, 99);
            r.add_csi();
            r.add_number(msg.count);
            r.add('B');
            break;
        case 10:
            msg.id = vt_message_id::cursor_forward;
            msg.count = rand_short(1, 99);
            r.add_csi();
            r.add_number(msg.count);
            r.add('C');
            break;
        case 11:
            msg.id = vt_message_id::cursor_backward;
            msg.count = rand_short(1, 99);
            r.add_csi();
            r.add_number(msg.count);
            r.add('D');
            break;
        case 12:
            msg.id = vt_message_id::cursor_next_line;
            msg.count = rand_short(1, 99);
            r.add_csi();
            r.add_number(msg.count);
            r.add('E');
            break;
        case 13:
            msg.id = vt_message_id::cursor_prev_line;
            msg.count = rand_short(1, 99);
            r.add_csi();
            r.add_number(msg.count);
            r.add('F');
            break;
        case 14:
            msg.id = vt_message_id::cursor_horiz_absolute;
            msg.col = rand_short(1, 200);
            r.add_csi();
            r.add_number(msg.col);
            r.add('G');
            break;
        case 15:
            msg.id = vt_message_id::cursor_vert_absolute;
            msg.row = rand_short(1, 200);
            r.add_csi();
            r.add_number(msg.row);
            r.add('d');
            break;
        case 16:
            msg.id = vt_message_id::cursor_position;
            msg.row = rand_short(1, 200);
            msg.col = rand_short(1, 200);
            r.add_csi();
            r.add_number(msg.row);
            r.add(';');
            r.add_number(msg.col);
            r.add('H');
            break;
        case 17:
            msg.id = vt_message_id::cursor_position; // HVP variant
            msg.row = rand_short(1, 200);
            msg.col = rand_short(1, 200);
            r.add_csi();
            r.add_number(msg.row);
            r.add(';');
            r.add_number(msg.col);
            r.add('f');
            break;
        case 18:
            msg.id = vt_message_id::ansi_save_cursor;
            r.add_csi();
            r.add('s');
            break;
        case 19:
            msg.id = vt_message_id::ansi_restore_cursor;
            r.add_csi();
            r.add('u');
            break;

        // ── Cursor Visibility / DEC Private h/l ──
        case 20:
            msg.id = vt_message_id::cursor_enable_blinking;
            r.add_csi();
            r.add('?');
            r.add_str("12");
            r.add('h');
            break;
        case 21:
            msg.id = vt_message_id::cursor_disable_blinking;
            r.add_csi();
            r.add('?');
            r.add_str("12");
            r.add('l');
            break;
        case 22:
            msg.id = vt_message_id::cursor_show;
            r.add_csi();
            r.add('?');
            r.add_str("25");
            r.add('h');
            break;
        case 23:
            msg.id = vt_message_id::cursor_hide;
            r.add_csi();
            r.add('?');
            r.add_str("25");
            r.add('l');
            break;
        case 24:
            msg.id = vt_message_id::cursor_keys_app_mode;
            r.add_csi();
            r.add('?');
            r.add('1');
            r.add('h');
            break;
        case 25:
            msg.id = vt_message_id::cursor_keys_normal_mode;
            r.add_csi();
            r.add('?');
            r.add('1');
            r.add('l');
            break;
        case 26:
            msg.id = vt_message_id::set_columns_132;
            r.add_csi();
            r.add('?');
            r.add('3');
            r.add('h');
            msg.window_width = 132;
            break;
        case 27:
            msg.id = vt_message_id::set_columns_80;
            r.add_csi();
            r.add('?');
            r.add('3');
            r.add('l');
            msg.window_width = 80;
            break;
        case 28:
            msg.id = vt_message_id::use_alternate_buffer;
            r.add_csi();
            r.add('?');
            r.add_str("1049");
            r.add('h');
            break;
        case 29:
            msg.id = vt_message_id::use_main_buffer;
            r.add_csi();
            r.add('?');
            r.add_str("1049");
            r.add('l');
            break;

        // ── Cursor Shape ──
        case 30:
            msg.id = vt_message_id::set_cursor_shape;
            msg.cursor_shape = rand_short(0, 6);
            r.add_csi();
            r.add_number(msg.cursor_shape);
            r.add(' ');
            r.add('q');
            break;

        // ── Scroll ──
        case 31:
            msg.id = vt_message_id::scroll_up;
            msg.count = rand_short(1, 99);
            r.add_csi();
            r.add_number(msg.count);
            r.add('S');
            break;
        case 32:
            msg.id = vt_message_id::scroll_down;
            msg.count = rand_short(1, 99);
            r.add_csi();
            r.add_number(msg.count);
            r.add('T');
            break;

        // ── Text Modification ──
        case 33:
            msg.id = vt_message_id::insert_characters;
            msg.count = rand_short(1, 50);
            r.add_csi();
            r.add_number(msg.count);
            r.add('@');
            break;
        case 34:
            msg.id = vt_message_id::delete_characters;
            msg.count = rand_short(1, 50);
            r.add_csi();
            r.add_number(msg.count);
            r.add('P');
            break;
        case 35:
            msg.id = vt_message_id::erase_characters;
            msg.count = rand_short(1, 50);
            r.add_csi();
            r.add_number(msg.count);
            r.add('X');
            break;
        case 36:
            msg.id = vt_message_id::insert_lines;
            msg.count = rand_short(1, 50);
            r.add_csi();
            r.add_number(msg.count);
            r.add('L');
            break;
        case 37:
            msg.id = vt_message_id::delete_lines;
            msg.count = rand_short(1, 50);
            r.add_csi();
            r.add_number(msg.count);
            r.add('M');
            break;
        case 38:
            msg.id = vt_message_id::erase_in_display;
            msg.erase_mode = rand_short(0, 2);
            r.add_csi();
            r.add_number(msg.erase_mode);
            r.add('J');
            break;
        case 39:
            msg.id = vt_message_id::erase_in_line;
            msg.erase_mode = rand_short(0, 2);
            r.add_csi();
            r.add_number(msg.erase_mode);
            r.add('K');
            break;

        // ── SGR ──
        case 40: {
            msg.id = vt_message_id::sgr;
            r.add_csi();
            // 随机生成 1-8 个 SGR 参数
            int n_params = std::uniform_int_distribution<int>(1, 8)(rng);
            bool first = true;
            for (int i = 0; i < n_params; ++i)
            {
                if (!first)
                    r.add(';');
                first = false;
                int p = std::uniform_int_distribution<int>(0, 11)(rng);
                short sgr_val = 0;
                switch (p)
                {
                case 0:
                    sgr_val = 0;
                    msg.sgr_reset = true;
                    break;
                case 1:
                    sgr_val = 1;
                    msg.bold = true;
                    break;
                case 2:
                    sgr_val = 4;
                    msg.underline = true;
                    break;
                case 3:
                    sgr_val = 7;
                    msg.negative = true;
                    break;
                case 4:
                    sgr_val = 22;
                    msg.bold = false;
                    break;
                case 5:
                    sgr_val = 24;
                    msg.underline = false;
                    break;
                case 6:
                    sgr_val = 27;
                    msg.negative = false;
                    break;
                case 7:
                    sgr_val = static_cast<short>(30 + rand_short(0, 7));
                    msg.fg_color = sgr_val - 30;
                    break;
                case 8:
                    sgr_val = static_cast<short>(40 + rand_short(0, 7));
                    msg.bg_color = sgr_val - 40;
                    break;
                case 9:
                    sgr_val = 39;
                    msg.fg_is_default = true;
                    msg.fg_color = -1;
                    break;
                case 10:
                    sgr_val = 49;
                    msg.bg_is_default = true;
                    msg.bg_color = -1;
                    break;
                case 11:
                    // RGB extended
                    sgr_val = 48;
                    msg.bg_is_rgb = true;
                    msg.bg_r = rand_u8(0, 255);
                    msg.bg_g = rand_u8(0, 255);
                    msg.bg_b = rand_u8(0, 255);
                    r.add_number(sgr_val);
                    r.add(';');
                    r.add('2');
                    r.add(';');
                    r.add_number(msg.bg_r);
                    r.add(';');
                    r.add_number(msg.bg_g);
                    r.add(';');
                    r.add_number(msg.bg_b);
                    continue; // already added
                }
                r.add_number(sgr_val);
            }
            r.add('m');
        }
        break;

        // ── OSC Window Title ──
        case 41: {
            msg.id = vt_message_id::set_window_title;
            int len = rand_short(1, 30);
            for (int i = 0; i < len; ++i)
                msg.title += rand_ascii_wchar();
            int osc_code = std::uniform_int_distribution<int>(0, 1)(rng) ? 0 : 2;
            r.add_osc();
            r.add(static_cast<uint8_t>('0' + osc_code));
            r.add(';');
            r.add_wstr(msg.title);
            r.add(0x07); // BEL terminator
        }
        break;

        // ── OSC Palette ──
        case 42: {
            msg.id = vt_message_id::set_palette_color;
            msg.palette_index = rand_short(0, 255);
            msg.palette_r = rand_u8(0, 255);
            msg.palette_g = rand_u8(0, 255);
            msg.palette_b = rand_u8(0, 255);
            r.add_osc();
            r.add('4');
            r.add(';');
            r.add_number(msg.palette_index);
            r.add(';');
            char buf[32];
            snprintf(buf, sizeof(buf), "rgb:%02x/%02x/%02x", msg.palette_r, msg.palette_g, msg.palette_b);
            r.add_str(buf);
            r.add(0x07); // BEL terminator
        }
        break;

        // ── Tabs ──
        case 43:
            msg.id = vt_message_id::cursor_forward_tab;
            msg.count = rand_short(1, 20);
            r.add_csi();
            r.add_number(msg.count);
            r.add('I');
            break;
        case 44:
            msg.id = vt_message_id::cursor_backward_tab;
            msg.count = rand_short(1, 20);
            r.add_csi();
            r.add_number(msg.count);
            r.add('Z');
            break;
        case 45:
            msg.id = vt_message_id::tab_clear_current;
            r.add_csi();
            r.add('0');
            r.add('g');
            break;
        case 46:
            msg.id = vt_message_id::tab_clear_all;
            r.add_csi();
            r.add('3');
            r.add('g');
            break;

        // ── Scrolling Margins ──
        case 47:
            msg.id = vt_message_id::set_scrolling_region;
            msg.scroll_top = rand_short(1, 50);
            msg.scroll_bottom = rand_short(1, 50);
            r.add_csi();
            r.add_number(msg.scroll_top);
            r.add(';');
            r.add_number(msg.scroll_bottom);
            r.add('r');
            break;

        // ── Query ──
        case 48:
            msg.id = vt_message_id::report_cursor_position;
            r.add_csi();
            r.add('6');
            r.add('n');
            break;
        case 49:
            msg.id = vt_message_id::device_attributes;
            r.add_csi();
            r.add('0');
            r.add('c');
            break;

        // ── Soft Reset ──
        case 50:
            msg.id = vt_message_id::soft_reset;
            r.add_csi();
            r.add('!');
            r.add('p');
            break;

        // ── SS3 / CSI Input ──
        case 51: {
            // Random input key: SS3 or CSI
            if (std::uniform_int_distribution<int>(0, 1)(rng))
            {
                // SS3: ESC O X
                const char s_keys[] = {'A', 'B', 'C', 'D', 'H', 'F', 'P', 'Q', 'R', 'S'};
                const vt_message_id s_ids[] = {
                    vt_message_id::key_up,   vt_message_id::key_down, vt_message_id::key_right, vt_message_id::key_left,
                    vt_message_id::key_home, vt_message_id::key_end,  vt_message_id::key_f1,    vt_message_id::key_f2,
                    vt_message_id::key_f3,   vt_message_id::key_f4};
                int ki = rand_short(0, 9);
                msg.id = s_ids[ki];
                r.add_esc();
                r.add('O');
                r.add(static_cast<uint8_t>(s_keys[ki]));
            }
            else
            {
                // CSI ~
                const short c_codes[] = {2, 3, 5, 6, 15, 17, 18, 19, 20, 21, 23, 24};
                const vt_message_id c_ids[] = {
                    vt_message_id::key_insert,    vt_message_id::key_delete, vt_message_id::key_page_up,
                    vt_message_id::key_page_down, vt_message_id::key_f5,     vt_message_id::key_f6,
                    vt_message_id::key_f7,        vt_message_id::key_f8,     vt_message_id::key_f9,
                    vt_message_id::key_f10,       vt_message_id::key_f11,    vt_message_id::key_f12};
                int ki = rand_short(0, 11);
                msg.id = c_ids[ki];
                r.add_csi();
                r.add_number(c_codes[ki]);
                r.add('~');
            }
        }
        break;
        }
        return r;
    }

    // 验证解析出的消息与 this->msg 一致
    bool verify(const vt_message &parsed) const
    {
        ASSERT(parsed.id == msg.id);
        switch (msg.id)
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
            ASSERT(parsed.count == msg.count);
            break;
        case vt_message_id::cursor_horiz_absolute:
            ASSERT(parsed.col == msg.col);
            break;
        case vt_message_id::cursor_vert_absolute:
            ASSERT(parsed.row == msg.row);
            break;
        case vt_message_id::cursor_position:
            ASSERT(parsed.row == msg.row);
            ASSERT(parsed.col == msg.col);
            break;
        case vt_message_id::set_cursor_shape:
            ASSERT(parsed.cursor_shape == msg.cursor_shape);
            break;
        case vt_message_id::erase_in_display:
        case vt_message_id::erase_in_line:
            ASSERT(parsed.erase_mode == msg.erase_mode);
            break;
        case vt_message_id::sgr:
            ASSERT(parsed.sgr_reset == msg.sgr_reset);
            ASSERT(parsed.bold == msg.bold);
            ASSERT(parsed.underline == msg.underline);
            ASSERT(parsed.negative == msg.negative);
            ASSERT(parsed.fg_color == msg.fg_color);
            ASSERT(parsed.bg_color == msg.bg_color);
            ASSERT(parsed.fg_is_default == msg.fg_is_default);
            ASSERT(parsed.bg_is_default == msg.bg_is_default);
            if (msg.bg_is_rgb)
            {
                ASSERT(parsed.bg_is_rgb);
                ASSERT(parsed.bg_r == msg.bg_r);
                ASSERT(parsed.bg_g == msg.bg_g);
                ASSERT(parsed.bg_b == msg.bg_b);
            }
            break;
        case vt_message_id::set_window_title:
            ASSERT(parsed.title == msg.title);
            break;
        case vt_message_id::set_palette_color:
            ASSERT(parsed.palette_index == msg.palette_index);
            ASSERT(parsed.palette_r == msg.palette_r);
            ASSERT(parsed.palette_g == msg.palette_g);
            ASSERT(parsed.palette_b == msg.palette_b);
            break;
        case vt_message_id::set_scrolling_region:
            ASSERT(parsed.scroll_top == msg.scroll_top);
            ASSERT(parsed.scroll_bottom == msg.scroll_bottom);
            break;
        default:
            break; // No extra fields to verify
        }
        return true;
    }
};

// ── 正向测试：单条随机消息 → 序列化 → 解析 → 验证 ──
bool test_positive_single()
{
    vt_parser parser;
    for (int trial = 0; trial < 500; ++trial)
    {
        msg_gen gen;
        raw_seq seq = gen.serialize();

        // Feed all bytes
        bool got_message = false;
        for (size_t i = 0; i < seq.bytes.size(); ++i)
        {
            if (parser.parse(static_cast<char>(seq.bytes[i])))
            {
                got_message = true;
                ASSERT(parser.bad() == parse_bad_state::ok);
                if (!gen.verify(parser.get()))
                    return false;
                // 解析器内部状态在每次完整序列后自动重置，无需手动清理
            }
        }
        ASSERT(got_message);
    }
    return true;
}

// ── 正向测试：多条随机消息拼接为连续序列 → 逐条解析验证 ──
bool test_positive_concatenated()
{
    vt_parser parser;
    for (int trial = 0; trial < 200; ++trial)
    {
        // 生成 2-6 条随机消息
        int msg_count = rand_short(2, 6);
        std::vector<msg_gen> gens(msg_count);
        raw_seq seq;
        for (int i = 0; i < msg_count; ++i)
        {
            gens[i] = msg_gen{};
            raw_seq part = gens[i].serialize();
            seq.bytes.insert(seq.bytes.end(), part.bytes.begin(), part.bytes.end());
        }

        // 连续解析
        int parsed_idx = 0;
        for (size_t i = 0; i < seq.bytes.size(); ++i)
        {
            if (parser.parse(static_cast<char>(seq.bytes[i])))
            {
                ASSERT(parsed_idx < msg_count);
                ASSERT(parser.bad() == parse_bad_state::ok);
                if (!gens[parsed_idx].verify(parser.get()))
                    return false;
                ++parsed_idx;
            }
        }
        ASSERT(parsed_idx == msg_count);
    }
    return true;
}

// ── 正向测试：消息 + 文本穿插 ──
bool test_positive_with_text()
{
    vt_parser parser;
    for (int trial = 0; trial < 100; ++trial)
    {
        msg_gen gen;
        raw_seq seq = gen.serialize();

        // 在序列前后随机插入普通文本
        std::wstring prefix, suffix;
        int pre_len = rand_short(1, 10);
        int suf_len = rand_short(1, 10);
        for (int i = 0; i < pre_len; ++i)
            prefix += rand_ascii_wchar();
        for (int i = 0; i < suf_len; ++i)
            suffix += rand_ascii_wchar();

        raw_seq full;
        full.add_wstr(prefix);
        full.bytes.insert(full.bytes.end(), seq.bytes.begin(), seq.bytes.end());
        full.add_wstr(suffix);

        // 解析
        bool text_received = false;
        bool msg_received = false;
        for (size_t i = 0; i < full.bytes.size(); ++i)
        {
            if (parser.parse(static_cast<char>(full.bytes[i])))
            {
                if (parser.get().id == vt_message_id::none)
                {
                    // 文本块
                    text_received = true;
                }
                else
                {
                    msg_received = true;
                    ASSERT(parser.bad() == parse_bad_state::ok);
                    if (!gen.verify(parser.get()))
                        return false;
                }
            }
        }
        ASSERT(msg_received);
    }
    return true;
}

// ============================================================================
// 2. 反向测试：错误字节序列 → 预期 bad_state
// ============================================================================

bool test_bad(const std::vector<uint8_t> &bytes, parse_bad_state expected)
{
    vt_parser parser;
    bool got_result = false;
    for (auto b : bytes)
    {
        if (parser.parse(static_cast<char>(b)))
        {
            got_result = true;
            ASSERT(parser.bad() == expected);
            return true;
        }
    }
    // 某些 bad 情况仍然会返回消息（如 charset_unknown_final 在 parse 中返回 true 并设 bad）
    // 如果没触发完成则算失败
    ASSERT(got_result);
    return true;
}

bool test_negative_esc_unknown_final()
{
    return test_bad({0x1B, 'X'}, parse_bad_state::esc_unknown_final);
}

bool test_negative_charset_unknown()
{
    // ESC ( 后面不是 0 或 B
    return test_bad({0x1B, '(', 'C'}, parse_bad_state::charset_unknown_final);
}

bool test_negative_csi_unknown_final()
{
    // CSI <params> + 不在 0x40-0x7E 范围的终态 → 在 parse() 中被丢弃并设 bad
    // CSI 1 & → '&' is 0x26, not final → aborts CSI
    // Actually: CSI param reading detects '&' is below 0x40, unknown → _csi = false with bad
    // Let's try something that goes through: CSI + garbage byte
    vt_parser parser;
    // CSI 序列中途出现非法字符 → _csi 被中止，设 csi_unknown_final
    (void)parser.parse('\x1B'); // ESC
    (void)parser.parse('[');    // CSI
    (void)parser.parse('1');
    // '<' = 0x3C → 不在 0x20-0x2F 也不在 0x40-0x7E → 中止
    if (parser.parse('<'))
    {
        ASSERT(parser.bad() == parse_bad_state::csi_unknown_final);
        return true;
    }
    // parse returned false, check bad state
    ASSERT(parser.bad() == parse_bad_state::csi_unknown_final);
    return true;
}

bool test_negative_csi_private_cursor()
{
    // CSI ? n A — private marker on cursor movement is invalid
    return test_bad({0x1B, '[', '?', '3', 'A'}, parse_bad_state::csi_private_cursor);
}

bool test_negative_csi_private_unknown()
{
    // CSI ? 99 h — unknown DECSET value
    return test_bad({0x1B, '[', '?', '9', '9', 'h'}, parse_bad_state::csi_private_unknown);
}

bool test_negative_csi_param_overflow()
{
    // CSI with 17+ params → param overflow
    raw_seq seq;
    seq.add_csi();
    for (int i = 0; i < 17; ++i)
    {
        seq.add('1');
        seq.add(';');
    }
    seq.add('m');
    vt_parser parser;
    bool got = false;
    for (auto b : seq.bytes)
    {
        if (parser.parse(static_cast<char>(b)))
        {
            got = true;
            break;
        }
    }
    ASSERT(got);
    ASSERT(parser.bad() == parse_bad_state::csi_param_overflow);
    return true;
}

bool test_negative_ss3_unknown_final()
{
    // ESC O X where X is not a known key
    return test_bad({0x1B, 'O', 'X'}, parse_bad_state::ss3_unknown_final);
}

bool test_negative_decscusr_missing_sp()
{
    // CSI n q without intermediate SP (DECSCUSR requires SP)
    return test_bad({0x1B, '[', '3', 'q'}, parse_bad_state::decscusr_missing_sp);
}

bool test_negative_decstr_missing_bang()
{
    // CSI p without intermediate '!'
    return test_bad({0x1B, '[', 'p'}, parse_bad_state::decstr_missing_bang);
}

bool test_negative_decfnk_unknown_code()
{
    // CSI 99 ~ → unknown DECFNK code
    return test_bad({0x1B, '[', '9', '9', '~'}, parse_bad_state::decfnk_unknown_code);
}

bool test_negative_sgr_extended_truncated()
{
    // 38 without sufficient params
    return test_bad({0x1B, '[', '3', '8', ';', '5', 'm'}, parse_bad_state::sgr_extended_truncated);
}

bool test_negative_osc_unknown_code()
{
    // OSC 9 ; ... BEL
    raw_seq seq;
    seq.add_osc();
    seq.add('9');
    seq.add(';');
    seq.add_str("test");
    seq.add(0x07);
    vt_parser parser;
    for (auto b : seq.bytes)
    {
        if (parser.parse(static_cast<char>(b)))
            break;
    }
    ASSERT(parser.bad() == parse_bad_state::osc_unknown_code);
    return true;
}

bool test_negative_osc_buf_overflow()
{
    // OSC 4 with > 31 bytes of data
    raw_seq seq;
    seq.add_osc();
    seq.add('4');
    seq.add(';');
    for (int i = 0; i < 40; ++i)
        seq.add('x');
    seq.add(0x07);
    vt_parser parser;
    for (auto b : seq.bytes)
    {
        if (parser.parse(static_cast<char>(b)))
            break;
    }
    ASSERT(parser.bad() == parse_bad_state::osc_buf_overflow);
    return true;
}

bool test_negative_osc_palette_no_rgb()
{
    // OSC 4 ; 0 ; norgb:xxx
    raw_seq seq;
    seq.add_osc();
    seq.add('4');
    seq.add(';');
    seq.add('0');
    seq.add(';');
    seq.add_str("norgb:ff/00/ff");
    seq.add(0x07);
    vt_parser parser;
    for (auto b : seq.bytes)
    {
        if (parser.parse(static_cast<char>(b)))
            break;
    }
    ASSERT(parser.bad() == parse_bad_state::osc_palette_no_rgb);
    return true;
}

bool test_negative_multiple_garbage()
{
    // 混合多种错误 → 每种都得到正确 bad_state
    struct
    {
        std::vector<uint8_t> bytes;
        parse_bad_state expected;
    } cases[] = {
        {{0x1B, 'Y'}, parse_bad_state::esc_unknown_final},
        {{0x1B, '(', '9'}, parse_bad_state::charset_unknown_final},
        {{0x1B, '[', '?', '5', 'h'}, parse_bad_state::csi_private_unknown},
        {{0x1B, '[', '?', '2', 'A'}, parse_bad_state::csi_private_cursor},
    };

    for (auto &c : cases)
    {
        vt_parser parser;
        bool got = false;
        for (auto b : c.bytes)
        {
            if (parser.parse(static_cast<char>(b)))
            {
                got = true;
                break;
            }
        }
        ASSERT(got);
        ASSERT(parser.bad() == c.expected);
    }
    return true;
}

// ============================================================================
// 入口
// ============================================================================

int main()
{
    utility::suppress_crt_error_dialogs();
    std::wcout << L"VT Parser Positive Tests\n";
    RUN_TEST(test_positive_single, L"Random single message roundtrip");
    RUN_TEST(test_positive_concatenated, L"Random concatenated messages");
    RUN_TEST(test_positive_with_text, L"Messages with intervening text");

    std::wcout << L"\nVT Parser Negative Tests\n";
    RUN_TEST(test_negative_esc_unknown_final, L"ESC unknown final");
    RUN_TEST(test_negative_charset_unknown, L"Charset unknown final");
    RUN_TEST(test_negative_csi_unknown_final, L"CSI unknown final");
    RUN_TEST(test_negative_csi_private_cursor, L"CSI private cursor");
    RUN_TEST(test_negative_csi_private_unknown, L"CSI private unknown DECSET");
    RUN_TEST(test_negative_csi_param_overflow, L"CSI param overflow");
    RUN_TEST(test_negative_ss3_unknown_final, L"SS3 unknown final");
    RUN_TEST(test_negative_decscusr_missing_sp, L"DECSCUSR missing SP");
    RUN_TEST(test_negative_decstr_missing_bang, L"DECSTR missing bang");
    RUN_TEST(test_negative_decfnk_unknown_code, L"DECFNK unknown code");
    RUN_TEST(test_negative_sgr_extended_truncated, L"SGR extended truncated");
    RUN_TEST(test_negative_osc_unknown_code, L"OSC unknown code");
    RUN_TEST(test_negative_osc_buf_overflow, L"OSC buffer overflow");
    RUN_TEST(test_negative_osc_palette_no_rgb, L"OSC palette missing rgb prefix");
    RUN_TEST(test_negative_multiple_garbage, L"Multiple garbage sequences");

    std::wcout << L"\nTotal: " << (tests_passed + tests_failed) << L" | Passed: " << tests_passed << L" | Failed: "
               << tests_failed << std::endl;

    return tests_failed == 0 ? 0 : 1;
}
