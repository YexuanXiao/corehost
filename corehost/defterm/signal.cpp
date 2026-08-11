// ── defterm/signal.cpp ────────────────────────────────────
// 信号管道轮询器: 转发 CONSOLECONTROL 到 CSRSS（替代原信号线程）
//
// 读取策略：写端一次 WriteFile 一条完整消息（≤ 21 字节，原子写入），
// poll() 一次读走全部可见字节后在局部缓冲内顺序解析，无跨 poll 状态。
// 缓冲内数据不足、未知 code 或畸形 dwSize 都是协议损坏，抛出异常由
// 入口统一处理后退出程序。

#include "signal.hpp"
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include "ntapi/conwinuserrefs.h"
#include "ntapi/consolecontrol.hpp"
#include "utility/log.hpp"
#include "win32/io.hpp"

namespace corehost::defterm
{

static_assert(sizeof(CONSOLENOTIFYAPPDATA) == 8);
static_assert(sizeof(CONSOLESETFOREGROUNDDATA) == 12);
static_assert(sizeof(CONSOLEENDTASKDATA) == 16);

// 由 code 确定结构体最小长度。未知 code 返回 0，调用方按协议损坏退出
// （内容损坏检查：写端发送非 1/5/7 的 code 时触发）。
size_t console_control_forwarder::min_payload_for(CONSOLECONTROL code) noexcept
{
    switch (code)
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

bool console_control_forwarder::poll()
{
    if (!_pipe.valid())
        return false;

    for (;;)
    {
        DWORD avail = 0;
        const auto peek = win32::peek_named_pipe(_pipe, avail);
        if (peek.closed() || peek.failed())
        {
            LOG("console_control_forwarder: pipe peek failed status=%u err=%u", static_cast<unsigned>(peek.status),
                static_cast<unsigned>(peek.error));
            return true;
        }
        if (avail == 0)
            return false;

        // ── 阶段 1: 精确读 1 字节 code ──
        // Peek 已确认管道有数据（avail ≥ 1），ReadFile(1) 必然读满，无需
        // 检查读取长度；写端违反原子契约导致的数据不足由后续 dwSize 和
        // 结构体长度的检查兜底。
        std::byte code{};
        const auto code_read = win32::read_some(_pipe, std::span{&code, size_t{1}});
        if (code_read.closed() || code_read.failed())
        {
            LOG("console_control_forwarder: code read failed status=%u err=%u", static_cast<unsigned>(code_read.status),
                static_cast<unsigned>(code_read.error));
            return true;
        }

        const auto code_enum = static_cast<CONSOLECONTROL>(code);
        const auto min_payload = min_payload_for(code_enum);
        if (min_payload == 0)
        {
            LOG("console_control_forwarder: unknown code=%u; protocol corrupted", static_cast<unsigned>(code_enum));
            throw win32::error::invalid_state;
        }

        // ── 阶段 2: 精确读 4 字节 dwSize（payload 长度头）──
        DWORD dw_size = 0;
        const auto size_read = win32::read_some(_pipe, std::span{reinterpret_cast<std::byte *>(&dw_size), size_t{4}});
        if (size_read.closed() || size_read.failed())
        {
            LOG("console_control_forwarder: size read failed status=%u err=%u", static_cast<unsigned>(size_read.status),
                static_cast<unsigned>(size_read.error));
            return true;
        }
        if (size_read.bytes != 4)
        {
            LOG("console_control_forwarder: partial size header; protocol corrupted");
            throw win32::error::invalid_state;
        }
        if (dw_size < min_payload)
        {
            LOG("console_control_forwarder: malformed payload size=%lu; protocol corrupted", dw_size);
            throw win32::error::invalid_state;
        }

        // ── 阶段 3: 精确读 payload 剩余字段 ──
        // dwSize（结构体前 4 字节）已在阶段 2 读取，这里只需读结构体剩余
        // 部分 min_payload - 4 字节；读不满 = 协议损坏。
        const auto body_len = min_payload - sizeof(DWORD);
        std::array<std::byte, sizeof(CONSOLEENDTASKDATA) - sizeof(DWORD)> body{};
        const auto body_read = win32::read_some(_pipe, std::span{body}.first(body_len));
        if (body_read.closed() || body_read.failed())
        {
            LOG("console_control_forwarder: payload read failed status=%u err=%u",
                static_cast<unsigned>(body_read.status), static_cast<unsigned>(body_read.error));
            return true;
        }
        if (body_read.bytes != body_len)
        {
            LOG("console_control_forwarder: partial payload; protocol corrupted");
            throw win32::error::invalid_state;
        }

        // ── 阶段 4: 跳过 dwSize 大于结构体的扩展尾部（正常为 0）──
        auto extra = static_cast<size_t>(dw_size) - min_payload;
        std::array<std::byte, 256> sink{};
        while (extra != 0)
        {
            const auto want = std::min(extra, sink.size());
            const auto skip = win32::read_some(_pipe, std::span{sink}.first(want));
            if (skip.closed() || skip.failed())
            {
                LOG("console_control_forwarder: payload skip failed status=%u err=%u",
                    static_cast<unsigned>(skip.status), static_cast<unsigned>(skip.error));
                return true;
            }
            if (skip.bytes != want)
            {
                LOG("console_control_forwarder: partial payload tail; protocol corrupted");
                throw win32::error::invalid_state;
            }
            extra -= skip.bytes;
        }

        // ── 转发完整消息：结构体 = [dwSize(4)][body] ──
        // dwSize 字段与 body 分两次读入，这里组装回完整结构体再 memcpy，
        // 避免各 case 重复拼接。
        std::array<std::byte, sizeof(CONSOLEENDTASKDATA)> data{};
        std::memcpy(data.data(), &dw_size, sizeof(dw_size));
        std::memcpy(data.data() + sizeof(DWORD), body.data(), body_len);
        switch (code_enum)
        {
        case ConsoleNotifyConsoleApplication: {
            CONSOLENOTIFYAPPDATA d{};
            std::memcpy(&d, data.data(), sizeof(d));
            CONSOLE_PROCESS_INFO cpi{d.dwProcessID, CPI_NEWPROCESSWINDOW};
            LOG("console_control_forwarder: NotifyConsoleApplication pid=%lu", d.dwProcessID);
            console::ConsoleControl(ConsoleNotifyConsoleApplication, &cpi, sizeof(cpi));
            break;
        }
        case ConsoleSetForeground: {
            // GH#13211: 新版 WT 不再发送此消息，改为本地处理；这里只
            // 消费消息保持协议同步（老版本 WT 可能仍发送）。
            CONSOLESETFOREGROUNDDATA d{};
            std::memcpy(&d, data.data(), sizeof(d));
            LOG("console_control_forwarder: ConsoleSetForeground pid=%lu foreground=%d", d.ProcessId, d.Foreground);
            break;
        }
        case ConsoleEndTask: {
            CONSOLEENDTASKDATA d{};
            std::memcpy(&d, data.data(), sizeof(d));
            CONSOLEENDTASK c{reinterpret_cast<HANDLE>(static_cast<std::uintptr_t>(d.ProcessId)), nullptr,
                             d.ConsoleEventCode, d.ConsoleFlags};
            LOG("console_control_forwarder: ConsoleEndTask pid=%lu event=%lu flags=0x%08lx", d.ProcessId,
                d.ConsoleEventCode, d.ConsoleFlags);
            console::ConsoleControl(ConsoleEndTask, &c, sizeof(c));
            break;
        }
        default:
            break;
        }
    }
}

} // namespace corehost::defterm
