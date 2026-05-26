// ── comserver/com_server.cpp ──────────────────────────────
// -Embedding COM 服务器实现: 接收 inbox conhost 的控制台会话移交。
//
//   仅供 com_server_entry() 内部使用，不对外暴露。

#include "com_server.hpp"
#include <windows.h>
#include <objbase.h>
#include <memory>
#include "com/clsid.hpp"
#include "com/com_base.hpp"
#include "com/com_ptr.hpp"
#include "com/register.hpp"
#include "com/bstring.hpp"
#include "win32/handle.hpp"
#include "win32/error.hpp"
#include "win32/hresult.hpp"
#include "win32/event.hpp"
#include "win32/com_apartment.hpp"
#include "IConsoleHandoff.h"
#include "ITerminalHandoff.h"
#include "ntapi/condrv.hpp"
#include "miniio/io_thread.hpp"
#include "os/Console/conmsgl1.h"
#include "win32/thread.hpp"
#include "utility/log.hpp"
#include "mutex"

namespace comserver
{

// ============================================================================
// 本地 I/O 辅助函数（仅 comserver 使用）
// ============================================================================

// 从 COM 便携连接消息填充 io_msg 描述符（供 accept_connection 和 read_input 使用）。
inline miniio::io_msg make_connect_msg(PCCONSOLE_PORTABLE_ATTACH_MSG msg)
{
    miniio::io_msg m{};
    m.descriptor.Identifier.LowPart = msg->IdLowPart;
    m.descriptor.Identifier.HighPart = msg->IdHighPart;
    m.descriptor.Process = static_cast<decltype(m.descriptor.Process)>(msg->Process);
    m.descriptor.Object = static_cast<decltype(m.descriptor.Object)>(msg->Object);
    m.descriptor.Function = msg->Function;
    m.descriptor.InputSize = msg->InputSize;
    m.descriptor.OutputSize = msg->OutputSize;
    return m;
}

// 读取指定 IO 的输入载荷。CONNECT 的 CONSOLE_SERVER_MSG 不一定在
// read_io 的短包 body 中完整呈现，从驱动按 Identifier 重新取完整载荷。
inline void read_input(win32::handle_view server, const miniio::io_msg &msg, ULONG offset, void *buffer, ULONG size)
{
    CD_IO_OPERATION op{};
    op.Identifier = msg.descriptor.Identifier;
    op.Buffer.Offset = offset;
    op.Buffer.Data = buffer;
    op.Buffer.Size = size;

    DWORD r = 0;
    if (!::DeviceIoControl(server.get(), miniio::IOCTL_READ_INPUT, &op, sizeof(op), nullptr, 0, &r, nullptr))
        win32::throw_last_error();
}

// ============================================================================
// 终端移交子流程
// ============================================================================

// 从便携 attach 描述符恢复一条 miniio 消息，再按原版 OpenConsole 的做法
// 使用 IOCTL_CONDRV_READ_INPUT 读取完整的 CONSOLE_SERVER_MSG。COM 传来的
// PCCONSOLE_PORTABLE_ATTACH_MSG 只保存 CD_IO_DESCRIPTOR 的关键字段，不能
// 直接包含标题、ShowWindow 等 connect 输入载荷。
CONSOLE_SERVER_MSG read_connect_message(win32::handle_view condrv_server, PCCONSOLE_PORTABLE_ATTACH_MSG attach_msg)
{
    auto connect_io = make_connect_msg(attach_msg);
    CONSOLE_SERVER_MSG connect_info{};
    read_input(condrv_server, connect_io, 0, &connect_info, sizeof(connect_info));
    return connect_info;
}

// CONSOLE_SERVER_MSG::TitleLength 是字节数，不是 wchar_t 数量。这里仅做
// OpenConsole 同等的边界和 NUL 终止校验，失败时返回空标题并让调用方
// 使用 corehost 作为兜底标题。
win32::wcstring_view connect_title(CONSOLE_SERVER_MSG &connect_info)
{
    if (connect_info.TitleLength > sizeof(connect_info.Title) - sizeof(WCHAR) ||
        (connect_info.TitleLength % sizeof(WCHAR)) != 0)
        return {};

    const auto title_chars = connect_info.TitleLength / sizeof(WCHAR);
    if (connect_info.Title[title_chars] != L'\0')
        return {};

    LOG("connect_title: title=%ls length=%lu bytes", connect_info.Title, connect_info.TitleLength);
    return {connect_info.Title, title_chars};
}

// STARTUPINFO.wShowWindow 只有在 STARTF_USESHOWWINDOW 置位时有效。原版
// OpenConsole 通过 Settings::ApplyStartupInfo 得到最终值；当前不支持
// 快捷方式解析，因此这个轻量判断就是普通 CreateProcess 场景的等价路径。
WORD connect_show_window(const CONSOLE_SERVER_MSG &connect_info) noexcept
{
    return (connect_info.StartupFlags & STARTF_USESHOWWINDOW) ? connect_info.ShowWindow : SW_SHOWDEFAULT;
}

// 实例化 Windows Terminal 的 ITerminalHandoff3 并执行 PTY 移交协商。
//   terminal_read_pipe  : WT 可读端，corehost 写入后映射为 vt_out
//   terminal_write_pipe : WT 可写端，corehost 读取后映射为 vt_in
void create_terminal_pty(REFCLSID terminal_clsid, win32::handle_view signal_write,
                         win32::handle_view reference_handle, win32::handle_view corehost_process,
                         win32::handle_view client_process, win32::wcstring_view startup_title, WORD show_window,
                         win32::handle &terminal_read_pipe, win32::handle &terminal_write_pipe)
{
    LOG("create_terminal_pty: clsid=%08X-%04X-%04X...", terminal_clsid.Data1, terminal_clsid.Data2,
        terminal_clsid.Data3);

    auto terminal = com::create_instance<ITerminalHandoff3>(terminal_clsid, CLSCTX_LOCAL_SERVER);
    LOG("create_terminal_pty: terminal COM obj=%p", terminal.get());

    com::bstring title{!startup_title.empty() ? startup_title.c_str() : L"corehost"};
    TERMINAL_STARTUP_INFO startup{.pszTitle = title.get(),
                                  .dwXCountChars = 120,
                                  .dwYCountChars = 30,
                                  .dwFlags = STARTF_USECOUNTCHARS,
                                  .wShowWindow = show_window};

    LOG("create_terminal_pty: EstablishPtyHandoff(title=%ls showWindow=%u signal=%p ref=%p server=%p client=%p)",
        startup_title.c_str(), show_window, signal_write.get(), reference_handle.get(), corehost_process.get(),
        client_process.get());

    auto hr = terminal->EstablishPtyHandoff(terminal_read_pipe.put(), terminal_write_pipe.put(), signal_write.get(),
                                            reference_handle.get(), corehost_process.get(), client_process.get(),
                                            &startup);

    LOG("create_terminal_pty: hr=0x%08X, terminal_read=%p terminal_write=%p", static_cast<unsigned long>(hr),
        terminal_read_pipe.get(), terminal_write_pipe.get());

    win32::throw_hresult(win32::hresult(hr));
}

// 完成 ConDrv CONNECT 请求，取得 \Input 和 \Output 客户端句柄。两个句柄
// 必须随 conpty 会话保持存活；如果提前关闭，客户端后续 ReadFile/WriteFile
// 会看到管道断开。
void accept_condrv_connection(win32::handle_view condrv_server, PCCONSOLE_PORTABLE_ATTACH_MSG attach_msg,
                              win32::handle &condrv_input, win32::handle &condrv_output)
{
    LOG("accept_condrv_connection: calling accept_connection");
    auto connect_io = make_connect_msg(attach_msg);
    miniio::accept_connection(condrv_server, connect_io, condrv_input, condrv_output);
    LOG("accept_condrv_connection: input=%p output=%p", condrv_input.get(), condrv_output.get());
}

// 执行默认终端链路的第二跳：
//   inbox conhost -> corehost(IConsoleHandoff) -> Windows Terminal(ITerminalHandoff3)
//
// 此函数仍运行在 COM RPC 调用线程中，只负责把所有长期需要的句柄移动进
// handoff_result。com_server_entry 返回后，主线程才会启动 conpty_entry。
void complete_terminal_handoff(REFCLSID terminal_clsid, PCCONSOLE_PORTABLE_ATTACH_MSG attach_msg,
                               win32::handle_view terminal_signal_write, win32::handle_view reference_handle,
                               win32::handle_view condrv_server, win32::handle terminal_signal_read,
                               win32::handle_view client_process, handoff_result &result)
{
    auto client_pid = static_cast<DWORD>(attach_msg->Process);
    LOG("complete_terminal_handoff: client_pid=%lu", client_pid);

    // WT 需要持有 corehost 进程句柄来感知 PTY server 生命周期。这里复制的
    // 是当前进程伪句柄的真实句柄，随后通过 COM 传给 WT。
    auto corehost_process = win32::duplicate_self();
    LOG("complete_terminal_handoff: corehost_process=%p", corehost_process.get());

    auto connect_info = read_connect_message(condrv_server, attach_msg);
    auto startup_title = connect_title(connect_info);
    auto show_window = connect_show_window(connect_info);

    win32::handle terminal_read_pipe;
    win32::handle terminal_write_pipe;
    create_terminal_pty(terminal_clsid, terminal_signal_write, reference_handle, corehost_process.view(),
                        client_process, startup_title, show_window, terminal_read_pipe, terminal_write_pipe);

    win32::handle condrv_input;
    win32::handle condrv_output;
    accept_condrv_connection(condrv_server, attach_msg, condrv_input, condrv_output);

    // signal_read 是 WT 写信号管道的 corehost 读端。它不能在 RPC 返回时关闭，
    // conpty_entry 需要用它接收 resize、close 等 PtySignal。
    result.signal = std::move(terminal_signal_read);
    LOG("complete_terminal_handoff: signal_read=%p saved in result", result.signal.get());

    // 命名按 corehost 内部方向保存：
    //   vt_out: corehost 写出的 VT 字节，进入 WT 的读 pipe
    //   vt_in : WT 写回的输入/控制字节，corehost 从该 pipe 读取
    LOG("complete_terminal_handoff: result server=%p vt_out(W)=%p vt_in(R)=%p condrv_in=%p condrv_out=%p",
        condrv_server.get(), terminal_read_pipe.get(), terminal_write_pipe.get(), condrv_input.get(),
        condrv_output.get());

    result.vt_out = std::move(terminal_read_pipe);
    result.vt_in = std::move(terminal_write_pipe);
    result.condrv_input = std::move(condrv_input);
    result.condrv_output = std::move(condrv_output);
    LOG("complete_terminal_handoff: result filled, returning");
}

// ============================================================================
// Console Handoff COM 实现
// ============================================================================

// IConsoleHandoff + IClassFactory: 接收 inbox conhost 的会话移交。
// IUnknown 由 com::implements 自动生成。
struct console_handoff : com::implements<console_handoff, IConsoleHandoff, IClassFactory>
{
    win32::event &notifier;
    handoff_result &result;

    explicit console_handoff(win32::event &n, handoff_result &r) noexcept : notifier(n), result(r)
    {
    }

    // ── IClassFactory ────────────────────────────────────
    HRESULT STDMETHODCALLTYPE CreateInstance(IUnknown *outer, REFIID riid, void **obj) noexcept override
    {
        if (outer)
            return CLASS_E_NOAGGREGATION;
        return QueryInterface(riid, obj);
    }

    HRESULT STDMETHODCALLTYPE LockServer(BOOL) noexcept override
    {
        return S_OK;
    }

    // ── IConsoleHandoff ──────────────────────────────────
    HRESULT STDMETHODCALLTYPE EstablishHandoff(HANDLE server, HANDLE inputEvent, PCCONSOLE_PORTABLE_ATTACH_MSG msg,
                                               HANDLE signalPipe, HANDLE inboxProcess, HANDLE *process) override
    try
    {
        std::unique_lock lock{notifier, std::adopt_lock};

        LOG("EstablishHandoff: entry, server=%p event=%p msg=%p signal=%p inbox=%p process=%p", server, inputEvent, msg,
            signalPipe, inboxProcess, process);

        if (!process)
        {
            LOG("EstablishHandoff: process==nullptr → E_INVALIDARG");
            return E_INVALIDARG;
        }
        *process = nullptr;

        // --- 阶段 1: 复制句柄（COM 契约: 入参生命周期归调用方）---
        auto dup_server = win32::duplicate_handle(win32::handle_view(server));
        LOG("EstablishHandoff: dup_server=%p", dup_server.get());

        auto dup_event = win32::duplicate_handle(win32::handle_view(inputEvent));
        LOG("EstablishHandoff: dup_event=%p", dup_event.get());

        auto dup_signal = win32::duplicate_handle(win32::handle_view(signalPipe));
        LOG("EstablishHandoff: dup_signal=%p", dup_signal.get());

        // inboxProcess 句柄当前阶段未直接使用，按契约复制引用计数
        auto dup_inbox = win32::duplicate_handle(win32::handle_view(inboxProcess));
        LOG("EstablishHandoff: dup_inbox=%p", dup_inbox.get());

        // --- 阶段 2: 创建引用句柄与信号管道 ---
        win32::handle ref_handle = condrv::create_client_handle(dup_server.view(), L"\\Reference");
        LOG("EstablishHandoff: ref_handle=%p", ref_handle.get());

        auto [signal_read, signal_write] = win32::create_pipe();
        LOG("EstablishHandoff: signal pipe read=%p write=%p", signal_read.get(), signal_write.get());

        // --- 阶段 3: 打开目标客户端进程 ---
        // WT 会持有这个进程句柄来等待客户端退出，并可能需要 SET_INFORMATION
        // 权限做 QoS 调整；这与原版 ConptyConnection::InitializeFromHandoff
        // 对 client 句柄的需求一致。
        auto client_pid = static_cast<DWORD>(msg->Process);
        win32::handle client{::OpenProcess(
            PROCESS_QUERY_INFORMATION | PROCESS_SET_INFORMATION | PROCESS_VM_READ | SYNCHRONIZE, FALSE, client_pid)};
        win32::throw_last_error(!client.valid());
        LOG("EstablishHandoff: OpenProcess(pid=%lu)=%p", client_pid, client.get());

        // --- 阶段 4: 查询终端 CLSID 并执行移交 ---
        auto terminal_clsid = clsid::default_clsid(clsid::delegation_step::terminal);
        LOG("EstablishHandoff: terminal_clsid=%08X-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X", terminal_clsid.Data1,
            terminal_clsid.Data2, terminal_clsid.Data3, terminal_clsid.Data4[0], terminal_clsid.Data4[1],
            terminal_clsid.Data4[2], terminal_clsid.Data4[3], terminal_clsid.Data4[4], terminal_clsid.Data4[5],
            terminal_clsid.Data4[6], terminal_clsid.Data4[7]);

        if (terminal_clsid != clsid::zero)
        {
            complete_terminal_handoff(terminal_clsid, msg, signal_write.view(), ref_handle.view(), dup_server.view(),
                                      std::move(signal_read), client.view(), result);

            result.event = std::move(dup_event);
            result.server = std::move(dup_server);
            LOG("EstablishHandoff: terminal handoff completed");
        }

        // --- 阶段 5: 返回自身进程句柄供 inbox conhost 等待 ---
        LOG("EstablishHandoff: duplicating self for process handle");
        auto proc = win32::duplicate_self();
        *process = proc.release();
        LOG("EstablishHandoff: returning S_OK, proc=%p", *process);

        return S_OK;
    }
    catch (...)
    {
        LOG("EstablishHandoff: UNHANDLED EXCEPTION");
        return E_FAIL;
    }
};

// ============================================================================
// COM 服务器入口
// ============================================================================

handoff_result com_server_entry()
{
    LOG("com_server_entry: start");
    handoff_result result{};
    win32::com_apartment apt{COINIT_MULTITHREADED};
    win32::event notifier{win32::create_tag, true, false};

    auto handoff = com::make<console_handoff>(notifier, result);
    LOG("com_server_entry: console_handoff created");

    auto factory = handoff.as<IClassFactory>();
    LOG("com_server_entry: IClassFactory acquired");

    auto reg = com::register_object(clsid::corehost_console, factory);
    notifier.wait();

    LOG("com_server_entry: returning result");
    return result;
}

} // namespace comserver
