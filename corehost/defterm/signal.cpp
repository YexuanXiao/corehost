// ── defterm/signal.cpp ────────────────────────────────────
// 信号管道消费者实现（overlapped I/O，无线程）。
// 缓冲管理 / overlapped 读生命周期 / 断开检测 / 帧原子性检查由基类提供，
// 本文件只实现 CONSOLECONTROL 协议解析（无状态）与 CSRSS 转发。

#include "signal.hpp"
#include "ntapi/conwinuserrefs.h"
#include "ntapi/consolecontrol.hpp"
#include "utility/log.hpp"
#include <cstring>

namespace corehost::defterm
{

static_assert(sizeof(CONSOLENOTIFYAPPDATA) == 8);
static_assert(sizeof(CONSOLESETFOREGROUNDDATA) == 12);
static_assert(sizeof(CONSOLEENDTASKDATA) == 16);

size_t signal_consumer::payload_size(unsigned code) noexcept
{
    switch (static_cast<CONSOLECONTROL>(code))
    {
    case ConsoleNotifyConsoleApplication:
        return sizeof(CONSOLENOTIFYAPPDATA);
    case ConsoleSetForeground:
        return sizeof(CONSOLESETFOREGROUNDDATA);
    case ConsoleEndTask:
        return sizeof(CONSOLEENDTASKDATA);
    default:
        return 0;
    }
}

bool signal_consumer::try_parse_message() noexcept
{
    const size_t pos = consumed();
    const size_t n = available() - pos;
    if (n < 1)
        return false; // 不足一帧：帧边界或半帧残留（基类区分）

    const unsigned code = static_cast<unsigned>(static_cast<unsigned char>(data()[pos]));
    const size_t need = payload_size(code);
    if (need == 0)
    {
        // 无 payload 或未知 code：直接处理。未知 code 不消费 payload，
        // 与原线程实现一致（协议同步由写端保证）。
        set_consumed(pos + 1);
        process_signal(code, nullptr);
        return true;
    }
    if (n < 1 + need)
        return false;

    // payload 首 4 字节是 dwSize；必须覆盖结构体本身。
    DWORD dw_size = 0;
    std::memcpy(&dw_size, data() + pos + 1, sizeof(dw_size));
    if (dw_size < need)
    {
        LOG("signal: malformed payload size=%lu expected=%zu", dw_size, need);
        mark_disconnected();
        return false;
    }

    // 帧原子性：完整帧（code + dwSize 声明的全部 payload）一次 WriteFile
    // 到达，直接整体消费（含 dwSize 大于结构体的扩展字节）。
    if (n < 1 + dw_size)
        return false;

    set_consumed(pos + 1 + dw_size);
    process_signal(code, data() + pos + 1);
    return true;
}

void signal_consumer::process_signal(unsigned code, const std::byte *payload) noexcept
{
    LOG("signal: code=%u", code);
    switch (static_cast<CONSOLECONTROL>(code))
    {
    case ConsoleNotifyConsoleApplication: {
        CONSOLENOTIFYAPPDATA d{};
        std::memcpy(&d, payload, sizeof(d));
        CONSOLE_PROCESS_INFO cpi{d.dwProcessID, CPI_NEWPROCESSWINDOW};
        LOG("signal: NotifyConsoleApplication pid=%lu", d.dwProcessID);
        console::ConsoleControl(ConsoleNotifyConsoleApplication, &cpi, sizeof(cpi));
        break;
    }
    case ConsoleSetForeground: {
        CONSOLESETFOREGROUNDDATA d{};
        std::memcpy(&d, payload, sizeof(d));
        LOG("signal: ConsoleSetForeground");
        break;
    }
    case ConsoleEndTask: {
        CONSOLEENDTASKDATA d{};
        std::memcpy(&d, payload, sizeof(d));
        CONSOLEENDTASK c{reinterpret_cast<HANDLE>(static_cast<std::uintptr_t>(d.ProcessId)), nullptr,
                         d.ConsoleEventCode, d.ConsoleFlags};
        LOG("signal: ConsoleEndTask pid=%lu event=%lu flags=0x%08lx", d.ProcessId, d.ConsoleEventCode, d.ConsoleFlags);
        console::ConsoleControl(ConsoleEndTask, &c, sizeof(c));
        break;
    }
    default:
        LOG("signal: unknown code=%u ignored", code);
        break;
    }
}

} // namespace corehost::defterm
