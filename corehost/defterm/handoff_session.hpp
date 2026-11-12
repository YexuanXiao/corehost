// ── defterm/handoff_session.hpp ────────────────────────────
// 默认终端 handoff 的**协议执行**部分：拿到 IConsoleHandoff 之后，创建信号
// 管道、调用 EstablishHandoff、启动信号转发线程，并等待终端进程退出或信号
// 管道断开。
//
// 与 COM 创建分离（terminal_handoff.hpp 负责 CoCreateInstance 与 marker
// 检查）；这一段只执行协议，不依赖注册表或真实终端。
#pragma once
#include <windows.h>

#include <IConsoleHandoff.h>

#include "signal.hpp"
#include "condrv_io.hpp"
#include "utility/log.hpp"
#include "win32/error.hpp"
#include "win32/event.hpp"
#include "win32/handle.hpp"
#include "win32/hresult.hpp"
#include "win32/wait.hpp"

namespace corehost::defterm
{

[[nodiscard]] inline CONSOLE_PORTABLE_ATTACH_MSG make_portable_attach_msg(const condrv_io::io_msg &msg) noexcept
{
    // CONSOLE_PORTABLE_ATTACH_MSG 是默认终端 COM 协议可跨进程传输的
    // CD_IO_DESCRIPTOR 子集。它不包含 CONNECT body，因此标题/showWindow
    // 需要在接收端用 READ_INPUT 重新读取。
    CONSOLE_PORTABLE_ATTACH_MSG portable{};
    portable.IdLowPart = msg.descriptor.Identifier.LowPart;
    portable.IdHighPart = msg.descriptor.Identifier.HighPart;
    portable.Process = msg.descriptor.Process;
    portable.Object = msg.descriptor.Object;
    portable.Function = msg.descriptor.Function;
    portable.InputSize = msg.descriptor.InputSize;
    portable.OutputSize = msg.descriptor.OutputSize;
    return portable;
}

// 执行 handoff 协议。handoff 对象已创建（且通过 marker 检查），返回 true 表示
// 终端接受了这次移交。
[[nodiscard]] inline bool perform_handoff(IConsoleHandoff &handoff, win32::handle_view server,
                                          win32::handle_view input_event,
                                          const CONSOLE_PORTABLE_ATTACH_MSG &portable_msg, DWORD client_pid)
{
    // signal_write 传给终端；signal_read 留给 corehost 转发 Ctrl+C/Break/Close。
    auto [signal_read, signal_write] = win32::create_pipe();

    // corehost_process 是当前进程的真实句柄副本，终端用它监控 server 生命周期。
    auto corehost_process = win32::duplicate_self();

    // terminal_process 由 COM 调用返回；有效时等待终端进程退出。
    win32::event terminal_process;

    LOG("calling EstablishHandoff server=%p event=%p signalWrite=%p self=%p id=%08lx:%08lx pid=%lu", server.get(),
        input_event.get(), signal_write.get(), corehost_process.get(), portable_msg.IdHighPart,
        portable_msg.IdLowPart, client_pid);
    const HRESULT hr = handoff.EstablishHandoff(server.get(), input_event.get(), &portable_msg, signal_write.get(),
                                               corehost_process.get(), terminal_process.put());
    LOG("EstablishHandoff returned hr=0x%08lx terminalProcess=%p", static_cast<unsigned long>(hr),
        terminal_process.get());
    if (win32::failed(static_cast<win32::hresult>(hr)))
        return false;

    signal_write.clear();
    corehost_process.clear();

    // Ctrl+C/Break/Close 由信号线程阻塞读取并转发到 CSRSS；管道断开时置位
    // signals_closed。主线程只在 {终端进程, signals_closed} 上无限等待。
    win32::event signals_closed{win32::create_tag, /*manual_reset=*/true, /*initial=*/false};
    console_control_thread signal_thread{signal_read.view(), signals_closed.view()};
    LOG("signal pipe bound to handoff signal thread pipe=%p", signal_read.get());
    signal_thread.start();

    const auto wait = win32::wait_any(terminal_process, signals_closed, INFINITE);
    if (wait.abandoned())
        LOG("terminal handoff wait abandoned");
    else if (wait.index == 0)
        LOG("terminal handoff wait completed; terminal process exited");
    else
        LOG("signal pipe closed; terminal no longer serviced");

    signal_thread.stop_and_join();
    return true;
}

} // namespace corehost::defterm
