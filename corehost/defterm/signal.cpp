// ── defterm/signal.cpp ────────────────────────────────────
// 信号管道线程: 管道断开 → 线程退出

#include "signal.hpp"
#include <memory>
#include "miniio/io_thread.hpp"
#include "ntapi/conwinuserrefs.h"
#include "ntapi/consolecontrol.hpp"

namespace defterm
{

static bool skip_bytes(win32::handle_view p, DWORD n)
{
    BYTE buf[256];
    while (n)
    {
        auto s = std::min(n, 256ul);
        DWORD r = 0;
        if (!::ReadFile(p.get(), buf, s, &r, nullptr))
            return false;
        n -= s;
    }
    return true;
}

DWORD WINAPI signal_thread_proc(LPVOID param)
{
    auto pp = std::unique_ptr<signal_thread_params>{static_cast<signal_thread_params *>(param)};
    auto &hp = pp->pipe;

    for (;;)
    {
        std::uint8_t code = 0;
        if (!miniio::read_exact(hp.view(), &code, 1))
        {
            break;
        }

        switch (static_cast<CONSOLECONTROL>(code))
        {
        case ConsoleNotifyConsoleApplication: {
            CONSOLENOTIFYAPPDATA d{};
            if (!miniio::read_exact(hp.view(), &d, sizeof(d)))
                return 1;
            if (d.dwSize > sizeof(d) && !skip_bytes(hp.view(), d.dwSize - sizeof(d)))
                return 1;
            CONSOLE_PROCESS_INFO cpi{d.dwProcessID, CPI_NEWPROCESSWINDOW};
            console::ConsoleControl(ConsoleNotifyConsoleApplication, &cpi, sizeof(cpi));
            break;
        }
        case ConsoleSetForeground:
            break;
        case ConsoleEndTask: {
            CONSOLEENDTASKDATA d{};
            if (!miniio::read_exact(hp.view(), &d, sizeof(d)))
                return 1;
            if (d.dwSize > sizeof(d) && !skip_bytes(hp.view(), d.dwSize - sizeof(d)))
                return 1;
            CONSOLEENDTASK c{d.ProcessId, nullptr, d.ConsoleEventCode, d.ConsoleFlags};
            console::ConsoleControl(ConsoleEndTask, &c, sizeof(c));
            break;
        }
        default:
            break;
        }
    }

    return 0;
}

} // namespace defterm
