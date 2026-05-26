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
    win32::handle server;        // ConDrv \Server
    win32::handle vt_in;         // ReadFile ← WT (corehost reads terminal output)
    win32::handle vt_out;        // WriteFile → WT (corehost writes to terminal input)
    win32::handle event;         // InputAvailableEvent
    win32::handle signal;        // Signal pipe read handle (for resize etc.)
    win32::handle condrv_input;  // ConDrv \Input client handle; must stay alive for the session
    win32::handle condrv_output; // ConDrv \Output client handle; must stay alive for the session
    short width = conpty::default_console_size.X;
    short height = conpty::default_console_size.Y;
};

// ── com_server_entry ──────────────────────────────────────
// 注册 COM 类工厂, 阻塞等待移交, 返回句柄集合。
[[nodiscard]] handoff_result com_server_entry();

} // namespace comserver
