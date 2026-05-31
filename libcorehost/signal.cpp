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
    // param 由创建线程的一侧 release；线程入口重新接管所有权。
    auto pp = std::unique_ptr<pty_signal_thread_params>{static_cast<pty_signal_thread_params *>(param)};
    auto &hp = pp->pipe;

    // adopt_lock 包装的 event 在 unique_lock 析构时 SetEvent。无论正常 EOF、
    // payload 短读还是未知信号退出，主 I/O 路径都能停止等待 vt_in。
    std::unique_lock shutdown_signal{pp->shutdown_event, std::adopt_lock};

    for (;;)
    {
        // sig 是 PtySignal 的 16-bit wire id；短读表示管道关闭或协议损坏。
        unsigned short sig = 0;
        if (!miniio::read_exact(hp.view(), &sig, sizeof(sig)))
        {
            LOG("pty_signal_thread_proc: failed to read signal id err=%lu", ::GetLastError());
            break;
        }
        switch (static_cast<PtySignal>(sig))
        {
        case PtySignal::ShowHideWindow: {
            // 当前不管理真实窗口，但必须消费 show payload；否则下一次读到的
            // payload 会被误当作信号 id，破坏协议同步。
            unsigned short show = 0;
            if (!miniio::read_exact(hp.view(), &show, sizeof(show)))
            {
                LOG("pty_signal_thread_proc: ShowHideWindow payload short read err=%lu", ::GetLastError());
                return 0;
            }
            break;
        }
        case PtySignal::ClearBuffer:
            // ClearBuffer 只更新本地屏幕模型。真实终端清屏由主输出路径发 VT。
            pp->sbuf.clear(pp->state.default_attributes);
            break;
        case PtySignal::SetParent: {
            // WT 按 ULONG_PTR 发送 HWND；corehost 不重新设置父窗口，只消费字段
            // 保持后续信号边界正确。
            ULONG_PTR hwnd = 0;
            if (!miniio::read_exact(hp.view(), &hwnd, sizeof(hwnd)))
            {
                LOG("pty_signal_thread_proc: SetParent payload short read err=%lu", ::GetLastError());
                return 0;
            }
            break;
        }
        case PtySignal::ResizeWindow: {
            // ResizeWindow 是 WT 主动通知的可见尺寸。state 中三个尺寸保持一致，
            // 因为当前 ConPTY 模型没有独立 scrollback buffer。
            COORD sz{0, 0};
            if (!miniio::read_exact(hp.view(), &sz, sizeof(sz)))
            {
                LOG("pty_signal_thread_proc: ResizeWindow payload short read err=%lu", ::GetLastError());
                return 0;
            }
            pp->state.screen_buffer_size = sz;
            pp->state.max_window_size = sz;
            pp->sbuf.viewport.reset_to_buffer(sz);
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
