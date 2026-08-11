// ── conpty/signal.hpp ──────────────────────────────
// WT 信号管道轮询器。
//
// 设计说明：
// 原实现为独立信号线程（pty_signal_thread_proc），从信号管道阻塞读取
// PtySignal 消息并更新 console_state/screen_buffer，与主 I/O 循环并发访问
// 共享状态。本实现把读取与处理合并到会话主线程：pty_signal_reader 提供
// 非阻塞 poll()，由 pipe_bridge 在 idle/pending 等待路径中周期调用，信号
// 到达的延迟不超过现有 16ms 轮询粒度，且消除了跨线程数据竞争。
//
// 读取策略：先读 2 字节信号 id，再按 id 精确读取对应长度的 payload，
// 每次 ReadFile 的请求长度都与消息一致，不会多读。写端（libconpty）
// 每次 WriteFile 原子写入一条完整消息，因此管道里要么没有数据，要么
// 至少有一条完整消息：每次 ReadFile 必然读满请求的字节数，读不满或
// 未知 id 都是协议损坏，抛出异常由会话入口统一处理后退出程序，等价于
// 官方 conhost PtySignalConsumer 把协议损坏当作断开并请求关闭会话的行为。
#pragma once
#include <windows.h>
#include <cstddef>
#include <span>
#include "win32/handle.hpp"
#include "win32/io.hpp"
#include "utility/log.hpp"

namespace corehost::conpty
{

enum class PtySignal
{
    // WT 通知可见性变化。corehost 当前不维护窗口可见状态，所以只消费消息。
    ShowHideWindow = 1,
    // WT 请求清空回滚/可见缓冲区。轮询路径把本地 screen_buffer 清空，并把
    // cursor 移回 viewport 左上角。
    ClearBuffer = 2,
    // WT 设置父窗口。corehost 不创建真实窗口，当前只消费消息以保持管道同步。
    SetParent = 3,
    // WT 通知终端行列变化。轮询路径更新 screen_buffer 尺寸、viewport 和
    // console_state 的有效范围。
    ResizeWindow = 8,
};

struct console_state;
struct screen_buffer;

// 非阻塞轮询 WT 信号管道，处理 PtySignal 消息。消息格式为 2 字节信号 id +
// 定长 payload。poll() 在局部缓冲上一次读尽并解析，无跨 poll 状态。
class pty_signal_reader
{
  public:
    // state/sbuf 引用必须覆盖本对象生命周期；conpty 会话持有它们，本对象
    // 只允许在会话主线程中调用 poll()。
    pty_signal_reader(console_state &state, screen_buffer &sbuf) noexcept : _state(state), _sbuf(sbuf) {}

    // 绑定信号管道读端。句柄所有权仍归 conpty 会话，本对象只保存视图。
    void set_pipe(win32::handle_view pipe) noexcept
    {
        _pipe = pipe;
    }

    [[nodiscard]] bool has_pipe() const noexcept
    {
        return _pipe.valid();
    }

    // 非阻塞读取并处理信号消息。函数不等待新数据；返回 true 表示管道已
    // 断开或不可读，会话应停止等待终端输入并按 EOF 处理。
    [[nodiscard]] bool poll();

  private:
    // 由消息 id 返回 payload 长度；0 表示未知 id（协议损坏，退出程序）。
    static size_t payload_size_for(unsigned short id) noexcept;

    // 执行一条已完整解析的信号动作。
    void handle_message(unsigned short id, std::span<const std::byte> payload);

    win32::handle_view _pipe;
    console_state &_state;
    screen_buffer &_sbuf;
};

} // namespace corehost::conpty
