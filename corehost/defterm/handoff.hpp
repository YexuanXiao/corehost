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
//   7. WaitForSingleObject(clientProcess, INFINITE)
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
#include <memory>
#include "com/com_ptr.hpp"
#include "signal.hpp"
#include "win32/handle.hpp"
#include "win32/thread.hpp"
#include "win32/error.hpp"
#include "win32/com_apartment.hpp"
#include "win32/hresult.hpp"
#include "IConsoleHandoff.h"
#include "com/clsid.hpp"
#include "os/Console/conmsgl1.h"
#include "utility/log.hpp"

namespace defterm
{

// 如果注册表提供的 CLSID 无效，则后续会回退到尝试 WT
[[nodiscard]] inline bool need_skip(const CLSID &c) noexcept
{
    // 系统 conhost 被认为是有效的，因为当安装 corehost 到系统时，
    // 它就是系统 conhost。由于 corehost 实现了 IConsoleHandoff，
    // 在此场景下可以工作，因此不需要排除。
    return c == clsid::zero;
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
        return false;
    if (session_id == 0)
        return false;

    auto winsta = ::GetProcessWindowStation();
    if (!winsta)
        return false;

    USEROBJECTFLAGS flags{};
    if (!::GetUserObjectInformationW(winsta, UOI_FLAGS, &flags, sizeof(flags), nullptr))
    {
        return false;
    }
    if (!(flags.dwFlags & WSF_VISIBLE))
        return false;

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
    // AllocConsole/AttachConsole 可能需要获得控制台，因此不检查它是否是控制台应用
    // if (!msg.ConsoleApp)
    //    return false;

    // WindowVisible == FALSE -> 进程以 CREATE_NO_WINDOW 创建，不应弹出窗口。
    if (!msg.WindowVisible)
        return false;

    // STARTF_USESHOWWINDOW 且 ShowWindow 为隐藏 -> 进程明确不想显示窗口。
    if (msg.StartupFlags & STARTF_USESHOWWINDOW)
    {
        if (msg.ShowWindow== SW_HIDE)
            return false;
    }

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
        (void)hnd.as<IDefaultTerminalMarker>();
    }
    // ── 2. 信号管道 + 进程句柄 + EstablishHandoff ──
    // CreatePipe：WT 通过写端发回控制信号，conhost 线程从读端接收
    auto [sr, sw] = win32::create_pipe();

    // 复制自身进程句柄
    auto our_proc = win32::duplicate_self();

    // EstablishHandoff 移交：server + inputEvent + portableMsg + signalPipe + ourProc -> WT 返回 clientProc 供
    // WaitForSingleObject
    LOG("attempt_handoff: calling EstablishHandoff");
    win32::event client;
    auto hr = hnd->EstablishHandoff(server_handle.get(), input_event.get(), &portable_msg, sw.get(), our_proc.get(),
                                    client.put());
    win32::throw_hresult(win32::hresult(hr));

    // WT 在 EstablishHandoff 中已通过 DuplicateHandle 获取自己的副本，conhost 侧可安全关闭这两句柄
    sw.clear();
    our_proc.clear();

    // 启动信号监听线程 (第一跳: inbox→corehost, 无需 vt_in)
    auto tp = std::make_unique<defterm::signal_thread_params>(
        defterm::signal_thread_params{std::move(sr)});
    auto sig_thread = win32::basic_thread{defterm::signal_thread_proc, tp.release()};

    // 阻塞等待 WT 退出
    client.wait();

    return true;
}

} // namespace defterm