// ── defterm/signal.cpp ────────────────────────────────────
// 信号转发线程：把终端写入的 CONSOLECONTROL 消息转发到 CSRSS。
//
// 读取策略：写端一次 WriteFile 一条完整消息（≤ 21 字节，原子写入），本线程
// 按 "code → dwSize → payload" 分阶段精确读取。数据不足、未知 code 或畸形
// dwSize 都是协议损坏，抛出异常由入口统一处理后退出程序。
//
// 读取是阻塞的：没有消息时线程不唤醒（见 doc/design/0001）。

#include "signal.hpp"
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include "ntapi/conwinuserrefs.h"
#include "utility/log.hpp"
#include "win32/error.hpp"
#include "win32/wait.hpp"

namespace corehost::defterm
{

static_assert(sizeof(CONSOLENOTIFYAPPDATA) == 8);
static_assert(sizeof(CONSOLESETFOREGROUNDDATA) == 12);
static_assert(sizeof(CONSOLEENDTASKDATA) == 16);

// 由 code 确定结构体最小长度。未知 code 返回 0，调用方按协议损坏退出。
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

// 阻塞读取一条完整消息并转发。返回 false 表示管道断开或读被取消。
bool console_control_forwarder::read_and_forward(win32::cancellable_read &reader)
{
    // ── 阶段 1: 精确读 1 字节 code ──
    std::byte code_byte{};
    DWORD read = 0;
    auto status = reader.read_exact(std::span{&code_byte, size_t{1}}, read);
    if (status != win32::cancellable_read::status::data)
        return false;

    const auto code = static_cast<CONSOLECONTROL>(code_byte);
    const auto min_payload = min_payload_for(code);
    if (min_payload == 0)
    {
        LOG("console_control_forwarder: unknown code=%u; protocol corrupted", static_cast<unsigned>(code));
        win32::throw_last_error(win32::error::invalid_state);
    }

    // ── 阶段 2: 精确读 4 字节 dwSize ──
    DWORD dw_size = 0;
    status = reader.read_exact(std::span{reinterpret_cast<std::byte *>(&dw_size), size_t{4}}, read);
    if (status == win32::cancellable_read::status::cancelled)
        return false;
    if (status != win32::cancellable_read::status::data)
    {
        LOG("console_control_forwarder: partial size header; protocol corrupted");
        win32::throw_last_error(win32::error::invalid_state);
    }
    if (dw_size < min_payload)
    {
        LOG("console_control_forwarder: malformed payload size=%lu; protocol corrupted", dw_size);
        win32::throw_last_error(win32::error::invalid_state);
    }

    // ── 阶段 3: 精确读结构体剩余字段 ──
    const auto body_len = min_payload - sizeof(DWORD);
    std::array<std::byte, sizeof(CONSOLEENDTASKDATA) - sizeof(DWORD)> body{};
    status = reader.read_exact(std::span{body}.first(body_len), read);
    if (status == win32::cancellable_read::status::cancelled)
        return false;
    if (status != win32::cancellable_read::status::data)
    {
        LOG("console_control_forwarder: partial payload; protocol corrupted");
        win32::throw_last_error(win32::error::invalid_state);
    }

    // ── 阶段 4: 跳过 dwSize 大于结构体的扩展尾部（正常为 0）──
    auto extra = static_cast<size_t>(dw_size) - min_payload;
    std::array<std::byte, 256> sink{};
    while (extra != 0)
    {
        const auto want = std::min(extra, sink.size());
        status = reader.read_exact(std::span{sink}.first(want), read);
        if (status == win32::cancellable_read::status::cancelled)
            return false;
        if (status != win32::cancellable_read::status::data)
        {
            LOG("console_control_forwarder: partial payload tail; protocol corrupted");
            win32::throw_last_error(win32::error::invalid_state);
        }
        extra -= want;
    }

    forward_message(code, std::span{body}.first(body_len), body_len);
    return true;
}

// 把完整消息转发给 CSRSS。结构体 = [dwSize(4)][body]。
void console_control_forwarder::forward_message(CONSOLECONTROL code, std::span<const std::byte> body, size_t body_len)
{
    switch (code)
    {
    case ConsoleNotifyConsoleApplication: {
        CONSOLENOTIFYAPPDATA d{};
        std::memcpy(reinterpret_cast<std::byte *>(&d) + sizeof(DWORD), body.data(), body_len);
        CONSOLE_PROCESS_INFO cpi{d.dwProcessID, CPI_NEWPROCESSWINDOW};
        LOG("console_control_forwarder: NotifyConsoleApplication pid=%lu", d.dwProcessID);
        console_control()(ConsoleNotifyConsoleApplication, &cpi, sizeof(cpi));
        break;
    }
    case ConsoleSetForeground: {
        // GH#13211: 新版 WT 不再发送此消息，改为本地处理；这里只
        // 消费消息保持协议同步（老版本 WT 可能仍发送）。
        CONSOLESETFOREGROUNDDATA d{};
        std::memcpy(reinterpret_cast<std::byte *>(&d) + sizeof(DWORD), body.data(), body_len);
        LOG("console_control_forwarder: ConsoleSetForeground pid=%lu foreground=%d", d.ProcessId, d.Foreground);
        break;
    }
    case ConsoleEndTask: {
        CONSOLEENDTASKDATA d{};
        std::memcpy(reinterpret_cast<std::byte *>(&d) + sizeof(DWORD), body.data(), body_len);
        CONSOLEENDTASK c{reinterpret_cast<HANDLE>(static_cast<std::uintptr_t>(d.ProcessId)), nullptr,
                         d.ConsoleEventCode, d.ConsoleFlags};
        LOG("console_control_forwarder: ConsoleEndTask pid=%lu event=%lu flags=0x%08lx", d.ProcessId,
            d.ConsoleEventCode, d.ConsoleFlags);
        console_control()(ConsoleEndTask, &c, sizeof(c));
        break;
    }
    default:
        break;
    }
}

// ── 信号转发线程 ─────────────────────────────────────────
void console_control_thread::start()
{
    _thread = win32::basic_thread{&console_control_thread::entry, this, {.name = L"corehost-handoff-signal"}};
}

void console_control_thread::stop_and_join()
{
    assert(_thread.valid());
    _reader.request_stop();
    _reader.wait_for_exit(_thread);
    _thread.join();
}

DWORD WINAPI console_control_thread::entry(LPVOID self)
{
    static_cast<console_control_thread *>(self)->run();
    return 0;
}

void console_control_thread::run()
{
    LOG("console_control_thread: enter");
    while (_forwarder.read_and_forward(_reader))
    {
    }

    // 管道断开（或停止请求）：唤醒 handoff 等待方。
    if (::SetEvent(_closed_event.get()) == FALSE)
        win32::throw_last_error();
    LOG("console_control_thread: exit");
}

} // namespace corehost::defterm