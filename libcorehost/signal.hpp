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
    ShowHideWindow = 1,
    ClearBuffer = 2,
    SetParent = 3,
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

    pty_signal_thread_params(win32::handle signal_pipe, win32::event signal_shutdown_event, console_state &console,
                             screen_buffer &screen) noexcept
        : pipe(std::move(signal_pipe)), shutdown_event(std::move(signal_shutdown_event)), state(console), sbuf(screen)
    {
    }
};

DWORD WINAPI pty_signal_thread_proc(LPVOID param);

} // namespace conpty
