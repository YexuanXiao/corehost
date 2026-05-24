// ── conpty/signal.cpp ──────────────────────────────
// PtySignal 管道线程实现
#include "signal.hpp"
#include "console_state.hpp"
#include "screen_buffer.hpp"
#include "utility/log.hpp"
#include <algorithm>
#include <memory>

namespace conpty
{

bool read_exact(win32::handle_view p, void *b, DWORD s)
{
    DWORD r = 0;
    if (!::ReadFile(p.get(), b, s, &r, nullptr))
        return false;
    return r == s;
}

bool peek_signal(win32::handle_view p, unsigned short &sig_out)
{
    DWORD avail = 0;
    if (!::PeekNamedPipe(p.get(), nullptr, 0, nullptr, &avail, nullptr))
        return false;
    if (avail < sizeof(unsigned short))
        return false;
    return read_exact(p, &sig_out, sizeof(sig_out));
}

void drain_pending_signals(win32::handle_view hp, console_state *state, screen_buffer *sbuf)
{
    DWORD avail = 0;
    if (!::PeekNamedPipe(hp.get(), nullptr, 0, nullptr, &avail, nullptr))
        return;
    LOG("[signal] drain_pending: %lu bytes in pipe", avail);

    unsigned short sig = 0;
    while (peek_signal(hp, sig))
    {
        LOG("[signal] drain_pending: signal=%u", static_cast<unsigned>(sig));
        switch (static_cast<PtySignal>(sig))
        {
        case PtySignal::ShowHideWindow: {
            unsigned short show = 0;
            read_exact(hp, &show, sizeof(show));
            LOG("[signal] drain_pending: ShowHideWindow show=%u", static_cast<unsigned>(show));
            break;
        }
        case PtySignal::ClearBuffer:
            if (sbuf)
                sbuf->clear(state ? state->default_attributes : 0x07);
            break;
        case PtySignal::SetParent: {
            ULONG_PTR hwnd = 0;
            read_exact(hp, &hwnd, sizeof(hwnd));
            break;
        }
        case PtySignal::ResizeWindow: {
            COORD sz{0, 0};
            if (!read_exact(hp, &sz, sizeof(sz)))
                return;
            LOG("[signal] drain_pending: ResizeWindow new=(%d,%d)", sz.X, sz.Y);
            if (state && sz.X > 0 && sz.Y > 0)
            {
                state->screen_buffer_size = sz;
                state->current_window_size = sz;
                state->max_window_size = sz;
                if (sbuf)
                    sbuf->resize(sz);
            }
            break;
        }
        default:
            break;
        }
    }
}

DWORD WINAPI pty_signal_thread_proc(LPVOID param)
{
    auto pp = std::unique_ptr<pty_signal_thread_params>{static_cast<pty_signal_thread_params *>(param)};
    auto &hp = pp->pipe;
    auto *state = pp->state;
    auto *sbuf = pp->sbuf;

    for (;;)
    {
        unsigned short sig = 0;
        if (!read_exact(hp.view(), &sig, sizeof(sig)))
        {
            pp->vt_in.clear();
            if (pp->pipe_broken)
                pp->pipe_broken->store(true, std::memory_order_relaxed);
            break;
        }
        switch (static_cast<PtySignal>(sig))
        {
        case PtySignal::ShowHideWindow: {
            unsigned short show = 0;
            if (!read_exact(hp.view(), &show, sizeof(show)))
                return 1;
            break;
        }
        case PtySignal::ClearBuffer:
            if (sbuf)
                sbuf->clear(state ? state->default_attributes : 0x07);
            break;
        case PtySignal::SetParent: {
            // 读取 HWND (8 字节, 32/64 兼容)
            ULONG_PTR hwnd = 0;
            if (!read_exact(hp.view(), &hwnd, sizeof(hwnd)))
                return 1;
            break;
        }
        case PtySignal::ResizeWindow: {
            COORD sz{0, 0};
            if (!read_exact(hp.view(), &sz, sizeof(sz)))
                return 1;
            if (state)
            {
                state->screen_buffer_size = sz;
                state->current_window_size = sz;
                state->max_window_size = sz;
                if (sbuf)
                    sbuf->resize(sz);
            }
            break;
        }
        default:
            break;
        }
    }
    return 0;
}

} // namespace conpty
