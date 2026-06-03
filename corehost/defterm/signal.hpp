// ── defterm/signal.hpp ────────────────────────────────────
// Ctrl+C / Break / Close 信号管道监听
//
// COM 移交后, 按键事件 (Ctrl+C / Ctrl+Break / 关闭按钮) 实际发生在
// 终端窗口中——但控制台子系统 (CSRSS) 需要被通知才能生成控制台事件
// (CTRL_C_EVENT 等) 并发送给客户端进程。
//
// 信号通道:
//   键盘 → WT 窗口 → 写入信号管道 → conhost 信号线程读取 →
//   user32!ConsoleControl → CSRSS → 客户端进程
//
// 管道断开时: 信号线程关闭 vt_in 打断 PeekNamedPipe,
//   使主 I/O 循环通过 _vt_eof 链路干净退出。
//
// 协议: 1 字节 CONSOLECONTROL + 对应数据结构体

#pragma once
#include <windows.h>
#include "win32/event.hpp"
#include "win32/handle.hpp"

namespace defterm
{

// ── 信号线程参数 ────────────────────────────────────────────
// 由调用方创建，通过 signal_thread_proc 移交所有权。

// 信号线程在管道断开时关闭 vt_in, PeekNamedPipe 检测到断管 →
// on_idle 设置 _vt_eof → api_read_console 返回 0 → cmd 退出 →
// ConDrv 断连 → read_io 返回 false → I/O 循环退出。
struct signal_thread_params
{
    win32::handle pipe;          // 信号管道读端
    win32::event shutdown_event; // 管道断开时通知 handoff 等待方
};

// ── 信号线程过程 ────────────────────────────────────────────
// 作为独立线程运行，从信号管道读取信号消息，
// 通过 user32!ConsoleControl 转发到 CSRSS。
// param 指向 signal_thread_params，函数接管其所有权。
DWORD WINAPI signal_thread_proc(LPVOID param);

} // namespace defterm
