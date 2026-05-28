// ── conpty/signal.cpp ──────────────────────────────
// PtySignal 管道线程实现
#include "signal.hpp"
#include "console_state.hpp"
#include "screen_buffer.hpp"
#include "miniio/io_thread.hpp"
#include "utility/log.hpp"
#include <algorithm>
#include <memory>
#include <mutex>

namespace conpty
{

DWORD WINAPI pty_signal_thread_proc(LPVOID param)
{
    auto pp = std::unique_ptr<pty_signal_thread_params>{static_cast<pty_signal_thread_params *>(param)};
    auto &hp = pp->pipe;
    std::unique_lock shutdown_signal{pp->shutdown_event, std::adopt_lock};

    for (;;)
    {
        unsigned short sig = 0;
        if (!miniio::read_exact(hp.view(), &sig, sizeof(sig)))
        {
            LOG("pty_signal_thread_proc: failed to read signal id err=%lu", ::GetLastError());
            break;
        }
        switch (static_cast<PtySignal>(sig))
        {
        case PtySignal::ShowHideWindow: {
            unsigned short show = 0;
            if (!miniio::read_exact(hp.view(), &show, sizeof(show)))
            {
                LOG("pty_signal_thread_proc: ShowHideWindow payload short read err=%lu", ::GetLastError());
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
                return 0;
            }
            break;
        }
        case PtySignal::ResizeWindow: {
            COORD sz{0, 0};
            if (!miniio::read_exact(hp.view(), &sz, sizeof(sz)))
            {
                LOG("pty_signal_thread_proc: ResizeWindow payload short read err=%lu", ::GetLastError());
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
