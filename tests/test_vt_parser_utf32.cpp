// ── tests/test_vt_parser.cpp ──────────────────────────────
// VT/CSI/OSC 解析器测(基于 char32_t 的新版解析器)
//
// 与旧版测试的区别
//   旧版：输入字节流，有 bad_state，文本消id=none
//   新版：输char32_t 码点，无 bad_state，任何非法序列原样输出为
//         text 消息（id 返回 vt_message_id::text）
//   因此反向测试改为验证非法序列被无损地输出text 消息
//
#include "test_common.hpp"
#include "conpty_vt_parser.hpp"
#include "utility/crtdbg.hpp"

#include <random>
#include <string>
#include <vector>
#include <sstream>
#include <iomanip>
#include <cstring> // memcmp

using namespace conpty;

// ============================================================================
// 随机数工具（种子固定，可重现
// ============================================================================
std::mt19937 rng(42);

short rand_short(short lo, short hi)
{
    return static_cast<short>(std::uniform_int_distribution<int>(lo, hi)(rng));
}

uint8_t rand_u8(uint8_t lo = 0, uint8_t hi = 255)
{
    return static_cast<uint8_t>(std::uniform_int_distribution<int>(lo, hi)(rng));
}

// 生成可打ASCII 字符 (码点 0x20-0x7E)
char32_t rand_ascii()
{
    return static_cast<char32_t>(rand_u8(0x20, 0x7E));
}

// ============================================================================
// 1. 序列化：vt_message 转为 char32_t 序列
// ============================================================================

// 序列的载体：直接存储 char32_t 码点
struct raw_seq
{
    std::vector<char32_t> code_points;

    void add(char32_t c)
    {
        code_points.push_back(c);
    }
    void add_str(std::u32string_view sv)
    {
        for (auto c : sv)
            add(c);
    }
    void add_number(short n)
    {
        if (n == 0)
        {
            add(U'0');
            return;
        }
        std::u32string s;
        while (n > 0)
        {
            s.insert(s.begin(), static_cast<char32_t>(U'0' + (n % 10)));
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
        add(U'[');
    }
    void add_osc()
    {
        add_esc();
        add(U']');
    }
    void add_st()
    {
        add_esc();
        add(U'\\');
    }
    // 直接添加 u32string，用于标
    void add_u32string(const std::u32string &s)
    {
        add_str(s);
    }
    void add_u32string(std::u32string_view sv)
    {
        add_str(sv);
    }
};

// ── 随机消息生成──
struct msg_gen
{
    vt_message_id msg_id = vt_message_id::continue_; // 本次生成id
    vt_message msg;                                  // 期望的字段

    // 用于持有 msg.title / msg.text 所指向的字符串数据 (u32string_view 需存活)
    std::u32string _title_storage;
    std::u32string _text_storage;

    // 生成随机消息并填msg msg_id，返回序列化码点
    raw_seq serialize()
    {
        raw_seq r;
        msg = vt_message{};
        _title_storage.clear();
        _text_storage.clear();

        // 随机选择消息类型（共 52 种，与旧版一致）
        int choice = std::uniform_int_distribution<int>(0, 51)(rng);
        switch (choice)
        {
        // ── Simple ESC ──
        case 0:
            msg_id = vt_message_id::reverse_index;
            r.add_esc();
            r.add(U'M');
            break;
        case 1:
            msg_id = vt_message_id::save_cursor;
            r.add_esc();
            r.add(U'7');
            break;
        case 2:
            msg_id = vt_message_id::restore_cursor;
            r.add_esc();
            r.add(U'8');
            break;
        case 3:
            msg_id = vt_message_id::horizontal_tab_set;
            r.add_esc();
            r.add(U'H');
            break;
        case 4:
            msg_id = vt_message_id::keypad_app_mode;
            r.add_esc();
            r.add(U'=');
            break;
        case 5:
            msg_id = vt_message_id::keypad_numeric_mode;
            r.add_esc();
            r.add(U'>');
            break;

        // ── Character Set ──
        case 6:
            msg_id = vt_message_id::designate_charset_line_drawing;
            r.add_esc();
            r.add(U'(');
            r.add(U'0');
            break;
        case 7:
            msg_id = vt_message_id::designate_charset_ascii;
            r.add_esc();
            r.add(U'(');
            r.add(U'B');
            break;

        // ── CSI Cursor Movement ──
        case 8:
            msg_id = vt_message_id::cursor_up;
            msg.count = rand_short(1, 99);
            r.add_csi();
            r.add_number(msg.count);
            r.add(U'A');
            break;
        case 9:
            msg_id = vt_message_id::cursor_down;
            msg.count = rand_short(1, 99);
            r.add_csi();
            r.add_number(msg.count);
            r.add(U'B');
            break;
        case 10:
            msg_id = vt_message_id::cursor_forward;
            msg.count = rand_short(1, 99);
            r.add_csi();
            r.add_number(msg.count);
            r.add(U'C');
            break;
        case 11:
            msg_id = vt_message_id::cursor_backward;
            msg.count = rand_short(1, 99);
            r.add_csi();
            r.add_number(msg.count);
            r.add(U'D');
            break;
        case 12:
            msg_id = vt_message_id::cursor_next_line;
            msg.count = rand_short(1, 99);
            r.add_csi();
            r.add_number(msg.count);
            r.add(U'E');
            break;
        case 13:
            msg_id = vt_message_id::cursor_prev_line;
            msg.count = rand_short(1, 99);
            r.add_csi();
            r.add_number(msg.count);
            r.add(U'F');
            break;
        case 14:
            msg_id = vt_message_id::cursor_horiz_absolute;
            msg.col = rand_short(1, 200);
            r.add_csi();
            r.add_number(msg.col);
            r.add(U'G');
            break;
        case 15:
            msg_id = vt_message_id::cursor_vert_absolute;
            msg.row = rand_short(1, 200);
            r.add_csi();
            r.add_number(msg.row);
            r.add(U'd');
            break;
        case 16:
            msg_id = vt_message_id::cursor_position;
            msg.row = rand_short(1, 200);
            msg.col = rand_short(1, 200);
            r.add_csi();
            r.add_number(msg.row);
            r.add(U';');
            r.add_number(msg.col);
            r.add(U'H');
            break;
        case 17:
            msg_id = vt_message_id::cursor_position; // HVP variant
            msg.row = rand_short(1, 200);
            msg.col = rand_short(1, 200);
            r.add_csi();
            r.add_number(msg.row);
            r.add(U';');
            r.add_number(msg.col);
            r.add(U'f');
            break;
        case 18:
            msg_id = vt_message_id::ansi_save_cursor;
            r.add_csi();
            r.add(U's');
            break;
        case 19:
            msg_id = vt_message_id::ansi_restore_cursor;
            r.add_csi();
            r.add(U'u');
            break;

        // ── DEC Private h/l ──
        case 20:
            msg_id = vt_message_id::cursor_enable_blinking;
            r.add_csi();
            r.add(U'?');
            r.add_str(U"12");
            r.add(U'h');
            break;
        case 21:
            msg_id = vt_message_id::cursor_disable_blinking;
            r.add_csi();
            r.add(U'?');
            r.add_str(U"12");
            r.add(U'l');
            break;
        case 22:
            msg_id = vt_message_id::cursor_show;
            r.add_csi();
            r.add(U'?');
            r.add_str(U"25");
            r.add(U'h');
            break;
        case 23:
            msg_id = vt_message_id::cursor_hide;
            r.add_csi();
            r.add(U'?');
            r.add_str(U"25");
            r.add(U'l');
            break;
        case 24:
            msg_id = vt_message_id::cursor_keys_app_mode;
            r.add_csi();
            r.add(U'?');
            r.add(U'1');
            r.add(U'h');
            break;
        case 25:
            msg_id = vt_message_id::cursor_keys_normal_mode;
            r.add_csi();
            r.add(U'?');
            r.add(U'1');
            r.add(U'l');
            break;
        case 26:
            msg_id = vt_message_id::set_columns_132;
            r.add_csi();
            r.add(U'?');
            r.add(U'3');
            r.add(U'h');
            msg.window_width = 132;
            break;
        case 27:
            msg_id = vt_message_id::set_columns_80;
            r.add_csi();
            r.add(U'?');
            r.add(U'3');
            r.add(U'l');
            msg.window_width = 80;
            break;
        case 28:
            msg_id = vt_message_id::use_alternate_buffer;
            r.add_csi();
            r.add(U'?');
            r.add_str(U"1049");
            r.add(U'h');
            break;
        case 29:
            msg_id = vt_message_id::use_main_buffer;
            r.add_csi();
            r.add(U'?');
            r.add_str(U"1049");
            r.add(U'l');
            break;

        // ── Cursor Shape ──
        case 30:
            msg_id = vt_message_id::set_cursor_shape;
            msg.cursor_shape = rand_short(0, 6);
            r.add_csi();
            r.add_number(msg.cursor_shape);
            r.add(U' ');
            r.add(U'q');
            break;

        // ── Scroll ──
        case 31:
            msg_id = vt_message_id::scroll_up;
            msg.count = rand_short(1, 99);
            r.add_csi();
            r.add_number(msg.count);
            r.add(U'S');
            break;
        case 32:
            msg_id = vt_message_id::scroll_down;
            msg.count = rand_short(1, 99);
            r.add_csi();
            r.add_number(msg.count);
            r.add(U'T');
            break;

        // ── Text Modification ──
        case 33:
            msg_id = vt_message_id::insert_characters;
            msg.count = rand_short(1, 50);
            r.add_csi();
            r.add_number(msg.count);
            r.add(U'@');
            break;
        case 34:
            msg_id = vt_message_id::delete_characters;
            msg.count = rand_short(1, 50);
            r.add_csi();
            r.add_number(msg.count);
            r.add(U'P');
            break;
        case 35:
            msg_id = vt_message_id::erase_characters;
            msg.count = rand_short(1, 50);
            r.add_csi();
            r.add_number(msg.count);
            r.add(U'X');
            break;
        case 36:
            msg_id = vt_message_id::insert_lines;
            msg.count = rand_short(1, 50);
            r.add_csi();
            r.add_number(msg.count);
            r.add(U'L');
            break;
        case 37:
            msg_id = vt_message_id::delete_lines;
            msg.count = rand_short(1, 50);
            r.add_csi();
            r.add_number(msg.count);
            r.add(U'M');
            break;
        case 38:
            msg_id = vt_message_id::erase_in_display;
            msg.erase_mode = rand_short(0, 2);
            r.add_csi();
            r.add_number(msg.erase_mode);
            r.add(U'J');
            break;
        case 39:
            msg_id = vt_message_id::erase_in_line;
            msg.erase_mode = rand_short(0, 2);
            r.add_csi();
            r.add_number(msg.erase_mode);
            r.add(U'K');
            break;

        // ── SGR ──
        case 40: {
            msg_id = vt_message_id::sgr;
            r.add_csi();
            int n_params = std::uniform_int_distribution<int>(1, 8)(rng);
            bool first = true;
            for (int i = 0; i < n_params; ++i)
            {
                if (!first)
                    r.add(U';');
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
                    sgr_val = 48;
                    msg.bg_is_rgb = true;
                    msg.bg_r = rand_u8(0, 255);
                    msg.bg_g = rand_u8(0, 255);
                    msg.bg_b = rand_u8(0, 255);
                    r.add_number(sgr_val);
                    r.add(U';');
                    r.add(U'2');
                    r.add(U';');
                    r.add_number(msg.bg_r);
                    r.add(U';');
                    r.add_number(msg.bg_g);
                    r.add(U';');
                    r.add_number(msg.bg_b);
                    continue;
                }
                r.add_number(sgr_val);
            }
            r.add(U'm');
        }
        break;

        // ── OSC Window Title ──
        case 41: {
            msg_id = vt_message_id::set_window_title;
            int len = rand_short(1, 30);
            for (int i = 0; i < len; ++i)
                _title_storage += rand_ascii();
            msg.title = _title_storage; // view into owned storage
            int osc_code = std::uniform_int_distribution<int>(0, 1)(rng) ? 0 : 2;
            r.add_osc();
            r.add(U'0' + osc_code);
            r.add(U';');
            r.add_str(_title_storage);
            r.add(0x07); // BEL terminator
        }
        break;

        // ── OSC Palette ──
        case 42: {
            msg_id = vt_message_id::set_palette_color;
            msg.palette_index = rand_short(0, 255);
            msg.palette_r = rand_u8(0, 255);
            msg.palette_g = rand_u8(0, 255);
            msg.palette_b = rand_u8(0, 255);
            r.add_osc();
            r.add(U'4');
            r.add(U';');
            r.add_number(msg.palette_index);
            r.add(U';');
            // 构"rgb:RR/GG/BB" 字符
            char buf[32];
            snprintf(buf, sizeof(buf), "rgb:%02x/%02x/%02x", msg.palette_r, msg.palette_g, msg.palette_b);
            for (char *p = buf; *p; ++p)
                r.add(static_cast<char32_t>(*p));
            r.add(0x07); // BEL terminator
        }
        break;

        // ── Tabs ──
        case 43:
            msg_id = vt_message_id::cursor_forward_tab;
            msg.count = rand_short(1, 20);
            r.add_csi();
            r.add_number(msg.count);
            r.add(U'I');
            break;
        case 44:
            msg_id = vt_message_id::cursor_backward_tab;
            msg.count = rand_short(1, 20);
            r.add_csi();
            r.add_number(msg.count);
            r.add(U'Z');
            break;
        case 45:
            msg_id = vt_message_id::tab_clear_current;
            r.add_csi();
            r.add(U'0');
            r.add(U'g');
            break;
        case 46:
            msg_id = vt_message_id::tab_clear_all;
            r.add_csi();
            r.add(U'3');
            r.add(U'g');
            break;

        // ── Scrolling Margins ──
        case 47:
            msg_id = vt_message_id::set_scrolling_region;
            msg.scroll_top = rand_short(1, 50);
            msg.scroll_bottom = rand_short(1, 50);
            r.add_csi();
            r.add_number(msg.scroll_top);
            r.add(U';');
            r.add_number(msg.scroll_bottom);
            r.add(U'r');
            break;

        // ── Query ──
        case 48:
            msg_id = vt_message_id::report_cursor_position;
            r.add_csi();
            r.add(U'6');
            r.add(U'n');
            break;
        case 49:
            msg_id = vt_message_id::device_attributes;
            r.add_csi();
            r.add(U'0');
            r.add(U'c');
            break;

        // ── Soft Reset ──
        case 50:
            msg_id = vt_message_id::soft_reset;
            r.add_csi();
            r.add(U'!');
            r.add(U'p');
            break;

        // ── SS3 / CSI Input ──
        case 51: {
            if (std::uniform_int_distribution<int>(0, 1)(rng))
            {
                // SS3: ESC O X
                static const char32_t s_keys[] = {U'A', U'B', U'C', U'D', U'H', U'F', U'P', U'Q', U'R', U'S'};
                static const vt_message_id s_ids[] = {
                    vt_message_id::key_up,   vt_message_id::key_down, vt_message_id::key_right, vt_message_id::key_left,
                    vt_message_id::key_home, vt_message_id::key_end,  vt_message_id::key_f1,    vt_message_id::key_f2,
                    vt_message_id::key_f3,   vt_message_id::key_f4};
                int ki = rand_short(0, 9);
                msg_id = s_ids[ki];
                r.add_esc();
                r.add(U'O');
                r.add(s_keys[ki]);
            }
            else
            {
                // CSI ~
                static const short c_codes[] = {2, 3, 5, 6, 15, 17, 18, 19, 20, 21, 23, 24};
                static const vt_message_id c_ids[] = {
                    vt_message_id::key_insert,    vt_message_id::key_delete, vt_message_id::key_page_up,
                    vt_message_id::key_page_down, vt_message_id::key_f5,     vt_message_id::key_f6,
                    vt_message_id::key_f7,        vt_message_id::key_f8,     vt_message_id::key_f9,
                    vt_message_id::key_f10,       vt_message_id::key_f11,    vt_message_id::key_f12};
                int ki = rand_short(0, 11);
                msg_id = c_ids[ki];
                r.add_csi();
                r.add_number(c_codes[ki]);
                r.add(U'~');
            }
        }
        break;
        }
        return r;
    }

    // 验证解析出的消息和返回的 id 与期望一
    bool verify(vt_message_id id, const vt_message &parsed) const
    {
        ASSERT(id == msg_id);
        switch (msg_id)
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
            break;
        }
        return true;
    }
};

// ============================================================================
// 正向测试
// ============================================================================

// 冒烟测试: 确保最基本的序列能被解
bool test_smoke()
{
    vt_parser p;

    // ESC M reverse_index
    ASSERT(p.parse(U'\x1B') == vt_message_id::continue_);
    ASSERT(p.parse(U'M') == vt_message_id::reverse_index);
    p.reset(vt_message_id::reverse_index);

    // ESC [ 1 A cursor_up
    ASSERT(p.parse(U'\x1B') == vt_message_id::continue_);
    ASSERT(p.parse(U'[') == vt_message_id::continue_);
    ASSERT(p.parse(U'1') == vt_message_id::continue_);
    ASSERT(p.parse(U'A') == vt_message_id::cursor_up);
    ASSERT(p.get().count == 1);
    p.reset(vt_message_id::cursor_up);

    // ESC ] 0 ; hello BEL set_window_title
    ASSERT(p.parse(U'\x1B') == vt_message_id::continue_);
    ASSERT(p.parse(U']') == vt_message_id::continue_);
    ASSERT(p.parse(U'0') == vt_message_id::continue_);
    ASSERT(p.parse(U';') == vt_message_id::continue_);
    ASSERT(p.parse(U'h') == vt_message_id::continue_);
    ASSERT(p.parse(U'i') == vt_message_id::continue_);
    ASSERT(p.parse(0x07) == vt_message_id::set_window_title);
    ASSERT(p.get().title == U"hi");
    p.reset(vt_message_id::set_window_title);

    // Esc followed by text "ab"
    ASSERT(p.parse(U'a') == vt_message_id::continue_text);
    ASSERT(p.parse(U'b') == vt_message_id::continue_text);

    return true;
}

// 测试单条随机消息的往
bool test_positive_single()
{
    vt_parser parser;
    for (int trial = 0; trial < 500; ++trial)
    {
        msg_gen gen;
        raw_seq seq = gen.serialize();

        bool got_message = false;
        for (size_t i = 0; i < seq.code_points.size(); ++i)
        {
            vt_message_id id = parser.parse(seq.code_points[i]);
            if ((id != vt_message_id::continue_ && id != vt_message_id::continue_text))
            {
                got_message = true;
                if (!gen.verify(id, parser.get()))
                    return false;
                parser.reset(id); // 消费后精确清
            }
        }
        ASSERT(got_message);
    }
    return true;
}

// 测试多条消息拼接后的连续解析
bool test_positive_concatenated()
{
    vt_parser parser;
    for (int trial = 0; trial < 200; ++trial)
    {
        int msg_count = rand_short(2, 6);
        std::vector<msg_gen> gens(msg_count);
        raw_seq seq;
        for (int i = 0; i < msg_count; ++i)
        {
            raw_seq part = gens[i].serialize();
            seq.code_points.insert(seq.code_points.end(), part.code_points.begin(), part.code_points.end());
        }

        int parsed_idx = 0;
        for (size_t i = 0; i < seq.code_points.size(); ++i)
        {
            vt_message_id id = parser.parse(seq.code_points[i]);
            if ((id != vt_message_id::continue_ && id != vt_message_id::continue_text))
            {
                ASSERT(parsed_idx < msg_count);
                if (!gens[parsed_idx].verify(id, parser.get()))
                    return false;
                parser.reset(id);
                ++parsed_idx;
            }
        }
        ASSERT(parsed_idx == msg_count);
    }
    return true;
}

// 测试消息与文本穿
bool test_positive_with_text()
{
    vt_parser parser;
    for (int trial = 0; trial < 100; ++trial)
    {
        msg_gen gen;
        raw_seq seq = gen.serialize();

        // 随机生成前后文本
        std::u32string prefix, suffix;
        int pre_len = rand_short(1, 10);
        int suf_len = rand_short(1, 10);
        for (int i = 0; i < pre_len; ++i)
            prefix += rand_ascii();
        for (int i = 0; i < suf_len; ++i)
            suffix += rand_ascii();

        raw_seq full;
        full.add_u32string(prefix);
        full.code_points.insert(full.code_points.end(), seq.code_points.begin(), seq.code_points.end());
        full.add_u32string(suffix);

        bool text_received = false;
        bool msg_received = false;
        for (size_t i = 0; i < full.code_points.size(); ++i)
        {
            vt_message_id id = parser.parse(full.code_points[i]);
            if ((id != vt_message_id::continue_ && id != vt_message_id::continue_text))
            {
                if (id == vt_message_id::text)
                {
                    text_received = true;
                    // 文本内容可以是前缀、后缀或非法序
                }
                else
                {
                    msg_received = true;
                    if (!gen.verify(id, parser.get()))
                        return false;
                }
                parser.reset(id);
            }
        }
        ASSERT(msg_received);
    }
    return true;
}

// ============================================================================
// 反向测试（非法序text 消息，内容与原输入一致）
// ============================================================================

// 辅助函数：输入一组码点，应当产生一text 消息，且 text 内容等于输入序列
bool test_illegal_as_text(const raw_seq &seq)
{
    vt_parser parser;
    bool produced = false;
    for (size_t i = 0; i < seq.code_points.size(); ++i)
    {
        vt_message_id id = parser.parse(seq.code_points[i]);
        if ((id != vt_message_id::continue_ && id != vt_message_id::continue_text))
        {
            ASSERT(id == vt_message_id::text);
            // 验证 text 视图等于原始非法序列
            std::u32string_view txt = parser.get().text;
            ASSERT(txt.size() == seq.code_points.size());
            // 由于原始序列可能在不同时机输出（如非法字符就在当前字符）
            // 但在此测试中我们故意构造完整的非法序列一次性输出（不包含控制中断）
            // 所以直接比较即可
            for (size_t j = 0; j < txt.size(); ++j)
                ASSERT(txt[j] == seq.code_points[j]);
            produced = true;
            parser.reset(id);
            break; // 只期望一条消
        }
    }
    ASSERT(produced);
    return true;
}

// 各种典型非法序列的测

bool test_illegal_esc_unknown_final()
{
    raw_seq seq;
    seq.add_esc();
    seq.add(U'X');
    return test_illegal_as_text(seq);
}

bool test_illegal_charset_unknown()
{
    raw_seq seq;
    seq.add_esc();
    seq.add(U'(');
    seq.add(U'C');
    return test_illegal_as_text(seq);
}

bool test_illegal_csi_unknown_final()
{
    // CSI 参数 + 非法终态字
    raw_seq seq;
    seq.add_csi();
    seq.add(U'1');
    seq.add(U'<'); // '<' 不是合法中间或终
    return test_illegal_as_text(seq);
}

bool test_illegal_csi_private_cursor()
{
    raw_seq seq;
    seq.add_csi();
    seq.add(U'?');
    seq.add(U'3');
    seq.add(U'A');
    return test_illegal_as_text(seq);
}

bool test_illegal_csi_private_unknown()
{
    raw_seq seq;
    seq.add_csi();
    seq.add(U'?');
    seq.add_str(U"99");
    seq.add(U'h');
    return test_illegal_as_text(seq);
}

bool test_illegal_ss3_unknown_final()
{
    raw_seq seq;
    seq.add_esc();
    seq.add(U'O');
    seq.add(U'X');
    return test_illegal_as_text(seq);
}

bool test_illegal_decscusr_missing_sp()
{
    raw_seq seq;
    seq.add_csi();
    seq.add(U'3');
    seq.add(U'q');
    return test_illegal_as_text(seq);
}

bool test_illegal_decstr_missing_bang()
{
    raw_seq seq;
    seq.add_csi();
    seq.add(U'p');
    return test_illegal_as_text(seq);
}

bool test_illegal_decfnk_unknown_code()
{
    raw_seq seq;
    seq.add_csi();
    seq.add_str(U"99");
    seq.add(U'~');
    return test_illegal_as_text(seq);
}

bool test_illegal_sgr_extended_truncated()
{
    raw_seq seq;
    seq.add_csi();
    seq.add_str(U"38;5");
    seq.add(U'm');
    return test_illegal_as_text(seq);
}

bool test_illegal_osc_unknown_code()
{
    raw_seq seq;
    seq.add_osc();
    seq.add(U'9');
    seq.add(U';');
    seq.add_str(U"test");
    seq.add(0x07);
    return test_illegal_as_text(seq);
}

// OSC 缓冲区溢出现在也不丢弃，只是忽略超出部分？在旧版中设osc_buf_overflow 错误
// 新版中由于我们在解析时仍然尝试写_osc_buf 并检查长度，但非法时整个 OSC 序列会变text
// 溢出时我们在代码里设置了 _bad，但新版没有 bad，而是继续，然dispatch 可能失败从而输text
// 所以测试应该验证溢出也输出 text 且内容完整
bool test_illegal_osc_buf_overflow()
{
    raw_seq seq;
    seq.add_osc();
    seq.add(U'4');
    seq.add(U';');
    for (int i = 0; i < 40; ++i)
        seq.add(U'x');
    seq.add(0x07);
    return test_illegal_as_text(seq);
}

bool test_illegal_osc_palette_no_rgb()
{
    raw_seq seq;
    seq.add_osc();
    seq.add(U'4');
    seq.add(U';');
    seq.add(U'0');
    seq.add(U';');
    seq.add_str(U"norgb:ff/00/ff");
    seq.add(0x07);
    return test_illegal_as_text(seq);
}

// 组合测试：多种非法序列连续输入，各自输出 text
bool test_illegal_mixed()
{
    // 构造多个非法序列拼接，每个都应产生一text 消息，内容等于该序列原文
    struct
    {
        raw_seq seq;
    } cases[] = {
        {[]() {
            raw_seq r;
            r.add_esc();
            r.add(U'Y');
            return r;
        }()},
        {[]() {
            raw_seq r;
            r.add_esc();
            r.add(U'(');
            r.add(U'9');
            return r;
        }()},
        {[]() {
            raw_seq r;
            r.add_csi();
            r.add(U'?');
            r.add(U'5');
            r.add(U'h');
            return r;
        }()},
        {[]() {
            raw_seq r;
            r.add_csi();
            r.add(U'?');
            r.add(U'2');
            r.add(U'A');
            return r;
        }()},
    };

    for (auto &c : cases)
    {
        vt_parser parser;
        bool got = false;
        for (size_t i = 0; i < c.seq.code_points.size(); ++i)
        {
            vt_message_id id = parser.parse(c.seq.code_points[i]);
            if ((id != vt_message_id::continue_ && id != vt_message_id::continue_text))
            {
                ASSERT(id == vt_message_id::text);
                std::u32string_view txt = parser.get().text;
                ASSERT(txt.size() == c.seq.code_points.size());
                for (size_t j = 0; j < txt.size(); ++j)
                    ASSERT(txt[j] == c.seq.code_points[j]);
                got = true;
                parser.reset(id);
                break;
            }
        }
        ASSERT(got);
    }
    return true;
}

// ============================================================================
// Resize window parser tests (\x1b[8;height;width t)
// ============================================================================
bool test_parse_resize_window_basic()
{
    vt_parser p;
    bool got = false;
    char32_t seq[] = {0x1B, U'[', U'8', U';', U'4', U'0', U';', U'1', U'0', U'0', U't'};
    for (char32_t ch : seq)
    {
        vt_message_id id = p.parse(ch);
        if ((id != vt_message_id::continue_ && id != vt_message_id::continue_text))
        {
            ASSERT(id == vt_message_id::resize_window);
            ASSERT(p.get().resize_rows == 40);
            ASSERT(p.get().resize_cols == 100);
            got = true;
            p.reset(id);
        }
    }
    ASSERT(got);
    return true;
}

bool test_parse_resize_window_zero_invalid()
{
    vt_parser p;
    bool got_text = false;
    char32_t seq[] = {0x1B, U'[', U'8', U';', U'0', U';', U'0', U't'};
    for (char32_t ch : seq)
    {
        vt_message_id id = p.parse(ch);
        if ((id != vt_message_id::continue_ && id != vt_message_id::continue_text))
        {
            ASSERT(id == vt_message_id::text);
            got_text = true;
            p.reset(id);
        }
    }
    ASSERT(got_text);
    return true;
}

bool test_parse_resize_window_pixel_is_text()
{
    vt_parser p;
    bool got_text = false;
    char32_t seq[] = {0x1B, U'[', U'4', U';', U'4', U'8', U'0', U';', U'6', U'4', U'0', U't'};
    for (char32_t ch : seq)
    {
        vt_message_id id = p.parse(ch);
        if ((id != vt_message_id::continue_ && id != vt_message_id::continue_text))
        {
            ASSERT(id == vt_message_id::text);
            got_text = true;
            p.reset(id);
        }
    }
    ASSERT(got_text);
    return true;
}

bool test_parse_resize_window_fields_reset()
{
    vt_parser p;
    // Use explicit code points to avoid any string-literal escape ambiguity
    char32_t seq[] = {0x1B, U'[', U'8', U';', U'3', U'0', U';', U'9', U'0', U't'};
    for (char32_t ch : seq)
    {
        vt_message_id id = p.parse(ch);
        if ((id != vt_message_id::continue_ && id != vt_message_id::continue_text))
        {
            ASSERT(id == vt_message_id::resize_window);
            p.reset(id);
        }
    }
    for (char32_t ch : U"hello")
    {
        vt_message_id id = p.parse(ch);
        // ground text may return continue_text (not text) since continue_text refactor
        if (id == vt_message_id::continue_ || id == vt_message_id::continue_text)
            continue;
        ASSERT(p.get().resize_rows == 0);
        ASSERT(p.get().resize_cols == 0);
        p.reset(id);
    }
    return true;
}

// ============================================================================
// 回归测试: CR/LF 不覆盖累积文(fix: _handle_control 不再_msg.text)
// ============================================================================

// 模拟 pipe_bridge process_input 逻辑：char32_t 喂入 parser
// 验证 text 消息的内容正确
static std::u32string feed_and_collect_text(const std::u32string &input)
{
    vt_parser p;
    std::u32string collected;
    for (char32_t ch : input)
    {
        vt_message_id id = p.parse(ch);
        if ((id != vt_message_id::continue_ && id != vt_message_id::continue_text))
        {
            if (id == vt_message_id::text)
                collected.append(p.get().text);
            p.reset(id);

            // drain 排队消息
            if (auto d_id = p.parse(U'\0'); d_id != vt_message_id::continue_ && d_id != vt_message_id::continue_text)
            {
                if (d_id == vt_message_id::text)
                    collected.append(p.get().text);
                p.reset(d_id);
            }
        }
    }
    // 排空末尾残留的 _pending_control
    if (auto id = p.parse(U'\0'); id != vt_message_id::continue_ && id != vt_message_id::continue_text)
    {
        if (id == vt_message_id::text)
            collected.append(p.get().text);
        p.reset(id);
    }
    return collected;
}

bool test_regression_cr_preserves_text()
{
    // \r 是专用 carriage_return，前导文本作为 text 先产出
    auto result = feed_and_collect_text(U"echo hello\r");
    ASSERT(result == U"echo hello");
    return true;
}

bool test_regression_lf_preserves_text()
{
    // \n 是专用 line_feed，前导文本作为 text 先产出
    auto result = feed_and_collect_text(U"echo hello\n");
    ASSERT(result == U"echo hello");
    return true;
}

bool test_regression_bare_cr_produces_carriage_return()
{
    // 单独\r → carriage_return
    vt_parser p;
    bool got_cr = false;
    for (char32_t ch : U"\r")
    {
        vt_message_id id = p.parse(ch);
        if (id == vt_message_id::carriage_return)
        {
            ASSERT(p.get().text.empty());
            got_cr = true;
            p.reset(id);
        }
    }
    ASSERT(got_cr);
    return true;
}

bool test_regression_bare_lf_produces_line_feed()
{
    // 单独\n → line_feed
    vt_parser p;
    bool got_lf = false;
    for (char32_t ch : U"\n")
    {
        vt_message_id id = p.parse(ch);
        if (id == vt_message_id::line_feed)
        {
            ASSERT(p.get().text.empty());
            got_lf = true;
            p.reset(id);
        }
    }
    ASSERT(got_lf);
    return true;
}

bool test_regression_crlf_full_pipeline()
{
    // text + carriage_return + line_feed: 文本段不含 \r \n
    auto result = feed_and_collect_text(U"echo hello\r\n");
    ASSERT(result == U"echo hello");
    return true;
}

// ── 回归 BUG: reset() 清除了 _pending_control ──
//   "echo hello" (可打印字符) + \r (CR) → reset(text) 后
//   _pending_control 被清除，\r 被丢弃，行终止符丢失。
//   修复: reset() 不再清除 _pending_control。
bool test_regression_pending_control_survives_reset()
{
    vt_parser p;
    char32_t input[] = {U'e', U'c', U'h', U'o', U'\r'};

    // 前 4 个字符 → text 消息
    vt_message_id id = vt_message_id::continue_;
    for (int i = 0; i < 4; ++i)
    {
        id = p.parse(input[i]);
        ASSERT(id == vt_message_id::continue_text);
    }
    // 第 5 个 \r → parser 先交付 text，设 _pending_control=carriage_return
    id = p.parse(input[4]);
    ASSERT(id == vt_message_id::text);
    ASSERT(p.get().text == U"echo");
    p.reset(vt_message_id::text); // reset 不应清除 _pending_control

    // 下一个 parse() 应交付 carriage_return
    id = p.parse(U' '); // dummy char 触发 _pending_control 交付
    ASSERT(id == vt_message_id::carriage_return);
    ASSERT(p.get().text.empty());
    return true;
}

// ── 回归 BUG: 纯文本无控制字符终止时永远不交付 ──
//   38 个可打印字符积累在 _raw 中，无 \r \n \t ESC 触发交付。
//   修复: 新增 flush_text()，api_write_console 循环后调用。
bool test_regression_flush_text_delivers_accumulated()
{
    vt_parser p;
    char32_t input[] = {U'M', U'i', U'c', U'r', U'o'};

    for (char32_t ch : input)
    {
        vt_message_id id = p.parse(ch);
        ASSERT(id == vt_message_id::continue_text);
    }

    // 所有字符都是 continue_text，无消息交付
    ASSERT(p.has_pending_text());

    // flush_text 应释放 "Micro"
    vt_message_id id = p.flush_text();
    ASSERT(id == vt_message_id::text);
    ASSERT(p.get().text == U"Micro");

    // 再次 flush 无残留
    ASSERT(!p.has_pending_text());
    ASSERT(p.flush_text() == vt_message_id::continue_);
    return true;
}

// ── \r\n 配对由 parser 内部处理，调用方无需额外标志 ──
// "hello\r\n" 流程: parse('h'..'o') 累积 → parse('\r') 产 text, _pending_control=carriage_return
// → parse('\n') 触发 _pending_control, 返回 carriage_return 并将 pending 升级为 line_feed
// → drain: parse(U'\0') 取出 line_feed。调用方只需在每次 reset 后无条件 drain。
bool test_regression_cr_then_nl_bridge_pairing()
{
    vt_parser p;
    char32_t hello[] = {U'h', U'e', U'l', U'l', U'o'};
    for (char32_t ch : hello)
    {
        vt_message_id id = p.parse(ch);
        ASSERT(id == vt_message_id::continue_text);
    }
    // "\r" → parser 产 text("hello")，_pending_control=carriage_return
    vt_message_id id = p.parse(U'\r');
    ASSERT(id == vt_message_id::text);
    ASSERT(p.get().text == U"hello");
    p.reset(id);

    // "\n" → _pending_control 触发, 返回 carriage_return, 升级为 line_feed
    id = p.parse(U'\n');
    ASSERT(id == vt_message_id::carriage_return);
    p.reset(id);

    // drain: parse(U'\0') → line_feed（_pending_control 升级后的延迟消息）
    id = p.parse(U'\0');
    ASSERT(id == vt_message_id::line_feed);
    p.reset(id);

    // 再次 drain: 无 pending → continue_
    id = p.parse(U'\0');
    ASSERT(id == vt_message_id::continue_);

    return true;
}

bool test_regression_text_with_vt_then_cr()
{
    // 混合场景: "abc\x1b[Adef\r" text1="abc", cursor_up, text2="def"
    //  key_up remap pipe_bridge 中完成，parser 产出 cursor_up
    vt_parser p;
    std::u32string collected;
    bool got_cursor_up = false;

    // 显式构造码点，避免字符串字面量转义歧义
    std::u32string input;
    input += U'a';
    input += U'b';
    input += U'c';
    input += 0x1B; // ESC
    input += U'[';
    input += U'A';
    input += U'd';
    input += U'e';
    input += U'f';
    input += U'\r';

    for (char32_t ch : input)
    {
        vt_message_id id = p.parse(ch);
        if (id == vt_message_id::continue_ || id == vt_message_id::continue_text)
            continue;
        if (id == vt_message_id::text)
            collected.append(p.get().text);
        else if (id == vt_message_id::cursor_up)
            got_cursor_up = true;
        else
        {
            fprintf(stderr, "UNEXPECTED id=%d ch=U+%04X\n", static_cast<int>(id), static_cast<unsigned>(ch));
            ASSERT(false); // 不应出现其他消息
        }
        p.reset(id);
    }
    ASSERT(collected == U"abcdef");
    ASSERT(got_cursor_up);
    return true;
}

bool test_regression_multiline_input()
{
    // _pending_control 触发时总会消费当前字符。调用方在每次 reset 后
    // 无条件 drain（parse(U'\0')），无 pending 时首次即返回 continue_。
    vt_parser p;
    std::u32string collected;

    std::u32string input = U"line1\r\nline2\r\nline3\r\n";
    for (char32_t ch : input)
    {
        vt_message_id id = p.parse(ch);
        if (id == vt_message_id::continue_text || id == vt_message_id::continue_)
            continue;
        if (id == vt_message_id::text)
            collected.append(p.get().text);
        p.reset(id);

        // 无条件 drain: _pending_control 最多一个排队消息
        if (auto drain_id = p.parse(U'\0');
            drain_id != vt_message_id::continue_ && drain_id != vt_message_id::continue_text)
        {
            if (drain_id == vt_message_id::text)
                collected.append(p.get().text);
            p.reset(drain_id);
        }
    }
    // drain remaining
    if (auto id = p.parse(U'\0'); id != vt_message_id::continue_ && id != vt_message_id::continue_text)
    {
        if (id == vt_message_id::text)
            collected.append(p.get().text);
        p.reset(id);
    }

    ASSERT(collected == U"line1line2line3");
    return true;
}

// ============================================================================
// Echo 判定测试: should_echo_last() 正确
// ============================================================================

// 地面态可打印字符应回
bool test_echo_ground_printable()
{
    vt_parser p;
    std::u32string_view chars = U"abc123";
    for (char32_t ch : chars)
    {
        p.parse(ch);
        ASSERT(p.should_echo_last());
    }
    return true;
}

// 地面态可见控制字符应回显 (\r \n \b \t)
bool test_echo_ground_controls()
{
    vt_parser p;
    p.parse(U'\r');
    ASSERT(p.should_echo_last());
    p.reset(vt_message_id::text);
    p.parse(U'\n');
    ASSERT(p.should_echo_last());
    p.reset(vt_message_id::text);
    p.parse(U'\b');
    ASSERT(!p.should_echo_last());
    p.reset(vt_message_id::char_del);
    p.parse(U'\t');
    ASSERT(p.should_echo_last());
    p.reset(vt_message_id::cursor_forward_tab);
    return true;
}

// ESC 本身及普ESC 序列不应回显
bool test_echo_esc_not_echoed()
{
    vt_parser p;
    p.parse(0x1B);
    ASSERT(!p.should_echo_last());
    p.parse(U'M');
    ASSERT(!p.should_echo_last()); // reverse_index
    p.reset(vt_message_id::reverse_index);
    return true;
}

// CSI 相对光标序列不应回显原始字节（dispatch 生成钳制 CUP
bool test_echo_csi_cursor_not_echoed()
{
    vt_parser p;
    for (char32_t ch : std::u32string_view(U"hello"))
    {
        p.parse(ch);
        ASSERT(p.should_echo_last());
    }
    vt_message_id id = p.parse(0x1B);
    ASSERT(id == vt_message_id::text);
    ASSERT(!p.should_echo_last());
    p.reset(vt_message_id::text);
    id = p.parse(U'[');
    ASSERT(id == vt_message_id::continue_);
    id = p.parse(U'D');
    ASSERT(id == vt_message_id::cursor_backward);
    ASSERT(!p.should_echo_last());
    p.reset(vt_message_id::cursor_backward);
    return true;
}

// SS3 键盘序列不应回显原始字节（dispatch 生成钳制 CUP
bool test_echo_ss3_not_echoed()
{
    vt_parser p;
    p.parse(0x1B);
    ASSERT(!p.should_echo_last());
    p.parse(U'O');
    ASSERT(!p.should_echo_last());
    p.parse(U'D');
    ASSERT(!p.should_echo_last());
    p.reset(vt_message_id::key_left);
    return true;
}

// text→ESC 过渡：ESC 触发 text flush parser 进入转义，CSI 不回
bool test_echo_text_esc_transition()
{
    vt_parser p;
    for (char32_t ch : std::u32string_view(U"echo "))
    {
        p.parse(ch);
        ASSERT(p.should_echo_last());
    }
    vt_message_id id = p.parse(0x1B);
    ASSERT(id == vt_message_id::text);
    ASSERT(!p.should_echo_last());
    p.reset(vt_message_id::text);
    id = p.parse(U'[');
    ASSERT(id == vt_message_id::continue_);
    id = p.parse(U'D');
    ASSERT(id == vt_message_id::cursor_backward);
    ASSERT(!p.should_echo_last());
    p.reset(vt_message_id::cursor_backward);
    return true;
}

// OSC 标题不应回显
bool test_echo_osc_not_echoed()
{
    vt_parser p;
    p.parse(0x1B);
    p.parse(U']');
    p.parse(U'0');
    p.parse(U';');
    for (char32_t ch : std::u32string_view(U"mytitle"))
        p.parse(ch);
    p.parse(0x07);
    ASSERT(!p.should_echo_last());
    p.reset(vt_message_id::set_window_title);
    return true;
}

// SGR 序列不应回显
bool test_echo_sgr_not_echoed()
{
    vt_parser p;
    p.parse(0x1B);
    p.parse(U'[');
    p.parse(U'3');
    p.parse(U'1');
    p.parse(U'm');
    ASSERT(!p.should_echo_last());
    p.reset(vt_message_id::sgr);
    return true;
}

// CSI ~ 扩展功能键不应回
bool test_echo_csi_tilde_not_echoed()
{
    vt_parser p;
    p.parse(0x1B);
    p.parse(U'[');
    p.parse(U'1');
    p.parse(U'5');
    p.parse(U'~');
    ASSERT(!p.should_echo_last());
    p.reset(vt_message_id::key_f5);
    return true;
}

// ============================================================================
// 入口
// ============================================================================

int main()
{
    utility::suppress_crt_error_dialogs();
    std::wcout << L"VT Parser Positive Tests (char32_t)\n";
    RUN_TEST(test_smoke, L"Smoke test (basic sequences)");
    RUN_TEST(test_positive_single, L"Random single message roundtrip");
    RUN_TEST(test_positive_concatenated, L"Random concatenated messages");
    RUN_TEST(test_positive_with_text, L"Messages with intervening text");

    std::wcout << L"\nVT Parser Negative Tests (illegal -> text)\n";
    RUN_TEST(test_illegal_esc_unknown_final, L"ESC unknown final -> text");
    RUN_TEST(test_illegal_charset_unknown, L"Charset unknown final -> text");
    RUN_TEST(test_illegal_csi_unknown_final, L"CSI unknown final -> text");
    RUN_TEST(test_illegal_csi_private_cursor, L"CSI private cursor -> text");
    RUN_TEST(test_illegal_csi_private_unknown, L"CSI private unknown -> text");
    RUN_TEST(test_illegal_ss3_unknown_final, L"SS3 unknown final -> text");
    RUN_TEST(test_illegal_decscusr_missing_sp, L"DECSCUSR missing SP -> text");
    RUN_TEST(test_illegal_decstr_missing_bang, L"DECSTR missing bang -> text");
    RUN_TEST(test_illegal_decfnk_unknown_code, L"DECFNK unknown code -> text");
    RUN_TEST(test_illegal_sgr_extended_truncated, L"SGR extended truncated -> text");
    RUN_TEST(test_illegal_osc_unknown_code, L"OSC unknown code -> text");
    RUN_TEST(test_illegal_osc_buf_overflow, L"OSC buffer overflow -> text");
    RUN_TEST(test_illegal_osc_palette_no_rgb, L"OSC palette missing rgb prefix -> text");
    RUN_TEST(test_illegal_mixed, L"Multiple illegal sequences -> text");

    std::wcout << L"\nVT Parser Resize Window Tests:\n";
    RUN_TEST(test_parse_resize_window_basic, L"Resize window 40x100");
    RUN_TEST(test_parse_resize_window_zero_invalid, L"Resize window 0x0->text");
    RUN_TEST(test_parse_resize_window_pixel_is_text, L"Pixel resize 4;...->text");
    RUN_TEST(test_parse_resize_window_fields_reset, L"Resize fields reset after");

    std::wcout << L"\nVT Parser Regression Tests (CR/LF text preservation)\n";
    RUN_TEST(test_regression_cr_preserves_text, L"CR preserves preceding text");
    RUN_TEST(test_regression_lf_preserves_text, L"LF preserves preceding text");
    RUN_TEST(test_regression_bare_cr_produces_carriage_return, L"Bare CR produces carriage_return");
    RUN_TEST(test_regression_bare_lf_produces_line_feed, L"Bare LF produces line_feed");
    RUN_TEST(test_regression_crlf_full_pipeline, L"CRLF full pipeline");
    RUN_TEST(test_regression_flush_text_delivers_accumulated, L"Flush text delivers accumulated");
    RUN_TEST(test_regression_pending_control_survives_reset, L"Pending control survives reset");
    RUN_TEST(test_regression_cr_then_nl_bridge_pairing, L"CR then NL bridge pairing");
    RUN_TEST(test_regression_text_with_vt_then_cr, L"Text+VT+CR preserves all");
    RUN_TEST(test_regression_multiline_input, L"Multiline input");

    std::wcout << L"\nVT Parser Echo Tests (should_echo_last)\n";
    RUN_TEST(test_echo_ground_printable, L"Ground printable chars echoed");
    RUN_TEST(test_echo_ground_controls, L"Ground controls (\\r\\n\\b\\t) echoed");
    RUN_TEST(test_echo_esc_not_echoed, L"ESC / reverse_index not echoed");
    RUN_TEST(test_echo_csi_cursor_not_echoed, L"CSI D cursor_backward NOT echoed");
    RUN_TEST(test_echo_ss3_not_echoed, L"SS3 D key_left NOT echoed");
    RUN_TEST(test_echo_text_esc_transition, L"Text→ESC transition preserves echo state");
    RUN_TEST(test_echo_osc_not_echoed, L"OSC title not echoed");
    RUN_TEST(test_echo_sgr_not_echoed, L"SGR not echoed");
    RUN_TEST(test_echo_csi_tilde_not_echoed, L"CSI ~ extended key not echoed");

    std::wcout << L"\nTotal: " << (tests_passed + tests_failed) << L" | Passed: " << tests_passed << L" | Failed: "
               << tests_failed << std::endl;

    return tests_failed == 0 ? 0 : 1;
}
