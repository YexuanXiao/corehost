// ── defterm/defterm_entry.hpp ────────────────────────────
// 默认终端入口点 & 事件循环
//
// 这是 corehost 在"默认终端协议"模式下的主循环。ConDrv 以
//   conhost.exe 0x<handle>
// 启动本程序时进入此路径。
//
// 消息循环接收 ConDrv (内核控制台驱动) 发来的 8 种 IO 消息。
// 其中只有 CONNECT 需要特别处理，决定是 COM 移交还是 mini console。
// 其他 7 种消息均用于维持 mini console 的 I/O 通道存活。

#pragma once
#include <windows.h>
#include "win32/handle.hpp"
#include "win32/event.hpp"
#include "defterm.hpp"
#include "connect.hpp"
#include "io_loop.hpp"
#include "utility/log.hpp"

namespace defterm
{

void defterm_entry(std::uintptr_t condrv_handle)
{
    LOG("defterm_entry: start, handle=0x%Ix", condrv_handle);
    assert(condrv_handle != 0);
    auto server = win32::handle_view::from_uintptr(condrv_handle);
    auto ev = win32::event{win32::create_tag, true, false};
    LOG("defterm_entry: event created=%p", ev.get());

    connect_handler handler{};
    handler.server = server;
    handler.ev = ev.view();

    LOG("defterm_entry: entering run_io_loop");
    defterm::run_io_loop(server, ev.view(), handler);
    LOG("defterm_entry: run_io_loop returned");
}

} // namespace defterm
