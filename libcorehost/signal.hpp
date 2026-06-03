// ── conpty/signal.hpp ──────────────────────────────
// PtySignal 管道线程。
//
// 功能分解：
// 1. 从 WT 信号管道读取 PtySignal id 和对应 payload。
// 2. ClearBuffer/ResizeWindow 直接更新 screen_buffer 和 console_state。
// 3. 管道关闭或短读时退出线程，并置位 shutdown_event。
#pragma once
#include <windows.h>
#include "win32/event.hpp"
#include "win32/handle.hpp"

namespace conpty
{

enum class PtySignal
{
    // WT 通知可见性变化。corehost 当前不维护窗口可见状态，所以只消耗消息。
    ShowHideWindow = 1,
    // WT 请求清空回滚/可见缓冲区。信号线程把本地 screen_buffer 清空，并把
    // cursor 移回 viewport 左上角。
    ClearBuffer = 2,
    // WT 设置父窗口。corehost 不创建真实窗口，当前只消耗消息以保持管道同步。
    SetParent = 3,
    // WT 通知终端行列变化。信号线程更新 screen_buffer 尺寸、viewport 和
    // console_state.cursor 的有效范围。
    ResizeWindow = 8,
};

struct console_state;
struct screen_buffer;

struct pty_signal_thread_params
{
    // pipe 是 WT 信号管道读端，线程独占所有权。
    win32::handle pipe;

    // 线程退出时置位，唤醒主 I/O 路径停止等待。
    win32::event shutdown_event;

    // state/sbuf 由 run_conpty_session 持有，信号线程只在会话存活期间访问。
    console_state &state;
    screen_buffer &sbuf;

    // 构造信号线程参数并转移 pipe/event 所有权。console/screen 引用必须覆盖
    // 线程生命周期，run_conpty_session 通过 basic_thread 等待线程退出保证这一点。
    pty_signal_thread_params(win32::handle signal_pipe, win32::event signal_shutdown_event, console_state &console,
                             screen_buffer &screen) noexcept
        : pipe(std::move(signal_pipe)), shutdown_event(std::move(signal_shutdown_event)), state(console), sbuf(screen)
    {
    }
};

// PtySignal 线程入口。param 必须指向 pty_signal_thread_params；线程接管该
// 指针所有权，并在退出时通过 shutdown_event 唤醒主 I/O 循环。
DWORD WINAPI pty_signal_thread_proc(LPVOID param);

} // namespace conpty
