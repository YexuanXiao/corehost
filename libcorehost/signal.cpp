// ── conpty/signal.cpp ──────────────────────────────
// PtySignal 消费者实现（overlapped I/O，无线程）。
// 缓冲管理 / overlapped 读生命周期 / 断开检测 / 帧原子性检查由基类提供，
// 本文件只实现 PtySignal 协议解析（无状态）与状态更新。
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
    const size_t pos = consumed();
    const size_t n = available() - pos;
    if (n < 2)
        return false; // 不足一帧：帧边界或半帧残留（基类区分）

    const auto sig = static_cast<unsigned>(static_cast<unsigned char>(data()[pos])) |
                     (static_cast<unsigned>(static_cast<unsigned char>(data()[pos + 1])) << 8);
    const size_t need = payload_size(sig);
    if (n < 2 + need)
        return false;

    set_consumed(pos + 2 + need);
    process_signal(sig, data() + pos + 2);
    return true;
}

void pty_signal_consumer::process_signal(unsigned sig, const std::byte *payload) noexcept
{
    LOG2("PtySignal id=%u", sig);
    switch (static_cast<PtySignal>(sig))
    {
    case PtySignal::ShowHideWindow: {
        // 当前不管理真实窗口，但必须消费 show payload；否则下一次读到的
        // payload 会被误当作信号 id，破坏协议同步。
        unsigned short show = 0;
        std::memcpy(&show, payload, sizeof(show));
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
        std::memcpy(&hwnd, payload, sizeof(hwnd));
        LOG2("PtySignal SetParent hwnd=%p", reinterpret_cast<void *>(hwnd));
        break;
    }
    case PtySignal::ResizeWindow: {
        // ResizeWindow 是 WT 主动通知的可见尺寸。state 中三个尺寸保持一致，
        // 因为当前 ConPTY 模型没有独立 scrollback buffer。
        COORD sz{0, 0};
        std::memcpy(&sz, payload, sizeof(sz));
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
