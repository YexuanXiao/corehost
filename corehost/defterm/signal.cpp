// ── defterm/signal.cpp ────────────────────────────────────
// 信号管道线程: 管道断开 → 线程退出

#include "signal.hpp"
#include <memory>
#include "miniio/io_thread.hpp"
#include "ntapi/conwinuserrefs.h"
#include "ntapi/consolecontrol.hpp"
#include "utility/log.hpp"

namespace defterm
{

static_assert(sizeof(CONSOLENOTIFYAPPDATA) == 8);
static_assert(sizeof(CONSOLESETFOREGROUNDDATA) == 12);
static_assert(sizeof(CONSOLEENDTASKDATA) == 16);

bool skip_bytes(win32::handle_view p, DWORD n)
{
    BYTE buf[256];
    while (n)
    {
        auto s = std::min(n, 256ul);
        DWORD r = 0;
        if (!::ReadFile(p.get(), buf, s, &r, nullptr))
        {
            LOG("signal skip_bytes: ReadFile failed err=%lu remaining=%lu", ::GetLastError(), n);
            return false;
        }
        n -= s;
    }
    return true;
}

template <typename T>
bool read_remote_console_payload(win32::handle_view pipe, T &payload)
{
    if (!miniio::read_exact(pipe, &payload, sizeof(payload)))
    {
        LOG("signal_thread_proc: failed payload read size=%zu err=%lu", sizeof(payload), ::GetLastError());
        return false;
    }

    if (payload.dwSize < sizeof(payload))
    {
        LOG("signal_thread_proc: malformed payload size=%lu expected=%zu", payload.dwSize, sizeof(payload));
        return false;
    }

    if (payload.dwSize > sizeof(payload) &&
        !skip_bytes(pipe, payload.dwSize - static_cast<DWORD>(sizeof(payload))))
    {
        return false;
    }

    return true;
}

DWORD WINAPI signal_thread_proc(LPVOID param)
{
    auto pp = std::unique_ptr<signal_thread_params>{static_cast<signal_thread_params *>(param)};
    auto &hp = pp->pipe;
    auto &shutdown_event = pp->shutdown_event;
    LOG("signal_thread_proc: start pipe=%p shutdownEvent=%p", hp.get(), shutdown_event.get());

    bool keep_running = true;
    while (keep_running)
    {
        std::uint8_t code = 0;
        if (!miniio::read_exact(hp.view(), &code, 1))
        {
            LOG("signal_thread_proc: pipe closed err=%lu", ::GetLastError());
            break;
        }

        LOG("signal_thread_proc: code=%u", static_cast<unsigned>(code));
        switch (static_cast<CONSOLECONTROL>(code))
        {
        case ConsoleNotifyConsoleApplication: {
            CONSOLENOTIFYAPPDATA d{};
            if (!read_remote_console_payload(hp.view(), d))
            {
                keep_running = false;
                break;
            }
            CONSOLE_PROCESS_INFO cpi{d.dwProcessID, CPI_NEWPROCESSWINDOW};
            LOG("signal_thread_proc: NotifyConsoleApplication pid=%lu", d.dwProcessID);
            console::ConsoleControl(ConsoleNotifyConsoleApplication, &cpi, sizeof(cpi));
            break;
        }
        case ConsoleSetForeground: {
            CONSOLESETFOREGROUNDDATA d{};
            if (!read_remote_console_payload(hp.view(), d))
            {
                keep_running = false;
                break;
            }
            LOG("signal_thread_proc: ConsoleSetForeground");
            break;
        }
        case ConsoleEndTask: {
            CONSOLEENDTASKDATA d{};
            if (!read_remote_console_payload(hp.view(), d))
            {
                keep_running = false;
                break;
            }
            CONSOLEENDTASK c{reinterpret_cast<HANDLE>(static_cast<std::uintptr_t>(d.ProcessId)), nullptr,
                             d.ConsoleEventCode, d.ConsoleFlags};
            LOG("signal_thread_proc: ConsoleEndTask pid=%lu event=%lu flags=0x%08lx", d.ProcessId,
                d.ConsoleEventCode, d.ConsoleFlags);
            console::ConsoleControl(ConsoleEndTask, &c, sizeof(c));
            break;
        }
        default:
            LOG("signal_thread_proc: unknown code=%u ignored", static_cast<unsigned>(code));
            break;
        }
    }

    LOG("signal_thread_proc: signaling shutdown event=%p", shutdown_event.get());
    if (!::SetEvent(shutdown_event.get()))
        LOG("signal_thread_proc: SetEvent failed err=%lu", ::GetLastError());
    LOG("signal_thread_proc: exit");
    return 0;
}

} // namespace defterm
