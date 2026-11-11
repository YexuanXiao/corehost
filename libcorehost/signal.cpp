// ── conpty/signal.cpp ──────────────────────────────
// WT 信号管道轮询器实现（替代原 PtySignal 信号线程）。
//
// 读取策略：写端一次 WriteFile 一条完整消息（≤ 10 字节，原子写入），
// 信号线程按 id + 定长 payload 精确读取后调用 apply()。
// 缓冲内数据不足或未知 id 都是协议损坏，抛出异常由会话入口统一处理后
// 退出程序。
#include "signal.hpp"
#include "console_state.hpp"
#include "screen_buffer.hpp"
#include <algorithm>
#include <array>
#include <cstring>

namespace corehost::conpty
{

// 由消息 id 确定 payload 长度。未知 id 返回 0，调用方按协议损坏退出。
size_t pty_signal_reader::payload_size_for(unsigned short id) noexcept
{
    switch (static_cast<PtySignal>(id))
    {
    case PtySignal::ShowHideWindow:
        return sizeof(unsigned short);
    case PtySignal::ClearBuffer:
        return sizeof(unsigned short);
    case PtySignal::SetParent:
        return sizeof(ULONG_PTR);
    case PtySignal::ResizeWindow:
        return sizeof(COORD);
    default:
        return 0;
    }
}

// 应用一条完整 PtySignal。会话锁由调用方持有（信号线程），因此这里只做状态
// 更新，不再处理管道读取或同步。

// 执行一条完整 PtySignal。所有状态更新都在会话主线程进行，与 I/O 循环
// 的其他状态推进天然互斥，无需额外同步。
void pty_signal_reader::apply(unsigned short id, std::span<const std::byte> payload)
{
    switch (static_cast<PtySignal>(id))
    {
    case PtySignal::ShowHideWindow: {
        // 当前不管理真实窗口，但必须消费 show payload；否则下一次读到的
        // payload 会被误当作信号 id，破坏协议同步。
        unsigned short show = 0;
        std::memcpy(&show, payload.data(), sizeof(show));
        LOG2("PtySignal ShowHideWindow show=%u", static_cast<unsigned>(show));
        break;
    }
    case PtySignal::ClearBuffer: {
        // keepCursorRow 是 WT 是否保留光标所在行的提示。当前清空整个屏幕
        // 模型；真实终端清屏由主输出路径发 VT。payload 必须消费以保持
        // 消息边界，否则下一条消息的 id 会错位。
        unsigned short keep_cursor_row = 0;
        std::memcpy(&keep_cursor_row, payload.data(), sizeof(keep_cursor_row));
        LOG2("PtySignal ClearBuffer keepCursorRow=%u", static_cast<unsigned>(keep_cursor_row));
        _sbuf.clear(_state.default_attributes);
        break;
    }
    case PtySignal::SetParent: {
        // WT 按 ULONG_PTR 发送 HWND；corehost 不重新设置父窗口，只消费字段
        // 保持后续信号边界正确。
        ULONG_PTR hwnd = 0;
        std::memcpy(&hwnd, payload.data(), sizeof(hwnd));
        LOG2("PtySignal SetParent hwnd=%p", reinterpret_cast<void *>(hwnd));
        break;
    }
    case PtySignal::ResizeWindow: {
        // ResizeWindow 是 WT 主动通知的可见尺寸。state 中三个尺寸保持一致，
        // 因为当前 ConPTY 模型没有独立 scrollback buffer。
        COORD sz{};
        std::memcpy(&sz, payload.data(), sizeof(sz));
        LOG2("PtySignal ResizeWindow size=%dx%d", sz.X, sz.Y);
        _state.screen_buffer_size = sz;
        _state.max_window_size = sz;
        _sbuf.viewport.reset_to_buffer(sz);
        _sbuf.resize(sz);
        break;
    }
    default:
        break;
    }
}

} // namespace corehost::conpty