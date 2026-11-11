// ── defterm/client_notify.hpp ─────────────────────────────
// 向发起 CONNECT 的控制台客户端投递控制事件（Ctrl+Break / Ctrl+C）。
//
// corehost 不是该控制台的附属进程，GenerateConsoleCtrlEvent 到不了客户端
// （见 gaps G11/G16）；必须经 CSRSS 的 ConsoleControl(ConsoleEndTask)，
// 控制事件的投递语义与系统控制台宿主一致。
//
// 投递函数做成参数是为了可注入测试：defterm 的 elevated/fallback 分支在
// 测试环境里无法真正进入，但"向哪个 pid 投递什么事件"可以单测。
#pragma once
#include <windows.h>

#include "console_ctrl.hpp"
#include "utility/log.hpp"

namespace corehost::defterm
{

// 与 console_end_task 签名一致：pid、事件码、控制台窗口（无则 nullptr）。
using ctrl_event_sender = corehost::conpty::ctrl_event_sender;

// 向单个客户端进程投递控制事件。事件码必须是 CTRL_C_EVENT / CTRL_BREAK_EVENT。
inline void notify_client_ctrl(DWORD pid, DWORD event,
                              ctrl_event_sender sender = &corehost::conpty::console_end_task) noexcept
{
    const NTSTATUS status = sender(pid, event, nullptr);
    LOG("defterm: ctrl event %lu to pid=%lu status=0x%08lx", event, pid,
        static_cast<unsigned long>(status));
}

} // namespace corehost::defterm
