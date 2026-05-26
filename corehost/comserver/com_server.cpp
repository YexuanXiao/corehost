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
#include "miniio/io_loop.hpp"
#include "miniio/signal.hpp"
#include "os/Console/conmsgl1.h"
#include "win32/thread.hpp"
#include "utility/log.hpp"
#include "mutex"

namespace comserver
{

// ============================================================================
// 终端移交子流程
// ============================================================================

win32::wcstring_view read_connect_title(win32::handle_view server, PCCONSOLE_PORTABLE_ATTACH_MSG msg,
                                        CONSOLE_SERVER_MSG &data)
{
    auto connect_msg = miniio::make_connect_msg(msg);
    miniio::read_input(server, connect_msg, 0, &data, sizeof(data));

    if (data.TitleLength > sizeof(data.Title) - sizeof(WCHAR) || (data.TitleLength % sizeof(WCHAR)) != 0)
        return {};

    const auto title_chars = data.TitleLength / sizeof(WCHAR);
    if (data.Title[title_chars] != L'\0')
        return {};

    LOG("read_connect_title: title=%ls length=%lu bytes", data.Title, data.TitleLength);
    return {data.Title, title_chars};
}

// 实例化 Windows Terminal 的 ITerminalHandoff3 并执行 PTY 移交协商。
// 返回 WT 输入/输出句柄对：
//   first  (wt_in)  : WT 可读端，供 corehost 写入
//   second (wt_out) : WT 可写端，供 corehost 读取
miniio::io_handles negotiate_terminal_pty(REFCLSID terminal_clsid, win32::handle_view signal_write,
                                          win32::handle_view ref_handle, win32::handle_view server_process,
                                          win32::handle_view client_process, win32::wcstring_view startup_title)
{
    LOG("negotiate_terminal_pty: clsid=%08X-%04X-%04X...", terminal_clsid.Data1, terminal_clsid.Data2,
        terminal_clsid.Data3);

    auto terminal = com::create_instance<ITerminalHandoff3>(terminal_clsid, CLSCTX_LOCAL_SERVER);
    LOG("negotiate_terminal_pty: terminal COM obj=%p", terminal.get());

    com::bstring title{!startup_title.empty() ? startup_title.c_str() : L"corehost"};
    TERMINAL_STARTUP_INFO startup{.pszTitle = title.get(),
                                  .dwXCountChars = 120,
                                  .dwYCountChars = 30,
                                  .dwFlags = STARTF_USECOUNTCHARS,
                                  .wShowWindow = SW_SHOWDEFAULT};

    win32::handle wt_in, wt_out;
    LOG("negotiate_terminal_pty: EstablishPtyHandoff(title=%ls signal=%p ref=%p server=%p client=%p)",
        startup_title.c_str(), signal_write.get(), ref_handle.get(), server_process.get(), client_process.get());

    auto hr = terminal->EstablishPtyHandoff(wt_in.put(), wt_out.put(), signal_write.get(), ref_handle.get(),
                                            server_process.get(), client_process.get(), &startup);

    LOG("negotiate_terminal_pty: hr=0x%08X, wt_in=%p wt_out=%p", static_cast<unsigned long>(hr), wt_in.get(),
        wt_out.get());

    win32::throw_hresult(win32::hresult(hr));

    return {std::move(wt_in), std::move(wt_out)};
}

// 通过 miniio 接受连接，返回 IO 句柄对。
miniio::io_handles accept_io_connection(win32::handle_view server, PCCONSOLE_PORTABLE_ATTACH_MSG msg)
{
    LOG("accept_io_connection: calling accept_connection");
    auto conn_msg = miniio::make_connect_msg(msg);
    auto handles = miniio::accept_connection(server, conn_msg);
    LOG("accept_io_connection: in=%p out=%p", handles.input.get(), handles.output.get());
    return handles;
}

// 执行第二跳：ITerminalHandoff3 → PTY 协商 → IO 连接 → 信号线程 → 填充 result。
void terminal_handoff(REFCLSID terminal_clsid, PCCONSOLE_PORTABLE_ATTACH_MSG msg, win32::handle_view signal_write,
                      win32::handle_view ref_handle, win32::handle_view dup_server, win32::handle signal_read,
                      win32::handle_view client, handoff_result &result)
{
    auto client_pid = static_cast<DWORD>(msg->Process);
    LOG("perform_terminal_handoff: client_pid=%lu", client_pid);

    // 1. 获取当前进程句柄供 WT 引用
    auto server_h = win32::duplicate_self();
    LOG("perform_terminal_handoff: server_h(our process)=%p", server_h.get());

    // 2. 与 Windows Terminal 协商 PTY 句柄
    CONSOLE_SERVER_MSG data{};
    auto startup_title = read_connect_title(dup_server, msg, data);
    auto [wt_in, wt_out] =
        negotiate_terminal_pty(terminal_clsid, signal_write, ref_handle, server_h.view(), client, startup_title);

    // 3. 建立 IO 连接（使用 dup_server，而非当前进程句柄）
    auto conn_handles = accept_io_connection(dup_server, msg);

    // 4. 将 signal_read 传递给 conpty_entry（由 conpty 信号线程处理 PtySignal::ResizeWindow）
    //    注意: miniio::signal_thread_proc 只处理 CONSOLECONTROL，不处理 PtySignal
    result.signal = std::move(signal_read);
    LOG("perform_terminal_handoff: signal_read=%p saved in result", result.signal.get());

    // 5. 填充结果
    //    wt_in  : WT 可读端 → corehost 写入 → 映射为 vt_out
    //    wt_out : WT 可写端 → corehost 读取 → 映射为 vt_in
    LOG("perform_terminal_handoff: filling result: server=%p vt_out(W)=%p vt_in(R)=%p", dup_server.get(), wt_in.get(),
        wt_out.get());

    result.vt_out = std::move(wt_in); // corehost 写 → WT 读
    result.vt_in = std::move(wt_out); // corehost 读 ← WT 写
    result.handles = std::move(conn_handles);
    LOG("perform_terminal_handoff: result filled, returning");
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
        // 供 perform_terminal_handoff 与后续 fallback 共享同一进程句柄。
        //   - perform_terminal_handoff 需要: QUERY | SET | VM_READ | SYNCHRONIZE
        //   - fallback 等待需要: SYNCHRONIZE
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
            terminal_handoff(terminal_clsid, msg, signal_write.view(), ref_handle.view(), dup_server.view(),
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
