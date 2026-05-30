// ── conpty/vt_input_engine.hpp ─────────────────────
// Layer 2: VT 键盘消息到 INPUT_RECORD 的转换。
//
// 功能分解：
// 1. convert 把解析器产出的键盘类 vt_message 映射为单条 KEY_EVENT_RECORD。
// 2. convert_text 把普通文本拆成可写入 input_buffer 的 KEY_EVENT_RECORD 序列。
// 3. ctrl_state 保存当前修饰键状态，并写入每条生成的 key event。
#pragma once
#include <windows.h>
#include <string>
#include "conpty_vt_parser.hpp"
#include "char_convert.hpp"

namespace conpty
{

struct vt_input_engine
{
    // 当前键盘修饰键状态，取值为 LEFT_CTRL_PRESSED 等 ControlKeyState 标志位组合。
    DWORD ctrl_state = 0;

    // ── convert: (vt_message_id + vt_message) → INPUT_RECORD ──
    // id 由 parse() 返回, msg 由 parser.get() 提供 (二者分离)
    bool convert(vt_message_id id, const vt_message &msg, INPUT_RECORD &rec)
    {
        // convert 只生成 KEY_DOWN；pipe_bridge 在需要完整按下/释放对时会额外
        // 构造 KEY_UP。这样 ConsoleRead 本地编辑可只消费按下事件。
        // rec 先初始化为一个 key-down 事件；非键盘消息返回 false。
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
            // Ctrl+Z 在控制台输入里以 SUB 字符出现，VK 值沿用 ASCII 26。
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
        // emit_record 会被调用 0..N 次；代理对字符会产生两个 UTF-16 record。
        INPUT_RECORD rec;
        rec.EventType = KEY_EVENT;
        auto &ke = rec.Event.KeyEvent;
        ke.bKeyDown = true;
        ke.wRepeatCount = 1;
        ke.dwControlKeyState = ctrl_state;

        for (char32_t cp : text)
        {
            if (cp < 0x20 && cp != U'\t')
                continue; // 除 tab 外，文本输入不把 C0 控制字符写入队列。

            if (cp == U'\t')
            {
                ke.wVirtualKeyCode = VK_TAB;
                ke.uChar.UnicodeChar = L'\t';
            }
            else
            {
                ke.wVirtualKeyCode = 0;
                // Unicode 文本输入不一定有稳定 VK；用 VK=0 + UnicodeChar 表示
                // 字符输入。非 BMP 字符拆成 UTF-16 code unit 逐条发送。
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
            // append_enter 用于把一段文本模拟成已提交的行，末尾只追加
            // KEY_DOWN Enter，由调用方决定是否需要 KEY_UP。
            ke.wVirtualKeyCode = VK_RETURN;
            ke.uChar.UnicodeChar = L'\r';
            ke.wVirtualScanCode = 0;
            emit_record(rec);
        }
    }
};

} // namespace conpty
