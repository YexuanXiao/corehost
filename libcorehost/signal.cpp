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

static void notify_signal_pipe_closed(pty_signal_thread_params &params) noexcept
{
    LOG("pty_signal_thread_proc: signal pipe closed, vt_in=%p", params.vt_in.get());
    params.vt_in.clear();
    params.pipe_broken.store(true, std::memory_order_relaxed);
}

DWORD WINAPI pty_signal_thread_proc(LPVOID param)
{
    auto pp = std::unique_ptr<pty_signal_thread_params>{static_cast<pty_signal_thread_params *>(param)};
    auto &hp = pp->pipe;

    for (;;)
    {
        unsigned short sig = 0;
        if (!miniio::read_exact(hp.view(), &sig, sizeof(sig)))
        {
            LOG("pty_signal_thread_proc: failed to read signal id err=%lu", ::GetLastError());
            notify_signal_pipe_closed(*pp);
            break;
        }
        switch (static_cast<PtySignal>(sig))
        {
        case PtySignal::ShowHideWindow: {
            unsigned short show = 0;
            if (!miniio::read_exact(hp.view(), &show, sizeof(show)))
            {
                LOG("pty_signal_thread_proc: ShowHideWindow payload short read err=%lu", ::GetLastError());
                notify_signal_pipe_closed(*pp);
                return 0;
            }
            break;
        }
        case PtySignal::ClearBuffer:
            pp->sbuf.clear(pp->state.default_attributes);
            break;
        case PtySignal::SetParent: {
            // 读取 HWND (8 字节, 32/64 兼容)
            ULONG_PTR hwnd = 0;
            if (!miniio::read_exact(hp.view(), &hwnd, sizeof(hwnd)))
            {
                LOG("pty_signal_thread_proc: SetParent payload short read err=%lu", ::GetLastError());
                notify_signal_pipe_closed(*pp);
                return 0;
            }
            break;
        }
        case PtySignal::ResizeWindow: {
            COORD sz{0, 0};
            if (!miniio::read_exact(hp.view(), &sz, sizeof(sz)))
            {
                LOG("pty_signal_thread_proc: ResizeWindow payload short read err=%lu", ::GetLastError());
                notify_signal_pipe_closed(*pp);
                return 0;
            }
            pp->state.screen_buffer_size = sz;
            pp->state.current_window_size = sz;
            pp->state.max_window_size = sz;
            pp->sbuf.resize(sz);
            break;
        }
        default:
            break;
        }
    }
    return 0;
}

} // namespace conpty
