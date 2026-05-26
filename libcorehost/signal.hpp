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
    std::atomic<bool> &pipe_broken; // signal 线程设置此标志以通知 I/O 循环退出
    console_state &state;
    screen_buffer &sbuf;

    pty_signal_thread_params(win32::handle signal_pipe, win32::handle vt_input, std::atomic<bool> &broken_flag,
                             console_state &console, screen_buffer &screen) noexcept
        : pipe(std::move(signal_pipe)), vt_in(std::move(vt_input)), pipe_broken(broken_flag), state(console),
          sbuf(screen)
    {
    }
};

DWORD WINAPI pty_signal_thread_proc(LPVOID param);

} // namespace conpty
