// ── conpty/vt_input_engine.hpp ─────────────────────
// Layer 2: VT 消息 → INPUT_RECORD 转换引擎 (char32_t 版本)
//
// 与 conpty/vt_input_engine.hpp 的区别:
//   - 使用 conpty::vt_message (char32_t 解析器输出)
//   - convert(): vt_message → INPUT_RECORD
//   - convert_text(): char32_t 文本 → INPUT_RECORD 序列 (含 char32_t→WCHAR 转换)
//   - convert_text() 直接操作 char32_t, 不再手工 UTF-8 解码
#pragma once
#include <windows.h>
#include <string>
#include "conpty_vt_parser.hpp"
#include "char_convert.hpp"

namespace conpty
{

struct vt_input_engine
{
    DWORD ctrl_state = 0;

    // ── convert: (vt_message_id + vt_message) → INPUT_RECORD ──
    // id 由 parse() 返回, msg 由 parser.get() 提供 (二者分离)
    bool convert(vt_message_id id, const vt_message &msg, INPUT_RECORD &rec)
    {
        rec.EventType = KEY_EVENT;
        auto &ke = rec.Event.KeyEvent;
        ke.bKeyDown = true;
        ke.wRepeatCount = 1;
        ke.wVirtualKeyCode = 0;
        ke.wVirtualScanCode = 0;
        ke.uChar.UnicodeChar = 0;
        ke.dwControlKeyState = ctrl_state;

        switch (id)
        {
        // ── 光标键 ──
        case vt_message_id::key_up:
            ke.wVirtualKeyCode = VK_UP;
            return true;
        case vt_message_id::key_down:
            ke.wVirtualKeyCode = VK_DOWN;
            return true;
        case vt_message_id::key_right:
            ke.wVirtualKeyCode = VK_RIGHT;
            return true;
        case vt_message_id::key_left:
            ke.wVirtualKeyCode = VK_LEFT;
            return true;
        case vt_message_id::key_home:
            ke.wVirtualKeyCode = VK_HOME;
            return true;
        case vt_message_id::key_end:
            ke.wVirtualKeyCode = VK_END;
            return true;
        case vt_message_id::key_insert:
            ke.wVirtualKeyCode = VK_INSERT;
            return true;
        case vt_message_id::key_delete:
            ke.wVirtualKeyCode = VK_DELETE;
            return true;
        case vt_message_id::key_page_up:
            ke.wVirtualKeyCode = VK_PRIOR;
            return true;
        case vt_message_id::key_page_down:
            ke.wVirtualKeyCode = VK_NEXT;
            return true;

        // ── 功能键 ──
        case vt_message_id::key_f1:
            ke.wVirtualKeyCode = VK_F1;
            return true;
        case vt_message_id::key_f2:
            ke.wVirtualKeyCode = VK_F2;
            return true;
        case vt_message_id::key_f3:
            ke.wVirtualKeyCode = VK_F3;
            return true;
        case vt_message_id::key_f4:
            ke.wVirtualKeyCode = VK_F4;
            return true;
        case vt_message_id::key_f5:
            ke.wVirtualKeyCode = VK_F5;
            return true;
        case vt_message_id::key_f6:
            ke.wVirtualKeyCode = VK_F6;
            return true;
        case vt_message_id::key_f7:
            ke.wVirtualKeyCode = VK_F7;
            return true;
        case vt_message_id::key_f8:
            ke.wVirtualKeyCode = VK_F8;
            return true;
        case vt_message_id::key_f9:
            ke.wVirtualKeyCode = VK_F9;
            return true;
        case vt_message_id::key_f10:
            ke.wVirtualKeyCode = VK_F10;
            return true;
        case vt_message_id::key_f11:
            ke.wVirtualKeyCode = VK_F11;
            return true;
        case vt_message_id::key_f12:
            ke.wVirtualKeyCode = VK_F12;
            return true;

        // ── Ctrl + 光标键 ──
        case vt_message_id::key_ctrl_up:
            ke.wVirtualKeyCode = VK_UP;
            ke.dwControlKeyState |= LEFT_CTRL_PRESSED;
            return true;
        case vt_message_id::key_ctrl_down:
            ke.wVirtualKeyCode = VK_DOWN;
            ke.dwControlKeyState |= LEFT_CTRL_PRESSED;
            return true;
        case vt_message_id::key_ctrl_right:
            ke.wVirtualKeyCode = VK_RIGHT;
            ke.dwControlKeyState |= LEFT_CTRL_PRESSED;
            return true;
        case vt_message_id::key_ctrl_left:
            ke.wVirtualKeyCode = VK_LEFT;
            ke.dwControlKeyState |= LEFT_CTRL_PRESSED;
            return true;

        // ── 特殊控制字符 ──
        case vt_message_id::char_del:
            ke.wVirtualKeyCode = VK_BACK;
            ke.uChar.UnicodeChar = L'\b';
            return true;
        case vt_message_id::char_sub:
            ke.wVirtualKeyCode = 26;
            ke.uChar.UnicodeChar = 0x1A;
            return true;
        case vt_message_id::char_esc:
            ke.wVirtualKeyCode = VK_ESCAPE;
            ke.uChar.UnicodeChar = 0x1B;
            return true;

        // ── 其他非输入消息 ──
        default:
            return false;
        }
    }

    // ── convert_text: char32_t 文本 → INPUT_RECORD 序列 ──
    // text: char32_t 文本 (不含 VT 序列)
    // emit_record: 每生成一个 INPUT_RECORD 调用一次
    // append_enter: 末尾追加 Enter 键
    template <typename Func>
    void convert_text(std::u32string_view text, Func &&emit_record, bool append_enter = false)
    {
        INPUT_RECORD rec;
        rec.EventType = KEY_EVENT;
        auto &ke = rec.Event.KeyEvent;
        ke.bKeyDown = true;
        ke.wRepeatCount = 1;
        ke.dwControlKeyState = ctrl_state;

        for (char32_t cp : text)
        {
            if (cp < 0x20 && cp != U'\t')
                continue; // 控制字符跳过

            if (cp == U'\t')
            {
                ke.wVirtualKeyCode = VK_TAB;
                ke.uChar.UnicodeChar = L'\t';
            }
            else
            {
                ke.wVirtualKeyCode = 0;
                // char32_t → wchar_t (含代理对 → 需拆分为多个 INPUT_RECORD)
                wchar_t wbuf[2];
                int nw = to_wchar(cp, wbuf);
                for (int i = 0; i < nw; ++i)
                {
                    ke.uChar.UnicodeChar = wbuf[i];
                    ke.wVirtualScanCode = 0;
                    emit_record(rec);
                }
                continue; // 已在循环内 emit
            }
            ke.wVirtualScanCode = 0;
            emit_record(rec);
        }

        if (append_enter)
        {
            ke.wVirtualKeyCode = VK_RETURN;
            ke.uChar.UnicodeChar = L'\r';
            ke.wVirtualScanCode = 0;
            emit_record(rec);
        }
    }
};

} // namespace conpty
