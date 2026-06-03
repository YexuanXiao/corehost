// ── comserver/com_server.cpp ──────────────────────────────
// -Embedding COM 服务器实现: 接收 inbox conhost 的控制台会话移交。

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
#include "default_console_size.hpp"
#include "win32/thread.hpp"
#include "utility/log.hpp"
#include "mutex"

namespace corehost::comserver
{

// 从 COM 便携连接消息填充 io_msg 描述符（供 accept_connection 和 read_input 使用）。
inline miniio::io_msg make_connect_msg(PCCONSOLE_PORTABLE_ATTACH_MSG msg)
{
    // m.body 在这里保持为空；portable attach 消息只携带 descriptor 字段。
    // 需要 CONNECT body 时必须再通过 READ_INPUT 以 Identifier 为键读取。
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
    // op.Identifier 必须来自 READ_IO/portable attach 的同一条请求；ConDrv 用它
    // 定位原始输入缓冲。offset/size 需落在该输入缓冲范围内。
    CD_IO_OPERATION op{};
    op.Identifier = msg.descriptor.Identifier;
    op.Buffer.Offset = offset;
    op.Buffer.Data = buffer;
    op.Buffer.Size = size;

    // r 是 DeviceIoControl 的 bytes-returned。IOCTL_READ_INPUT 不通过该值
    // 返回 payload 大小，成功条件只看 BOOL。
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

    // 零初始化让缺失/短读在后续字段校验中表现为安全的默认值；正常情况下
    // READ_INPUT 会填满整个结构。
    CONSOLE_SERVER_MSG connect_info{};

    // CONNECT 不需要分段读取；offset=0 表示从 CONSOLE_SERVER_MSG 起始处读取。
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

    // TitleLength 不包含末尾 NUL。title_chars 可为 0，表示调用方没有提供
    // lpTitle 或标题为空。
    const auto title_chars = connect_info.TitleLength / sizeof(WCHAR);
    if (connect_info.Title[title_chars] != L'\0')
        return {};

    LOG("CONNECT title accepted: title=%ls bytes=%lu", connect_info.Title, connect_info.TitleLength);
    return {connect_info.Title, title_chars};
}

// STARTUPINFO.wShowWindow 只有在 STARTF_USESHOWWINDOW 置位时有效。原版
// OpenConsole 通过 Settings::ApplyStartupInfo 得到最终值；当前不支持
// 快捷方式解析，因此这个轻量判断就是普通 CreateProcess 场景的等价路径。
WORD connect_show_window(const CONSOLE_SERVER_MSG &connect_info) noexcept
{
    // SW_SHOWDEFAULT 表示调用方没有指定 STARTUPINFO.wShowWindow，交给 WT
    // 按自身默认策略显示窗口。
    return (connect_info.StartupFlags & STARTF_USESHOWWINDOW) ? connect_info.ShowWindow : SW_SHOWDEFAULT;
}

// 实例化 Windows Terminal 的 ITerminalHandoff3 并执行 PTY 移交协商。
//   terminal_read_pipe  : WT 可读端，corehost 写入后映射为 vt_out
//   terminal_write_pipe : WT 可写端，corehost 读取后映射为 vt_in
void create_terminal_pty(REFCLSID terminal_clsid, win32::handle_view signal_write, win32::handle_view reference_handle,
                         win32::handle_view corehost_process, win32::handle_view client_process,
                         win32::wcstring_view startup_title, WORD show_window, win32::handle &terminal_read_pipe,
                         win32::handle &terminal_write_pipe)
{
    LOG("creating WT PTY handoff clsid=%08X-%04X-%04X", terminal_clsid.Data1, terminal_clsid.Data2,
        terminal_clsid.Data3);

    // terminal 指向 WT 的本地 COM server。创建失败会抛出 HRESULT，调用方
    // 不应继续 accept ConDrv CONNECT。
    auto terminal = com::create_instance<ITerminalHandoff3>(terminal_clsid, CLSCTX_LOCAL_SERVER);
    LOG("WT COM object created ptr=%p", terminal.get());

    // BSTR 生命周期必须覆盖 EstablishPtyHandoff 调用；WT 在调用内复制需要
    // 的 STARTUPINFO 字段。空标题兜底为 corehost。
    com::bstring title{!startup_title.empty() ? startup_title.c_str() : L"corehost"};

    // dwXCountChars/dwYCountChars 为 WT 初始字符尺寸；default_console_size 是
    // corehost 与 libcorehost 统一的 120x30。show_window 可能是 SW_SHOWDEFAULT
    // 或 CreateProcess lpStartupInfo 中显式给出的值。dwFlags 当前只声明字符
    // 列/行数有效，不支持窗口位置、像素尺寸、填充属性和快捷方式图标。
    TERMINAL_STARTUP_INFO startup{.pszTitle = title.get(),
                                  .dwXCountChars = static_cast<DWORD>(corehost::conpty::default_console_size.X),
                                  .dwYCountChars = static_cast<DWORD>(corehost::conpty::default_console_size.Y),
                                  .dwFlags = STARTF_USECOUNTCHARS,
                                  .wShowWindow = show_window};

    LOG("calling EstablishPtyHandoff title=%ls showWindow=%u signal=%p ref=%p server=%p client=%p",
        startup_title.c_str(), show_window, signal_write.get(), reference_handle.get(), corehost_process.get(),
        client_process.get());

    auto hr =
        terminal->EstablishPtyHandoff(terminal_read_pipe.put(), terminal_write_pipe.put(), signal_write.get(),
                                      reference_handle.get(), corehost_process.get(), client_process.get(), &startup);

    LOG_IF(FAILED(hr), "serious: EstablishPtyHandoff failed hr=0x%08lx", static_cast<unsigned long>(hr));
    LOG("EstablishPtyHandoff returned hr=0x%08lx terminalRead=%p terminalWrite=%p", static_cast<unsigned long>(hr),
        terminal_read_pipe.get(), terminal_write_pipe.get());

    win32::throw_hresult(win32::hresult(hr));
}

// 完成 ConDrv CONNECT 请求，取得 \Input 和 \Output 客户端句柄。两个句柄
// 必须随 conpty 会话保持存活；如果提前关闭，客户端后续 ReadFile/WriteFile
// 会看到管道断开。
void accept_condrv_connection(win32::handle_view condrv_server, PCCONSOLE_PORTABLE_ATTACH_MSG attach_msg,
                              win32::handle &condrv_input, win32::handle &condrv_output)
{
    LOG("accepting ConDrv CONNECT; input/output handles are expected");

    // accept_connection 会完成 CONNECT，并把 \Input/\Output 客户端句柄写入
    // 出参；失败时出参保持空句柄并抛出 Win32 错误。
    auto connect_io = make_connect_msg(attach_msg);
    miniio::accept_connection(condrv_server, connect_io, condrv_input, condrv_output);
    LOG("ConDrv CONNECT accepted input=%p output=%p", condrv_input.get(), condrv_output.get());
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
    // attach_msg->Process 是发起 CONNECT 的控制台客户端 pid；必须非 0，
    // 否则 OpenProcess 和 WT 客户端生命周期监控都没有意义。
    auto client_pid = static_cast<DWORD>(attach_msg->Process);
    LOG("completing second-hop handoff for client pid=%lu", client_pid);

    // WT 需要持有 corehost 进程句柄来感知 PTY server 生命周期。这里复制的
    // 是当前进程伪句柄的真实句柄，随后通过 COM 传给 WT。
    auto corehost_process = win32::duplicate_self();
    LOG("duplicated corehost process handle=%p", corehost_process.get());

    auto connect_info = read_connect_message(condrv_server, attach_msg);

    // startup_title 是对 connect_info.Title 的 view，必须在 connect_info
    // 生命周期内使用。show_window 已规约为 WT 可直接消费的值。
    auto startup_title = connect_title(connect_info);
    auto show_window = connect_show_window(connect_info);

    // terminal_read_pipe/terminal_write_pipe 由 WT 创建并通过 out 参数返回；
    // 成功后必须移动进 result，供主线程启动 conpty 会话。
    win32::handle terminal_read_pipe;
    win32::handle terminal_write_pipe;
    create_terminal_pty(terminal_clsid, terminal_signal_write, reference_handle, corehost_process.view(),
                        client_process, startup_title, show_window, terminal_read_pipe, terminal_write_pipe);

    // condrv_input/output 在 CONNECT accept 后才有效；它们是保持客户端控制台
    // 对象存活的实际 ConDrv 句柄。
    win32::handle condrv_input;
    win32::handle condrv_output;
    accept_condrv_connection(condrv_server, attach_msg, condrv_input, condrv_output);

    // signal_read 是 WT 写信号管道的 corehost 读端。它不能在 RPC 返回时关闭，
    // conpty_entry 需要用它接收 resize、close 等 PtySignal。
    result.signal = std::move(terminal_signal_read);
    LOG("signal pipe read handle saved in result=%p", result.signal.get());

    // 命名按 corehost 内部方向保存：
    //   vt_out: corehost 写出的 VT 字节，进入 WT 的读 pipe
    //   vt_in : WT 写回的输入/控制字节，corehost 从该 pipe 读取
    LOG("handoff result ready server=%p vtOut=%p vtIn=%p condrvIn=%p condrvOut=%p", condrv_server.get(),
        terminal_read_pipe.get(), terminal_write_pipe.get(), condrv_input.get(), condrv_output.get());

    result.vt_out = std::move(terminal_read_pipe);
    result.vt_in = std::move(terminal_write_pipe);
    result.condrv_input = std::move(condrv_input);
    result.condrv_output = std::move(condrv_output);
    LOG("second-hop handoff data moved to result");
}

// ============================================================================
// Console Handoff COM 实现
// ============================================================================

// IConsoleHandoff + IClassFactory: 接收 inbox conhost 的会话移交。
// IUnknown 由 com::implements 自动生成。
struct console_handoff : com::implements<console_handoff, IConsoleHandoff, IClassFactory>
{
    // notifier 是 com_server_entry 等待的手动复位事件。EstablishHandoff 返回
    // 时通过 unique_lock 析构触发 set()，唤醒主线程。
    win32::event &notifier;

    // result 由 COM RPC 线程填充，com_server_entry 在 notifier 置位后读取。
    // 事件等待形成跨线程可见性的同步边界。
    handoff_result &result;

    explicit console_handoff(win32::event &n, handoff_result &r) noexcept : notifier(n), result(r)
    {
    }

    // ── IClassFactory ────────────────────────────────────
    HRESULT STDMETHODCALLTYPE CreateInstance(IUnknown *outer, REFIID riid, void **obj) noexcept override
    {
        // 默认终端协议不支持 COM 聚合；outer 非空必须拒绝。
        if (outer)
            return CLASS_E_NOAGGREGATION;

        // obj 由 QueryInterface 校验；成功时返回请求接口，失败时保持 nullptr。
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
        // 作用域结束时通知 com_server_entry：本次 handoff 已经得到结果。
        std::unique_lock lock{notifier, std::adopt_lock};

        LOG("inbox handoff received server=%p event=%p msg=%p signal=%p inbox=%p processOut=%p", server, inputEvent,
            msg, signalPipe, inboxProcess, process);

        // process 是 COM out 参数。为空表示调用方违反接口契约；非空时先清空，
        // 避免失败路径留下未定义句柄值。
        if (!process)
        {
            LOG("serious: COM caller passed null process out parameter; returning E_INVALIDARG");
            return E_INVALIDARG;
        }
        *process = nullptr;

        // --- 阶段 1: 复制句柄（COM 契约: 入参生命周期归调用方）---
        // dup_server 是 ConDrv \Server 句柄；dup_event 是 InputAvailableEvent。
        // 二者成功 handoff 后会移动进 result，并由 conpty 会话继续拥有。
        auto dup_server = win32::duplicate_handle(win32::handle_view(server));
        LOG("duplicated ConDrv server=%p", dup_server.get());

        auto dup_event = win32::duplicate_handle(win32::handle_view(inputEvent));
        LOG("duplicated input event=%p", dup_event.get());

        // dup_signal/dup_inbox 当前只用于遵守 COM handoff 的句柄复制契约。
        // 终端第二跳使用 corehost 自己创建的 signal pipe。
        auto dup_signal = win32::duplicate_handle(win32::handle_view(signalPipe));
        LOG("duplicated inbox signal pipe=%p", dup_signal.get());

        // inboxProcess 句柄当前阶段未直接使用，按契约复制引用计数
        auto dup_inbox = win32::duplicate_handle(win32::handle_view(inboxProcess));
        LOG("duplicated inbox process=%p", dup_inbox.get());

        // --- 阶段 2: 创建引用句柄与信号管道 ---
        win32::handle ref_handle = condrv::create_client_handle(dup_server.view(), L"\\Reference");
        LOG("created ConDrv reference handle=%p", ref_handle.get());

        // signal_read 留给 corehost conpty signal 线程；signal_write 传给 WT。
        // 两端都有效时才能完成第二跳 Pty handoff。
        auto [signal_read, signal_write] = win32::create_pipe();
        LOG("created WT signal pipe read=%p write=%p", signal_read.get(), signal_write.get());

        // --- 阶段 3: 打开目标客户端进程 ---
        // WT 会持有这个进程句柄来等待客户端退出，并可能需要 SET_INFORMATION
        // 权限做 QoS 调整；这与原版 ConptyConnection::InitializeFromHandoff
        // 对 client 句柄的需求一致。
        // msg->Process 是客户端 pid，而不是 process group id。OpenProcess 返回
        // 的句柄在 create_terminal_pty 调用期间保持有效，WT 会复制自己的副本。
        auto client_pid = static_cast<DWORD>(msg->Process);
        win32::handle client{::OpenProcess(
            PROCESS_QUERY_INFORMATION | PROCESS_SET_INFORMATION | PROCESS_VM_READ | SYNCHRONIZE, FALSE, client_pid)};
        win32::throw_last_error(!client.valid());
        LOG("client process opened pid=%lu handle=%p", client_pid, client.get());

        // --- 阶段 4: 查询终端 CLSID 并执行移交 ---
        // zero CLSID 表示没有可用的第二跳终端。此时仍返回 S_OK 和自身进程
        // 句柄，让 inbox conhost 的协议层有确定的等待对象；result 保持空。
        auto terminal_clsid = clsid::default_clsid(clsid::delegation_step::terminal);
        LOG("terminal CLSID selected %08X-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X", terminal_clsid.Data1,
            terminal_clsid.Data2, terminal_clsid.Data3, terminal_clsid.Data4[0], terminal_clsid.Data4[1],
            terminal_clsid.Data4[2], terminal_clsid.Data4[3], terminal_clsid.Data4[4], terminal_clsid.Data4[5],
            terminal_clsid.Data4[6], terminal_clsid.Data4[7]);

        LOG_IF(terminal_clsid == clsid::zero, "no terminal CLSID configured; empty result is expected");
        if (terminal_clsid != clsid::zero)
        {
            complete_terminal_handoff(terminal_clsid, msg, signal_write.view(), ref_handle.view(), dup_server.view(),
                                      std::move(signal_read), client.view(), result);

            result.event = std::move(dup_event);
            result.server = std::move(dup_server);
            LOG("inbox handoff completed; conpty session should start after COM returns");
        }

        // --- 阶段 5: 返回自身进程句柄供 inbox conhost 等待 ---
        // proc.release() 后句柄所有权转移给 COM 调用方；本进程不再关闭它。
        LOG("returning corehost process handle to inbox conhost");
        auto proc = win32::duplicate_self();
        *process = proc.release();
        LOG("returning S_OK process=%p", *process);

        return S_OK;
    }
    catch (...)
    {
        LOG("serious: EstablishHandoff failed unexpectedly; returning E_FAIL");
        return E_FAIL;
    }
};

// ============================================================================
// COM 服务器入口
// ============================================================================

handoff_result com_server_entry()
{
    LOG("COM server entry started; waiting for inbox handoff");

    // result 初始为空。只有 EstablishHandoff 成功完成第二跳时，server/event/
    // vt/condrv/signal 等句柄才变为有效。
    handoff_result result{};

    // 默认终端 COM 回调可能发生在 RPC 线程池线程；MTA 满足这里的无 UI、
    // 无 STA 消息泵需求。
    win32::com_apartment apt{COINIT_MULTITHREADED};

    // 手动复位、初始未触发。主线程 wait，COM RPC 线程在任意返回路径 set。
    win32::event notifier{win32::create_tag, true, false};

    // handoff 对象同时作为 class factory 和实际 IConsoleHandoff 实例。
    // com::register_object 的生命周期覆盖 notifier.wait()。
    auto handoff = com::make<console_handoff>(notifier, result);
    LOG("console handoff object created");

    // factory 非空表示 QueryInterface(IClassFactory) 成功；注册后 COM 可激活
    // clsid::corehost_console 并调用 EstablishHandoff。
    auto factory = handoff.as<IClassFactory>();
    LOG("class factory acquired");

    // reg 析构时撤销类对象。REGCLS_SINGLEUSE 在 register_object 默认参数中，
    // 因此一次 handoff 后本入口返回。
    auto reg = com::register_object(clsid::corehost_console, factory);
    notifier.wait();

    LOG("COM server wait completed; returning handoff result server=%p vtIn=%p vtOut=%p", result.server.get(),
        result.vt_in.get(), result.vt_out.get());
    return result;
}

} // namespace corehost::comserver
