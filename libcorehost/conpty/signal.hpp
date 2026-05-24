// ── conpty/signal.hpp ──────────────────────────────
// PtySignal 管道线程 (char32_t 版本)
//
// 与 conpty/signal.hpp 相同 — 无文本编码依赖。
#pragma once
#include <windows.h>
#include <atomic>
#include "win32/handle.hpp"

namespace conpty
{

enum class PtySignal : unsigned short
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
    win32::handle pipe;
    win32::handle vt_in;
    std::atomic<bool> *pipe_broken = nullptr; // signal 线程设置此标志以通知 I/O 循环退出
    console_state *state = nullptr;
    screen_buffer *sbuf = nullptr;
};

// 同步排空信号管道中已排队的 PtySignal (在启动信号线程前调用)
// 确保 ResizeWindow 等信号在 I/O 循环处理首个 API 调用前已生效
void drain_pending_signals(win32::handle_view signal_pipe, console_state *state, screen_buffer *sbuf);

DWORD WINAPI pty_signal_thread_proc(LPVOID param);

} // namespace conpty
