// ── conpty/signal.hpp ──────────────────────────────
// WT 信号管道轮询器。
//
// 设计说明：
// 原实现为独立信号线程（pty_signal_thread_proc），从信号管道阻塞读取
// PtySignal 消息并更新 console_state/screen_buffer，与主 I/O 循环并发访问
// 共享状态。本实现把读取与处理合并到会话主线程：pty_signal_reader 提供
// 阻塞读取，由信号线程独占调用；没有数据时线程不唤醒，信号
// 到达的延迟不超过现有 16ms 轮询粒度，且消除了跨线程数据竞争。
//
// 读取策略：信号线程先精确读 2 字节 id，再按 payload_size_for(id) 精确读
// payload；两次读取保证不会把相邻消息切断或合并。
#pragma once
#include <windows.h>
#include <cstddef>
#include <cstdint>
#include <span>
#include "win32/handle.hpp"
#include "win32/io.hpp"
#include "utility/log.hpp"

namespace corehost::conpty
{

// SetParent 的 payload 是 uint64_t；其余信号不超过它。
inline constexpr std::size_t max_signal_payload_size = sizeof(std::uint64_t);

enum class PtySignal
{
    // WT 通知可见性变化。message_router 把它转成伪窗口的 ShowWindow 请求。
    ShowHideWindow = 1,
    // WT 请求清空回滚/可见缓冲区。轮询路径把本地 screen_buffer 清空，并把
    // cursor 移回 viewport 左上角。
    ClearBuffer = 2,
    // WT 设置父窗口。message_router 更新伪窗口的 GWLP_HWNDPARENT。
    SetParent = 3,
    // WT 通知终端行列变化。轮询路径更新 screen_buffer 尺寸、viewport 和
    // console_state 的有效范围。
    ResizeWindow = 8,
};

struct console_state;
struct screen_buffer;

// 非阻塞轮询 WT 信号管道，处理 PtySignal 消息。消息格式为 2 字节信号 id +
// 定长 payload。apply() 只应用一条已完整读出的消息，由信号线程调用。
class pty_signal_reader
{
  public:
    // state/sbuf 引用必须覆盖本对象生命周期；conpty 会话持有它们，本对象
    // 只允许在会话锁内调用 apply()（信号线程持有会话锁）。
    pty_signal_reader(console_state &state, screen_buffer &sbuf) noexcept : _state(state), _sbuf(sbuf)
    {
    }

    // 绑定信号管道读端。句柄所有权仍归 conpty 会话，本对象只保存视图。
    void set_pipe(win32::handle_view pipe) noexcept
    {
        _pipe = pipe;
    }


    // 由消息 id 返回 payload 长度；0 表示未知 id（协议损坏，退出程序）。
    // 公开给信号线程：它需要在读 payload 之前知道精确长度。
    static size_t payload_size_for(unsigned short id) noexcept;

    // 应用一条已完整读出的信号消息。调用方必须持有会话锁。
    void apply(unsigned short id, std::span<const std::byte> payload);

  private:
    // 应用一条已完整解析的信号。


    win32::handle_view _pipe;
    console_state &_state;
    screen_buffer &_sbuf;
};

} // namespace corehost::conpty