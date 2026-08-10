// ── conpty/signal.cpp ──────────────────────────────
// PtySignal 消费者实现（overlapped I/O，无线程）。
// 缓冲管理 / overlapped 读生命周期 / 断开检测由基类提供，本文件只实现
// PtySignal 协议解析与状态更新。
#include "signal.hpp"
#include "console_state.hpp"
#include "screen_buffer.hpp"
#include "utility/log.hpp"
#include <cstring>

namespace corehost::conpty
{

size_t pty_signal_consumer::payload_size(unsigned sig) noexcept
{
    switch (static_cast<PtySignal>(sig))
    {
    case PtySignal::ShowHideWindow:
        return sizeof(unsigned short);
    case PtySignal::ClearBuffer:
        return 0;
    case PtySignal::SetParent:
        return sizeof(ULONG_PTR);
    case PtySignal::ResizeWindow:
        return sizeof(COORD);
    default:
        return 0;
    }
}

bool pty_signal_consumer::try_parse_message() noexcept
{
    if (_parse == parse_state::need_sig)
    {
        // 需要 2 字节信号 id。
        if (available() - consumed() < 2)
            return false;
        const auto sig = static_cast<unsigned>(static_cast<unsigned char>(data()[consumed()])) |
                         (static_cast<unsigned>(static_cast<unsigned char>(data()[consumed() + 1])) << 8);
        set_consumed(consumed() + 2);

        _need = payload_size(sig);
        if (_need == 0)
        {
            // 无 payload 或未知信号：直接处理。未知信号不消费 payload，
            // 与原线程实现一致（协议同步由写端保证）。
            process_signal(sig);
            return true;
        }
        _current_sig = sig;
        _parse = parse_state::need_payload;
        return true; // 头部已消费；下一次迭代检查 payload 是否完整
    }

    // need_payload
    if (available() - consumed() < _need)
        return false;
    process_signal(_current_sig);
    set_consumed(consumed() + _need);
    _parse = parse_state::need_sig;
    return true;
}

void pty_signal_consumer::process_signal(unsigned sig) noexcept
{
    LOG2("PtySignal id=%u", sig);
    switch (static_cast<PtySignal>(sig))
    {
    case PtySignal::ShowHideWindow: {
        // 当前不管理真实窗口，但必须消费 show payload；否则下一次读到的
        // payload 会被误当作信号 id，破坏协议同步。
        unsigned short show = 0;
        std::memcpy(&show, data() + consumed(), sizeof(show));
        LOG2("PtySignal ShowHideWindow show=%u", static_cast<unsigned>(show));
        break;
    }
    case PtySignal::ClearBuffer:
        // ClearBuffer 只更新本地屏幕模型。真实终端清屏由主输出路径发 VT。
        LOG2("PtySignal ClearBuffer");
        _sbuf->clear(_state->default_attributes);
        break;
    case PtySignal::SetParent: {
        // WT 按 ULONG_PTR 发送 HWND；corehost 不重新设置父窗口，只消费字段
        // 保持后续信号边界正确。
        ULONG_PTR hwnd = 0;
        std::memcpy(&hwnd, data() + consumed(), sizeof(hwnd));
        LOG2("PtySignal SetParent hwnd=%p", reinterpret_cast<void *>(hwnd));
        break;
    }
    case PtySignal::ResizeWindow: {
        // ResizeWindow 是 WT 主动通知的可见尺寸。state 中三个尺寸保持一致，
        // 因为当前 ConPTY 模型没有独立 scrollback buffer。
        COORD sz{0, 0};
        std::memcpy(&sz, data() + consumed(), sizeof(sz));
        LOG2("PtySignal ResizeWindow size=%dx%d", sz.X, sz.Y);
        _state->screen_buffer_size = sz;
        _state->max_window_size = sz;
        _sbuf->viewport.reset_to_buffer(sz);
        _sbuf->resize(sz);
        break;
    }
    default:
        break;
    }
}

} // namespace corehost::conpty
