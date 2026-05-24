// ── defterm/connect.hpp ─────────────────────────────────────
// CONNECT 消息分派逻辑
//
// 包含:
//   - try_handoff_all          — 依次尝试全部候选终端
//   - handle_connect           — CONNECT 分派决策核心
//   - connect_handler          — 适配 miniio::run_io_loop 模板

#pragma once
#include <windows.h>
#include <winternl.h>
#include <shellapi.h>
#include <algorithm>
#include <array>
#include <ranges>
#include "handoff.hpp"
#include "win32/handle.hpp"
#include "win32/event.hpp"
#include "os/Console/condrv.h"
#include "ntapi/condrv.hpp"
#include "miniio/io_loop.hpp"
#include "utility/env.hpp"
#include "utility/log.hpp"

namespace defterm
{

// 依次尝试全部候选终端
//
// 候选顺序：注册表配置优先 -> WT 稳定版 -> Preview -> Canary -> Dev。
// CLSID_ZERO (未配置) 被跳过。
// WT 四个通道需要通过 IDefaultTerminalMarker 验证（marker_check）。
// 如果有一个成功，返回 true。
[[nodiscard]] inline bool try_handoff_all(win32::handle_view server, win32::handle_view ev,
                                          const CONSOLE_PORTABLE_ATTACH_MSG &portable, DWORD client_pid)
{
    auto candidates = std::array{
        clsid::default_clsid(clsid::delegation_step::console),
        clsid::wt_console,
        clsid::wt_console_pre,
        clsid::wt_console_can,
        clsid::wt_console_dev,
    };
    auto marker = std::array{false, true, true, true, true};

    auto valid = std::views::zip(candidates, marker) |
                 std::views::filter([](const auto &p) { return !need_skip(std::get<0>(p)); });
    auto it = std::ranges::find_if(valid, [&](const auto &p) {
        return attempt_handoff(std::get<0>(p), std::get<1>(p), server, ev, portable, client_pid);
    });
    return it != std::ranges::end(valid);
}

// CONNECT 消息分派 — 决策核心
//
//   1. need_gui 检查：已初始化? 非交互会话?
//      -> mini console 路径（继续循环，为后续消息服务）
//   2. UAC 提升? -> handle_elevated_connect 回退
//   3. 依次尝试 5 个候选 CLSID -> 成功则直接 return（WT 接管）
//   4. 全部失败 -> handle_no_terminal 回退
//
// 返回 true  -> 调用方退出事件循环 (移交成功，conhost 进程退出)
// 返回 false -> 调用方继续循环 (mini console 需处理后续消息)
[[nodiscard]] inline bool handle_connect(win32::handle_view server, win32::handle_view ev, miniio::io_msg &msg,
                                         bool &initialized, miniio::io_handles &handles)
{
    DWORD client_pid = static_cast<DWORD>(msg.descriptor.Process);
    LOG("handle_connect: pid=%lu func=%lu input=%lu output=%lu initialized=%d", client_pid, msg.descriptor.Function,
        msg.descriptor.InputSize, msg.descriptor.OutputSize, initialized);

    // ── 分派决策 ──
    bool need_gui = !initialized && should_attempt_handoff(*reinterpret_cast<const CONSOLE_SERVER_MSG *>(msg.body)) &&
                    is_interactive_user_session();
    LOG("handle_connect: need_gui=%d", need_gui);
    initialized = true;

    if (!need_gui)
    {
        LOG("handle_connect: no GUI → %s, continuing loop",
            handles.input.valid() ? "prepare_completion" : "accept_connection");
        if (!handles.input.valid())
            handles = miniio::accept_connection(server, msg);
        else
            miniio::prepare_completion(msg);
        return false; // 继续事件循环
    }

    // UAC 提升回退
    //
    // 当 corehost 以 High IL (管理员) 运行时，UIPI (用户界面特权隔离)
    // 阻止向 Medium IL 的 Windows Terminal 传递内核句柄。
    // COM 激活也由于跨会话边界而失败。
    //
    // 此函数是降级路径：接管连接 -> MessageBox 告知用户 -> 发送
    // Ctrl+Break 终止客户端（避免客户端在无窗口控制台中无限等待输入）。
    if (env::is_elevated())
    {
        env::show_elevated_message();
        handles = miniio::accept_connection(server, msg);
        ::GenerateConsoleCtrlEvent(CTRL_BREAK_EVENT, client_pid);
        return false;
    }

    // ── COM 移交 ──
    LOG("handle_connect: trying COM handoff");
    if (try_handoff_all(server, ev, miniio::make_portable_attach_msg(msg), client_pid))
    {
        LOG("handle_connect: handoff SUCCESS → exiting loop");
        return true; // 移交成功 -> 退出事件循环
    }
    LOG("handle_connect: all handoff attempts FAILED");

    // 无可用终端回退
    //
    // 如果所有 5 个候选 CLSID 均不可用（终端未安装或 COM 激活失败）。
    // 提示用户安装 Windows Terminal，并跳转 Microsoft Store。
    // 之后发送 Ctrl+Break 终止客户端。
    env::show_not_found_message();
    handles = miniio::accept_connection(server, msg);
    ::GenerateConsoleCtrlEvent(CTRL_BREAK_EVENT, client_pid);
    return false;
}

// CONNECT 处理器（适配 miniio::run_io_loop 模板）
struct connect_handler
{
    bool initialized = false;
    miniio::io_handles handles;
    win32::handle_view server;
    win32::handle_view ev;

    bool on_connect(miniio::io_msg &msg)
    {
        return !handle_connect(server, ev, msg, initialized, handles);
    }

    bool on_message(miniio::io_msg &msg)
    {
        miniio::dispatch_non_connect(server, msg, handles);
        return true;
    }

    void on_idle()
    {
    } // no-op: defterm connect 无需检查 PTY

    bool has_pending() const
    {
        return false;
    }
    bool should_exit() const
    {
        return false;
    }
};

} // namespace defterm
