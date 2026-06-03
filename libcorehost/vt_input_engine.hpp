// ── conpty/vt_input_engine.hpp ─────────────────────
// Layer 2: VT 键盘消息到 INPUT_RECORD 的转换。
//
// 功能分解：
// 1. convert 把解析器产出的键盘类 vt_message 映射为单条 KEY_EVENT_RECORD。
// 2. ctrl_state 保存当前修饰键状态，并写入每条生成的 key event。
#pragma once
#include <windows.h>
#include <string>
#include "vt_parser.hpp"

namespace corehost::conpty
{

struct vt_input_engine
{
    // 当前键盘修饰键状态，取值为 LEFT_CTRL_PRESSED 等 ControlKeyState 标志位组合。
    DWORD ctrl_state = 0;

    // ── convert: (vt_message_id + vt_message) → INPUT_RECORD ──
    // id 和 msg 都来自 vt_parser::parse(range) 的 vt_parse_result；parser 内部
    // 复用同一个消息存储，不要求调用方再窥探 parser 当前 payload。
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
};

} // namespace corehost::conpty
