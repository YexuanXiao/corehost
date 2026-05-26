// ── conpty/signal.cpp ──────────────────────────────
// PtySignal 管道线程实现
#include "signal.hpp"
#include "console_state.hpp"
#include "screen_buffer.hpp"
#include "miniio/io_thread.hpp"
#include "utility/log.hpp"
#include <algorithm>
#include <memory>

namespace conpty
{

DWORD WINAPI pty_signal_thread_proc(LPVOID param)
{
    auto pp = std::unique_ptr<pty_signal_thread_params>{static_cast<pty_signal_thread_params *>(param)};
    auto &hp = pp->pipe;
    auto *state = pp->state;
    auto *sbuf = pp->sbuf;

    for (;;)
    {
        unsigned short sig = 0;
        if (!miniio::read_exact(hp.view(), &sig, sizeof(sig)))
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
            if (!miniio::read_exact(hp.view(), &show, sizeof(show)))
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
            if (!miniio::read_exact(hp.view(), &hwnd, sizeof(hwnd)))
                return 1;
            break;
        }
        case PtySignal::ResizeWindow: {
            COORD sz{0, 0};
            if (!miniio::read_exact(hp.view(), &sz, sizeof(sz)))
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
