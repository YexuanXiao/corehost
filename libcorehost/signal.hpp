// ── conpty/signal.hpp ──────────────────────────────
// PtySignal 消费者（无线程，overlapped I/O）。
//
// 功能分解：
// 1. 从 WT 信号管道异步读取 PtySignal id 和对应 payload。
// 2. ClearBuffer/ResizeWindow 直接更新 screen_buffer 和 console_state。
// 3. 管道断开时 handle_event 返回 false，主 I/O 循环驱动 EOF 完成并退出。
//
// I/O 机械部分（缓冲管理、overlapped 读生命周期、断开检测）由基类
// win32::overlapped_pipe_reader 提供；本类只实现 PtySignal 协议解析。
// 完成事件由主 I/O 循环与 ConDrv server 一起交给 WaitForMultipleObjects，
// 断开也由主循环直接发现，不需要任何事件通知机制。
#pragma once
#include <windows.h>
#include <cstddef>
#include "win32/overlapped_reader.hpp"

namespace corehost::conpty
{

enum class PtySignal
{
    // WT 通知可见性变化。corehost 当前不维护窗口可见状态，所以只消耗消息。
    ShowHideWindow = 1,
    // WT 请求清空回滚/可见缓冲区。消费者把本地 screen_buffer 清空，并把
    // cursor 移回 viewport 左上角。
    ClearBuffer = 2,
    // WT 设置父窗口。corehost 不创建真实窗口，当前只消耗消息以保持管道同步。
    SetParent = 3,
    // WT 通知终端行列变化。消费者更新 screen_buffer 尺寸、viewport 和
    // console_state.cursor 的有效范围。
    ResizeWindow = 8,
};

struct console_state;
struct screen_buffer;

// PtySignal 消费者：在主 I/O 循环线程内用 overlapped I/O 读取信号管道，
// 不再需要独立信号线程。公共 API（start_read/event/valid/handle_event/
// try_handle_event）继承自 win32::overlapped_pipe_reader。
class pty_signal_consumer : public win32::overlapped_pipe_reader
{
  public:
    // 无管道状态；配合移动赋值用于"有信号管道时才装配"（栈上声明）。
    pty_signal_consumer() noexcept = default;

    // 绑定管道与共享状态。state/sbuf 引用以指针保存，使类可移动。
    pty_signal_consumer(win32::handle pipe, console_state &console, screen_buffer &screen) noexcept
        : win32::overlapped_pipe_reader(std::move(pipe)), _state(&console), _sbuf(&screen)
    {
    }

  private:
    // 解析一条完整 PtySignal 消息（2 字节 id + payload）。
    [[nodiscard]] bool try_parse_message() noexcept override;

    // 查询信号 payload 字节数；未知信号返回 0（不消费 payload）。
    [[nodiscard]] static size_t payload_size(unsigned sig) noexcept;

    // 处理一条完整信号。payload 位于 data()[consumed()..]。
    void process_signal(unsigned sig) noexcept;

    console_state *_state{};
    screen_buffer *_sbuf{};

    enum class parse_state
    {
        need_sig,     // 需要 2 字节信号 id
        need_payload, // 需要剩余 payload 字节（_need 记录）
    };
    parse_state _parse{parse_state::need_sig};
    size_t _need{};        // need_payload 时剩余 payload 字节数
    unsigned _current_sig; // need_payload 时正在处理的信号 id
};

} // namespace corehost::conpty
