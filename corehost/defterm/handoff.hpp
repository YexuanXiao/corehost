// ── defterm/handoff.hpp ──────────────────────────────────
// COM 移交 — IConsoleHandoff::EstablishHandoff
//
// 这是默认终端协议的核心：将 ConDrv 会话控制权转交给第三方终端。
//
// 移交流程：
//   1. CoInitializeEx(COINIT_MULTITHREADED)
//      — 多线程公寓，因为信号线程需要并行访问 COM。
//   2. CoCreateInstance(CLSCTX_LOCAL_SERVER)
//      — 激活终端进程（如 Windows Terminal）。LOCAL_SERVER
//      而非 INPROC，因为终端是独立 .exe。
//   3. IDefaultTerminalMarker 验证（仅 WT 通道需要）
//      — 确保终端确实声明自己愿意充当默认终端。
//      注册表配置的终端跳过此检查（用户主动选择）。
//   4. CreatePipe -> 信号管道
//      — WT 通过此管道发回 Ctrl+C/Break/Close 等控制信号。
//      conhost 将写端移交给 WT，读端留给信号线程。
//   5. DuplicateHandle -> 进程句柄
//      — WT 需要监控 conhost 是否存活。SYNCHRONIZE 权限即可。
//   6. EstablishHandoff(server, inputEvent, msg, signalPipe, ourProcess, &clientProcess)
//      — 服务器句柄 + 输入事件 + 便携连接消息 + 信号管道 + 自身进程
//        -> WT 接管整个控制台会话，返回客户端进程句柄。
//   7. Wait for clientProcess until the terminal exits
//      — 阻塞直到 WT 退出。在此期间信号线程转发控制事件。
//
// 句柄所有权：
//   sw (信号管道写端) -> 转移给 WT，conhost 侧 clear() 放弃所有权
//   our_proc (自身进程句柄) -> 转移给 WT
//   client (WT 返回的进程句柄) -> conhost 拥有，Wait 后 clear() 关闭
//   sr (信号管道读端) -> 转移到 defterm::signal_thread_params，线程结束时关闭

#pragma once
#include <windows.h>
#include <objbase.h>
#include <array>
#include <memory>
#include "com/com_ptr.hpp"
#include "signal.hpp"
#include "win32/handle.hpp"
#include "win32/thread.hpp"
#include "win32/error.hpp"
#include "win32/com_apartment.hpp"
#include "win32/hresult.hpp"
#include "win32/wait.hpp"
#include "IConsoleHandoff.h"
#include "com/clsid.hpp"
#include "os/Console/conmsgl1.h"
#include "utility/log.hpp"

namespace defterm
{

// 如果注册表提供的 CLSID 无效，则后续会回退到尝试 WT
[[nodiscard]] inline bool need_skip(const CLSID &c) noexcept
{
    // 零 CLSID 和标准 conhost CLSID 都不是可移交目标。
    // 旧实现会跳过 conhost CLSID 后继续尝试 WT 的固定通道；
    // 若把 conhost 当作 IConsoleHandoff 目标，默认终端链路可能绕回
    // inbox/corehost 自身，导致 WT 关闭后等待对象不按预期结束。
    return c == clsid::zero || c == clsid::conhost;
}

// 判断当前进程是否运行在交互式用户会话中。
//   - 会话 0 (服务/驱动) -> 非交互
//   - 窗口不可见 -> 非交互
// 非交互会话不应尝试 COM 移交（无可用终端、可能破坏服务）。
// 注意，不应该拒绝窗口应用，因为窗口应用可以通过 AllocConsole 和
// AttachConsole 获得一个可用终端
[[nodiscard]] inline bool is_interactive_user_session() noexcept
{
    DWORD session_id = 0;
    if (!::ProcessIdToSessionId(::GetCurrentProcessId(), &session_id))
    {
        LOG("is_interactive_user_session: ProcessIdToSessionId failed err=%lu", ::GetLastError());
        return false;
    }
    if (session_id == 0)
    {
        LOG("is_interactive_user_session: session 0");
        return false;
    }

    auto winsta = ::GetProcessWindowStation();
    if (!winsta)
    {
        LOG("is_interactive_user_session: GetProcessWindowStation failed err=%lu", ::GetLastError());
        return false;
    }

    USEROBJECTFLAGS flags{};
    if (!::GetUserObjectInformationW(winsta, UOI_FLAGS, &flags, sizeof(flags), nullptr))
    {
        LOG("is_interactive_user_session: GetUserObjectInformationW failed err=%lu", ::GetLastError());
        return false;
    }
    if (!(flags.dwFlags & WSF_VISIBLE))
    {
        LOG("is_interactive_user_session: invisible window station flags=0x%08lx", flags.dwFlags);
        return false;
    }

    LOG("is_interactive_user_session: yes session=%lu flags=0x%08lx", session_id, flags.dwFlags);
    return true;
}

// 判断 CONNECT 消息是否值得尝试移交终端窗口。
// 过滤条件 (任一满足 -> false):
//   1. WindowVisible == FALSE (CREATE_NO_WINDOW / AllocConsole 的隐藏请求)
//   2. (StartupFlags & STARTF_USESHOWWINDOW) && ShowWindow ∈ {
//       SW_HIDE(0), SW_SHOWMINIMIZED(2), SW_MINIMIZE(6),
//       SW_SHOWMINNOACTIVE(7), SW_FORCEMINIMIZE(11) }
//
[[nodiscard]] inline bool should_attempt_handoff(const CONSOLE_SERVER_MSG &msg) noexcept
{
    LOG("should_attempt_handoff: consoleApp=%u visible=%u startupFlags=0x%08lx showWindow=%u titleLength=%u "
        "pgid=%lu",
        static_cast<unsigned>(msg.ConsoleApp), static_cast<unsigned>(msg.WindowVisible), msg.StartupFlags,
        msg.ShowWindow, msg.TitleLength, msg.ProcessGroupId);

    // AllocConsole/AttachConsole 可能需要获得控制台，因此不检查它是否是控制台应用
    // if (!msg.ConsoleApp)
    //    return false;

    // WindowVisible == FALSE -> 进程以 CREATE_NO_WINDOW 创建，不应弹出窗口。
    if (!msg.WindowVisible)
    {
        LOG("should_attempt_handoff: reject WindowVisible=false");
        return false;
    }

    // STARTF_USESHOWWINDOW 且 ShowWindow 为隐藏 -> 进程明确不想显示窗口。
    if (msg.StartupFlags & STARTF_USESHOWWINDOW)
    {
        switch (msg.ShowWindow)
        {
        case SW_HIDE:
        case SW_SHOWMINIMIZED:
        case SW_MINIMIZE:
        case SW_SHOWMINNOACTIVE:
        case SW_FORCEMINIMIZE:
            LOG("should_attempt_handoff: reject showWindow=%u", msg.ShowWindow);
            return false;
        default:
            break;
        }
    }

    LOG("should_attempt_handoff: accept");
    return true;
}

// 尝试 COM 移交。
// CLSID 不存在/COM 服务器未安装 -> 返回 false；其他错误 -> 抛异常
[[nodiscard]] inline bool attempt_handoff(const CLSID &console_clsid, bool marker_check_required,
                                          win32::handle_view server_handle, win32::handle_view input_event,
                                          const CONSOLE_PORTABLE_ATTACH_MSG &portable_msg, DWORD client_pid)
{
    LOG("attempt_handoff: clsid=%08X-%04X-%04X... marker=%d pid=%lu", console_clsid.Data1, console_clsid.Data2,
        console_clsid.Data3, marker_check_required, client_pid);
    auto apt = win32::com_apartment{COINIT_MULTITHREADED};

    // ── 1. COM 对象创建 + marker 验证 ──
    com::com_ptr<IConsoleHandoff> hnd;
    try
    {
        hnd = com::create_instance<IConsoleHandoff>(console_clsid, CLSCTX_LOCAL_SERVER);
        LOG("attempt_handoff: create_instance ok, ptr=%p", hnd.get());
    }
    catch (...)
    {
        LOG("attempt_handoff: create_instance FAILED");
        return false; // CLSID 未安装或无法启动
    }

    // 只有 WindowsTerminal 需要实现此接口
    if (marker_check_required)
    {
        LOG("attempt_handoff: checking IDefaultTerminalMarker");
        (void)hnd.as<IDefaultTerminalMarker>();
        LOG("attempt_handoff: marker ok");
    }
    // ── 2. 信号管道 + 进程句柄 + EstablishHandoff ──
    // CreatePipe：WT 通过写端发回控制信号，conhost 线程从读端接收
    auto [sr, sw] = win32::create_pipe();
    LOG("attempt_handoff: signal pipe read=%p write=%p", sr.get(), sw.get());

    // 复制自身进程句柄
    auto our_proc = win32::duplicate_self();
    LOG("attempt_handoff: duplicated self process=%p", our_proc.get());

    // EstablishHandoff 移交：server + inputEvent + portableMsg + signalPipe + ourProc -> WT 返回 clientProc 供等待
    LOG("attempt_handoff: calling EstablishHandoff server=%p event=%p signalWrite=%p self=%p id=%08lx:%08lx",
        server_handle.get(), input_event.get(), sw.get(), our_proc.get(), portable_msg.IdHighPart,
        portable_msg.IdLowPart);
    win32::event client;
    auto hr = hnd->EstablishHandoff(server_handle.get(), input_event.get(), &portable_msg, sw.get(), our_proc.get(),
                                    client.put());
    LOG("attempt_handoff: EstablishHandoff hr=0x%08lx client=%p", static_cast<unsigned long>(hr), client.get());
    win32::throw_hresult(win32::hresult(hr));

    // WT 在 EstablishHandoff 中已通过 DuplicateHandle 获取自己的副本，conhost 侧可安全关闭这两句柄
    sw.clear();
    our_proc.clear();
    LOG("attempt_handoff: transferred signal/self handles");

    // 启动信号监听线程 (第一跳: inbox→corehost, 无需 vt_in)
    auto shutdown_event = win32::event{win32::create_tag, true, false};
    auto signal_shutdown_event = win32::event{win32::duplicate_handle(shutdown_event.view())};
    auto tp = std::make_unique<defterm::signal_thread_params>(
        defterm::signal_thread_params{std::move(sr), std::move(signal_shutdown_event)});
    DWORD signal_thread_id = 0;
    auto sig_thread = win32::basic_thread{defterm::signal_thread_proc, tp.release(), &signal_thread_id};
    LOG("attempt_handoff: signal thread started tid=%lu handle=%p shutdownEvent=%p", signal_thread_id, sig_thread.get(),
        shutdown_event.get());

    // WT 返回的 client 进程句柄用于保持默认终端协议中的 PID 连续性，
    // 但实际窗口关闭时更可靠的退出信号是 WT 关闭 signal pipe。
    // 原版 HostSignalInputThread 在管道断开时 RundownAndExit；
    // corehost 没有全局 rundown 体系，因此这里同时等待二者。
    LOG("attempt_handoff: waiting for handoff process=%p or signal shutdown=%p", client.get(), shutdown_event.get());
    const auto wait_result = win32::wait_any(client, shutdown_event, INFINITE);
    if (wait_result.abandoned())
    {
        LOG("attempt_handoff: wait abandoned index=%zu", wait_result.index);
        return true;
    }

    LOG("attempt_handoff: wait completed index=%zu source=%ls", wait_result.index,
        wait_result.index == 0 ? L"process" : L"signal");
    return true;
}

} // namespace defterm
