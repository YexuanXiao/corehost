// ── defterm/signal.cpp ────────────────────────────────────
// 信号管道消费者实现（overlapped I/O，无线程）。
// 缓冲管理 / overlapped 读生命周期 / 断开检测由基类提供，本文件只实现
// CONSOLECONTROL 协议解析与 CSRSS 转发。

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
    switch (_parse)
    {
    case parse_state::need_code:
        if (available() - consumed() < 1)
            return false;
        _current_code = static_cast<unsigned>(static_cast<unsigned char>(data()[consumed()]));
        set_consumed(consumed() + 1);

        _need = payload_size(_current_code);
        if (_need == 0)
        {
            // 无 payload 或未知 code：直接处理。未知 code 不消费 payload，
            // 与原线程实现一致（协议同步由写端保证）。
            process_signal(_current_code);
            return true;
        }
        _parse = parse_state::need_payload;
        return true; // code 已消费；下一次迭代检查 payload 是否完整

    case parse_state::need_payload:
        if (available() - consumed() < _need)
            return false;
        {
            // payload 首 4 字节是 dwSize；必须覆盖结构体本身。
            DWORD dw_size = 0;
            std::memcpy(&dw_size, data() + consumed(), sizeof(dw_size));
            if (dw_size < _need)
            {
                LOG("signal: malformed payload size=%lu expected=%zu", dw_size, _need);
                mark_disconnected();
                return false;
            }
            _payload_offset = consumed();
            set_consumed(consumed() + _need);
            if (dw_size > _need)
            {
                // dwSize 大于结构体：跳过多余字节后再处理。
                _need = dw_size - _need;
                _parse = parse_state::need_skip;
                return true;
            }
            process_signal(_current_code);
            _parse = parse_state::need_code;
            return true;
        }

    case parse_state::need_skip:
        if (available() - consumed() < _need)
            return false;
        set_consumed(consumed() + _need);
        process_signal(_current_code);
        _parse = parse_state::need_code;
        return true;
    }
    std::unreachable();
}

void signal_consumer::process_signal(unsigned code) noexcept
{
    LOG("signal: code=%u", code);
    switch (static_cast<CONSOLECONTROL>(code))
    {
    case ConsoleNotifyConsoleApplication: {
        CONSOLENOTIFYAPPDATA d{};
        std::memcpy(&d, data() + _payload_offset, sizeof(d));
        CONSOLE_PROCESS_INFO cpi{d.dwProcessID, CPI_NEWPROCESSWINDOW};
        LOG("signal: NotifyConsoleApplication pid=%lu", d.dwProcessID);
        console::ConsoleControl(ConsoleNotifyConsoleApplication, &cpi, sizeof(cpi));
        break;
    }
    case ConsoleSetForeground: {
        CONSOLESETFOREGROUNDDATA d{};
        std::memcpy(&d, data() + _payload_offset, sizeof(d));
        LOG("signal: ConsoleSetForeground");
        break;
    }
    case ConsoleEndTask: {
        CONSOLEENDTASKDATA d{};
        std::memcpy(&d, data() + _payload_offset, sizeof(d));
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
