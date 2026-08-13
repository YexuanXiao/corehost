#pragma once

#include <windows.h>
#include <array>
#include <objbase.h>
#include <ranges>
#include "signal.hpp"
#include "IConsoleHandoff.h"
#include "com/clsid.hpp"
#include "com/com_ptr.hpp"
#include "condrv_io.hpp"
#include "utility/log.hpp"
#include "win32/com_apartment.hpp"
#include "win32/error.hpp"
#include "win32/event.hpp"
#include "win32/handle.hpp"
#include "win32/hresult.hpp"
#include "win32/wait.hpp"

namespace corehost::defterm
{

[[nodiscard]] inline CONSOLE_PORTABLE_ATTACH_MSG make_portable_attach_msg(
    const corehost::condrv_io::io_msg &msg) noexcept
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

[[nodiscard]] inline bool should_skip_terminal(const CLSID &clsid) noexcept
{
    // zero 表示注册表没有配置；conhost 表示默认终端指回传统 conhost。
    // 两者都不是可 handoff 的终端实现。
    return clsid == clsid::zero || clsid == clsid::conhost;
}

[[nodiscard]] inline bool try_terminal_handoff(const CLSID &terminal_clsid, bool marker_check_required,
                                               win32::handle_view server, win32::handle_view input_event,
                                               const CONSOLE_PORTABLE_ATTACH_MSG &portable_msg, DWORD client_pid)
{
    LOG("trying terminal candidate clsid=%08X-%04X-%04X markerRequired=%d pid=%lu", terminal_clsid.Data1,
        terminal_clsid.Data2, terminal_clsid.Data3, marker_check_required, client_pid);

    // 默认终端协议使用本地 COM server，信号转发和等待逻辑不依赖 STA，
    // 因此使用 MTA。
    auto apartment = win32::com_apartment{COINIT_MULTITHREADED};

    // handoff 为空表示 CoCreateInstance 失败；返回 false 后调用方会尝试
    // 下一个 CLSID。
    HRESULT hr;
    auto handoff = com::try_create_instance<IConsoleHandoff>(hr, terminal_clsid, CLSCTX_LOCAL_SERVER);
    LOG("terminal candidate COM object created ptr=%p, HRESULT=%08lx", handoff.get(), static_cast<unsigned long>(hr));

    if (!handoff)
    {
        return false;
    }

    if (marker_check_required && handoff.try_as<IDefaultTerminalMarker>())
    {
        LOG("checking default-terminal marker failed.");
        return false;
    }

    // signal_write 传给终端；signal_read 留给 corehost 轮询转发
    // Ctrl+C/Break/Close 等事件。
    auto [signal_read, signal_write] = win32::create_pipe();

    // corehost_process 是当前进程的真实句柄副本，终端用它监控 server 生命周期。
    auto corehost_process = win32::duplicate_self();

    // terminal_process 由 COM 调用返回。有效时可等待终端进程退出；为空会被
    // throw_hresult 前的 HRESULT 失败路径拦截。
    win32::event terminal_process;

    LOG("calling EstablishHandoff server=%p event=%p signalWrite=%p self=%p id=%08lx:%08lx", server.get(),
        input_event.get(), signal_write.get(), corehost_process.get(), portable_msg.IdHighPart, portable_msg.IdLowPart);
    hr = handoff->EstablishHandoff(server.get(), input_event.get(), &portable_msg, signal_write.get(),
                                   corehost_process.get(), terminal_process.put());
    LOG("EstablishHandoff returned hr=0x%08lx terminalProcess=%p", static_cast<unsigned long>(hr),
        terminal_process.get());
    if (win32::failed(static_cast<win32::hresult>(hr)))
    {
        return false;
    }

    signal_write.clear();
    corehost_process.clear();

    // 信号管道由 console_control_forwarder 在主线程轮询：Ctrl+C/Break/Close
    // 消息被转发到 CSRSS，管道断开由 poll() 报告。不再创建独立信号线程，
    // signal_read 在函数栈上保持到等待结束。
    console_control_forwarder signals;
    signals.set_pipe(signal_read.view());
    LOG("signal pipe bound to handoff polling loop pipe=%p", signal_read.get());

    // 等待终端进程退出或信号管道断开（等价于终端侧停止服务）。每个 16ms
    // 时间片先轮询一次信号管道，保证 Ctrl+C 等消息不被饿死。
    for (;;)
    {
        if (signals.poll())
        {
            LOG("signal pipe closed; terminal no longer serviced");
            return true;
        }
        const auto wait = win32::wait_one(terminal_process, 16);
        if (wait.abandoned())
        {
            LOG("terminal handoff wait abandoned");
            return true;
        }
        if (wait.signaled())
        {
            LOG("terminal handoff wait completed; terminal process exited");
            return true;
        }
    }
}

[[nodiscard]] inline bool try_terminal_handoff(win32::handle_view server, win32::handle_view input_event,
                                               const CONSOLE_PORTABLE_ATTACH_MSG &portable_msg, DWORD client_pid)
{
    const auto candidates = std::array{
        clsid::default_clsid(clsid::delegation_step::console),
        clsid::wt_console,
        clsid::wt_console_pre,
        clsid::wt_console_can,
        clsid::wt_console_dev,
    };

    // marker_required 与 candidates 一一对应：注册表配置的 CLSID 是用户显式
    // 选择，不要求 IDefaultTerminalMarker；内置 WT 通道必须声明 marker。
    constexpr auto marker_required = std::array{false, true, true, true, true};

    for (const auto [candidate, marker_check_required] : std::views::zip(candidates, marker_required))
    {
        const bool skip_candidate = should_skip_terminal(candidate);
        LOG_IF(skip_candidate, "skipping non-handoff terminal candidate");
        if (skip_candidate)
            continue;
        if (try_terminal_handoff(candidate, marker_check_required, server, input_event, portable_msg, client_pid))
            return true;
    }
    return false;
}

} // namespace corehost::defterm
