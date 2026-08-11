// ── defterm/signal.hpp ────────────────────────────────────
// Ctrl+C / Break / Close 信号管道轮询器
//
// COM 移交后, 按键事件 (Ctrl+C / Ctrl+Break / 关闭按钮) 实际发生在
// 终端窗口中——但控制台子系统 (CSRSS) 需要被通知才能生成控制台事件
// (CTRL_C_EVENT 等) 并发送给客户端进程。
//
// 信号通道:
//   键盘 → WT 窗口 → 写入信号管道 → corehost 轮询读取 →
//   user32!ConsoleControl → CSRSS → 客户端进程
//
// 原实现为独立信号线程（signal_thread_proc），从管道阻塞读取并转发。本
// 实现改为非阻塞轮询器：由 handoff 等待循环在主线程周期调用 poll()，
// 管道断开同样由 poll() 返回 true 报告，消除独立线程。
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
#include "win32/handle.hpp"

using PFN_ConsoleControl = NTSTATUS(WINAPI *)(CONSOLECONTROL Command, PVOID ConsoleInformation,
                                              DWORD ConsoleInformationLength);

namespace corehost::defterm
{

// 非阻塞轮询信号管道并把 CONSOLECONTROL 消息转发到 CSRSS。消息格式为
// 1 字节 code + 4 字节 dwSize + dwSize 字节 payload；读取按
// "code → dwSize → payload"分阶段精确推进，无跨 poll 状态。
class console_control_forwarder
{
  public:
    console_control_forwarder();

    // 绑定信号管道读端。句柄所有权仍归调用方，本对象只保存视图。
    void set_pipe(win32::handle_view pipe) noexcept
    {
        _pipe = pipe;
    }

    [[nodiscard]] bool has_pipe() const noexcept
    {
        return _pipe.valid();
    }

    // 非阻塞推进消息读取并转发完整消息。函数不等待新数据；返回 true 表示
    // 管道已断开，会话应停止等待终端。
    [[nodiscard]] bool poll();

  private:
    PFN_ConsoleControl ConsoleControl;
    win32::handle_view _pipe;
};

} // namespace corehost::defterm
