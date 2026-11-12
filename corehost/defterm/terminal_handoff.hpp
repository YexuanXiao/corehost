#pragma once

#include <windows.h>

#include <array>
#include <objbase.h>
#include <ranges>

#include "handoff_session.hpp"
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

[[nodiscard]] inline bool should_skip_terminal(const CLSID &clsid) noexcept
{
    // zero 表示注册表没有配置；conhost 表示默认终端指回传统 conhost。
    // 两者都不是可 handoff 的终端实现。
    return clsid == clsid::zero || clsid == clsid::conhost;
}

// 创建候选 CLSID 对应的 COM 对象，通过 marker 检查后执行 handoff 协议。
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
        return false;

    if (marker_check_required && handoff.try_as<IDefaultTerminalMarker>())
    {
        LOG("checking default-terminal marker failed.");
        return false;
    }

    return perform_handoff(*handoff.get(), server, input_event, portable_msg, client_pid);
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