// ── conpty/signal.hpp ──────────────────────────────
// PtySignal 管道线程 (char32_t 版本)
//
// 与 conpty/signal.hpp 相同 — 无文本编码依赖。
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
    win32::handle pipe;
    win32::event shutdown_event;
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
