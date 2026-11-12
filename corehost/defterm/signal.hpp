// ── defterm/signal.hpp ────────────────────────────────────
// Ctrl+C / Break / Close 信号管道转发
//
// COM 移交后, 按键事件 (Ctrl+C / Ctrl+Break / 关闭按钮) 实际发生在
// 终端窗口中——但控制台子系统 (CSRSS) 需要被通知才能生成控制台事件
// (CTRL_C_EVENT 等) 并发送给客户端进程。
//
// 信号通道:
//   键盘 → WT 窗口 → 写入信号管道 → corehost 信号线程阻塞读取 →
//   user32!ConsoleControl → CSRSS → 客户端进程
//
// 由独立信号线程从管道阻塞读取并转发；管道断开时置位 closed_event，
// handoff 等待循环据此退出。
//
// 协议（与 WT 的 RemoteConsoleControl/HostSignals 一致，code 值等同
// CONSOLECONTROL 枚举）:
//   code=1 NotifyApp      {u32 size, u32 processId}
//   code=5 SetForeground  {u32 size, u32 processId, bool}（新 WT 不再发送）
//   code=7 EndTask        {u32 size, u32 processId, u32 eventType, u32 ctrlFlags}
//
// 读取策略：先读 1 字节 code，再读 4 字节 dwSize，最后按 dwSize 精确
// 读取 payload，每次 ReadFile 的请求长度都与消息一致，不会多读。写端
// （WT RemoteConsoleControl）每次 WriteFile 原子写入一条完整消息，因此
// 管道里要么没有数据，要么至少有一条完整消息：每次 ReadFile 必然读满
// 请求的字节数，读不满、未知 code 或畸形 dwSize 都是协议损坏，抛出
// 异常由入口统一处理后退出程序（官方 conhost 抛 E_UNEXPECTED/
// E_ILLEGAL_METHOD_CALL 后崩溃，行为一致）。

#pragma once
#include <windows.h>
#include <cstddef>
#include "ntapi/conwinuserrefs.h"
#include "ntapi/consolecontrol.hpp"
#include "win32/cancellable_read.hpp"
#include "win32/handle.hpp"
#include "win32/thread.hpp"


namespace corehost::defterm
{

// 非阻塞轮询信号管道并把 CONSOLECONTROL 消息转发到 CSRSS。消息格式为
// 1 字节 code + 4 字节 dwSize + dwSize 字节 payload；读取按
// "code → dwSize → payload"分阶段精确推进，无跨 poll 状态。
class console_control_forwarder
{
  public:
    console_control_forwarder() : _console_control{resolve_console_control()} {}

    // 绑定信号管道读端。句柄所有权仍归调用方，本对象只保存视图。
    void set_pipe(win32::handle_view pipe) noexcept
    {
        _pipe = pipe;
    }


    // 阻塞读取并转发一条完整消息（信号线程使用）。返回 false 表示管道
    // 已断开或读被取消，调用方应结束循环。
    [[nodiscard]] bool read_and_forward(win32::cancellable_read &reader);

  private:
    // 由 code 确定结构体最小长度；未知 code 返回 0（协议损坏）。
    static size_t min_payload_for(CONSOLECONTROL code) noexcept;

    // 把一条已完整读出的消息转发给 CSRSS。
    void forward_message(CONSOLECONTROL code, std::span<const std::byte> body, size_t body_len);

    PFN_ConsoleControl _console_control = nullptr;
    win32::handle_view _pipe;
};

// 手递手（defterm）阶段的信号转发线程：阻塞读取终端写入的
// Ctrl+C/Break/Close 消息并转发到 CSRSS。管道断开时置位 closed_event。
class console_control_thread
{
  public:
    console_control_thread(console_control_forwarder &forwarder, win32::handle_view pipe,
                           win32::handle_view closed_event)
        : _reader{pipe}, _forwarder{forwarder}, _closed_event{closed_event}
    {
    }

    console_control_thread(const console_control_thread &) = delete;
    console_control_thread &operator=(const console_control_thread &) = delete;

    void start();
    void stop_and_join();

  private:
    static DWORD WINAPI entry(LPVOID self);
    void run();

    win32::cancellable_read _reader;
    console_control_forwarder &_forwarder;
    win32::handle_view _closed_event;
    win32::basic_thread _thread;
};

} // namespace corehost::defterm