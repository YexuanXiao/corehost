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
#include <algorithm>
#include <array>
#include <cstring>
#include <ranges>
#include <string>
#include <string_view>
#include "handoff.hpp"
#include "win32/handle.hpp"
#include "win32/event.hpp"
#include "os/Console/condrv.h"
#include "ntapi/condrv.hpp"
#include "miniio/io_thread.hpp"
#include "io_loop.hpp"
#include "utility/env.hpp"
#include "utility/log.hpp"

namespace defterm
{
using namespace std::literals;

struct connect_process_info
{
    DWORD pid = 0;
    DWORD process_group_id = 0;
    std::wstring image_path;
};

struct fallback_state
{
    bool break_when_input_waits = false;
    bool break_sent = false;
    DWORD target_process_group_id = 0;
};

[[nodiscard]] inline std::wstring query_process_image_path(DWORD pid)
{
    if (pid == 0)
        return {};

    win32::handle process{::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid)};
    if (!process.valid())
    {
        LOG("query_process_image_path: OpenProcess pid=%lu failed err=%lu", pid, ::GetLastError());
        return {};
    }

    std::wstring path(32768, L'\0');
    DWORD path_length = static_cast<DWORD>(path.size());
    if (!::QueryFullProcessImageNameW(process.get(), 0, path.data(), &path_length))
    {
        LOG("query_process_image_path: QueryFullProcessImageNameW pid=%lu failed err=%lu", pid, ::GetLastError());
        return {};
    }

    path.resize(path_length);
    LOG("query_process_image_path: pid=%lu length=%zu", pid, path.size());
    return path;
}

[[nodiscard]] inline connect_process_info make_connect_process_info(const CONSOLE_SERVER_MSG &connect_info,
                                                                    DWORD client_pid)
{
    connect_process_info info;
    info.pid = client_pid;
    info.process_group_id = connect_info.ProcessGroupId;
    info.image_path = query_process_image_path(info.process_group_id);
    if (info.image_path.empty() && info.process_group_id != info.pid)
        info.image_path = query_process_image_path(info.pid);
    LOG("make_connect_process_info: pid=%lu pgid=%lu imagePathLength=%zu", info.pid, info.process_group_id,
        info.image_path.size());
    return info;
}

[[nodiscard]] inline bool is_waiting_for_user_input(const miniio::io_msg &msg) noexcept
{
    if (msg.descriptor.Function == CONSOLE_IO_RAW_READ)
    {
        LOG("is_waiting_for_user_input: RAW_READ");
        return true;
    }
    if (msg.descriptor.Function != CONSOLE_IO_USER_DEFINED ||
        msg.descriptor.InputSize < sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_READCONSOLE_MSG))
        return false;

    auto *header = reinterpret_cast<const CONSOLE_MSG_HEADER *>(msg.body);
    LOG("is_waiting_for_user_input: USER_DEFINED api=0x%08lx", header->ApiNumber);
    return header->ApiNumber == ConsolepReadConsole;
}

inline void send_deferred_ctrl_break_if_needed(const miniio::io_msg &msg, fallback_state &fallback)
{
    if (!fallback.break_when_input_waits || fallback.break_sent || !is_waiting_for_user_input(msg))
        return;

    LOG("fallback: sending deferred CTRL_BREAK to pgid=%lu", fallback.target_process_group_id);
    ::GenerateConsoleCtrlEvent(CTRL_BREAK_EVENT, fallback.target_process_group_id);
    fallback.break_sent = true;
}

// 从 io_msg 提取便携连接消息（供 COM 接口使用）。
inline CONSOLE_PORTABLE_ATTACH_MSG make_portable_attach_msg(const miniio::io_msg &msg)
{
    CONSOLE_PORTABLE_ATTACH_MSG p{};
    p.IdLowPart = msg.descriptor.Identifier.LowPart;
    p.IdHighPart = msg.descriptor.Identifier.HighPart;
    p.Process = msg.descriptor.Process;
    p.Object = msg.descriptor.Object;
    p.Function = msg.descriptor.Function;
    p.InputSize = msg.descriptor.InputSize;
    p.OutputSize = msg.descriptor.OutputSize;
    return p;
}

inline void handle_non_gui_connect(win32::handle_view server, miniio::io_msg &msg, win32::handle &condrv_input,
                                   win32::handle &condrv_output, connect_completion &completion)
{
    LOG("handle_connect: no GUI -> %s, continuing loop",
        condrv_input.valid() ? "prepare_completion" : "accept_connection");
    if (!condrv_input.valid())
    {
        LOG("handle_connect: accepting mini-console connection");
        miniio::accept_connection(server, msg, condrv_input, condrv_output);
        LOG("handle_connect: accepted mini-console input=%p output=%p", condrv_input.get(), condrv_output.get());
        return;
    }

    LOG("handle_connect: completing secondary connect inline");
    miniio::prepare_completion(msg);
    completion = connect_completion::inline_complete;
}

[[nodiscard]] inline bool should_open_terminal_window(const CONSOLE_SERVER_MSG &connect_info, bool initialized)
{
    if (initialized)
    {
        LOG("should_open_terminal_window: reject already initialized");
        return false;
    }
    if (!should_attempt_handoff(connect_info))
    {
        LOG("should_open_terminal_window: reject startup info");
        return false;
    }
    if (!is_interactive_user_session())
    {
        LOG("should_open_terminal_window: reject non-interactive session");
        return false;
    }
    return true;
}

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
// 返回 true  -> 移交成功
// 返回 false -> 调用方继续循环 (mini console 需处理后续消息)
// completion 说明 CONNECT completion 是否已经通过 accept_connection 显式提交。
[[nodiscard]] inline bool handle_connect(win32::handle_view server, win32::handle_view ev, miniio::io_msg &msg,
                                         bool &initialized, win32::handle &condrv_input, win32::handle &condrv_output,
                                         fallback_state &fallback, connect_completion &completion)
{
    DWORD client_pid = static_cast<DWORD>(msg.descriptor.Process);
    auto &connect_info = *reinterpret_cast<const CONSOLE_SERVER_MSG *>(msg.body);
    LOG("handle_connect: pid=%lu pgid=%lu func=%lu input=%lu output=%lu initialized=%d consoleApp=%u visible=%u "
        "show=%u flags=0x%08lx",
        client_pid, connect_info.ProcessGroupId, msg.descriptor.Function, msg.descriptor.InputSize,
        msg.descriptor.OutputSize, initialized, static_cast<unsigned>(connect_info.ConsoleApp),
        static_cast<unsigned>(connect_info.WindowVisible), connect_info.ShowWindow, connect_info.StartupFlags);

    bool need_gui = should_open_terminal_window(connect_info, initialized);
    LOG("handle_connect: needGui=%d", need_gui);
    initialized = true;

    if (!need_gui)
    {
        handle_non_gui_connect(server, msg, condrv_input, condrv_output, completion);
        return false; // 继续事件循环
    }

    // UAC 提升回退
    //
    // 当 corehost 以 High IL (管理员) 运行时，UIPI (用户界面特权隔离)
    // 阻止向 Medium IL 的 Windows Terminal 传递内核句柄。
    // COM 激活也由于跨会话边界而失败。
    //
    // 此函数是降级路径：接管连接 -> 通知用户 -> 等客户端真正开始等待输入
    // 时发送 Ctrl+Break。这样 GUI/CLI 初始化代码仍能先运行到稳定点。
    if (env::is_elevated())
    {
        LOG("handle_connect: elevated fallback");
        auto process = make_connect_process_info(connect_info, client_pid);
        env::show_elevated_notification(process.pid, process.process_group_id, process.image_path);
        miniio::accept_connection(server, msg, condrv_input, condrv_output);
        fallback.break_when_input_waits = true;
        fallback.target_process_group_id = process.process_group_id ? process.process_group_id : process.pid;
        LOG("handle_connect: elevated fallback accepted input=%p output=%p breakTarget=%lu", condrv_input.get(),
            condrv_output.get(), fallback.target_process_group_id);
        return false;
    }

    // ── COM 移交 ──
    LOG("handle_connect: trying COM handoff");
    if (try_handoff_all(server, ev, make_portable_attach_msg(msg), client_pid))
    {
        LOG("handle_connect: handoff SUCCESS → exiting loop");
        return true; // 移交成功 -> 退出事件循环
    }
    LOG("handle_connect: all handoff attempts FAILED");

    // 无可用终端回退
    //
    // 如果所有 5 个候选 CLSID 均不可用（终端未安装或 COM 激活失败）。
    // 通知用户安装 Windows Terminal。
    // 之后发送 Ctrl+Break 终止客户端。
    env::show_not_found_notification();
    miniio::accept_connection(server, msg, condrv_input, condrv_output);
    LOG("handle_connect: no terminal fallback accepted input=%p output=%p sending break pid=%lu", condrv_input.get(),
        condrv_output.get(), client_pid);
    ::GenerateConsoleCtrlEvent(CTRL_BREAK_EVENT, client_pid);
    return false;
}

// CONNECT 处理器（适配 defterm::run_io_loop 模板）
struct connect_handler
{
    bool initialized = false;
    win32::handle condrv_input;
    win32::handle condrv_output;
    win32::handle_view server;
    win32::handle_view ev;
    fallback_state fallback;

    bool on_connect(miniio::io_msg &msg, connect_completion &completion)
    {
        LOG("connect_handler::on_connect");
        completion = connect_completion::explicit_complete;
        return !handle_connect(server, ev, msg, initialized, condrv_input, condrv_output, fallback, completion);
    }

    bool on_message(miniio::io_msg &msg)
    {
        LOG("connect_handler::on_message func=%lu fallbackWait=%d breakSent=%d", msg.descriptor.Function,
            fallback.break_when_input_waits, fallback.break_sent);
        send_deferred_ctrl_break_if_needed(msg, fallback);
        dispatch_non_connect(server, msg, condrv_input, condrv_output);
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
