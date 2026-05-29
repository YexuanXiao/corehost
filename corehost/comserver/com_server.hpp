// ── comserver/com_server.hpp ──────────────────────────────
// -Embedding COM 服务器: 接收 inbox conhost 的控制台会话移交。
// 仅暴露 handoff_result 和 com_server_entry()。

#pragma once
#include "win32/handle.hpp"
#include "com/com_ptr.hpp"
#include "ITerminalHandoff.h"
#include "default_console_size.hpp"

namespace comserver
{

// ── handoff_result ────────────────────────────────────────
struct handoff_result
{
    // 空句柄表示 COM handoff 没有完成；非空时所有权转移给调用方，
    // 调用方必须把它传给 conpty_entry/run_conpty_session 并保持到会话结束。
    win32::handle server; // ConDrv \Server

    // vt_in/vt_out 按 corehost 的视角命名：
    //   vt_in  : corehost 从 WT 读取输入/控制字节。
    //   vt_out : corehost 向 WT 写入 VT 输出字节。
    win32::handle vt_in;
    win32::handle vt_out;

    // ConDrv InputAvailableEvent。非空时已经由 inbox conhost 创建并注册。
    win32::handle event;

    // WT 信号管道读端；非空时 conpty signal 线程用它接收 resize/close 等信号。
    win32::handle signal;

    // ConDrv \Input/\Output 客户端句柄。非空时说明 CONNECT 已经完成；
    // 必须与 conpty 会话同寿命，否则客户端 I/O 会断开。
    win32::handle condrv_input;
    win32::handle condrv_output;

    // 0 不合法；默认值来自 conpty::default_console_size。WT handoff 当前只
    // 支持字符尺寸，不支持像素尺寸。
    short width = conpty::default_console_size.X;
    short height = conpty::default_console_size.Y;
};

// ── com_server_entry ──────────────────────────────────────
// 注册 COM 类工厂并阻塞等待 WT 调用 EstablishHandoff。返回值中句柄为空
// 表示没有成功移交；非空句柄的所有权属于调用方。
[[nodiscard]] handoff_result com_server_entry();

} // namespace comserver
