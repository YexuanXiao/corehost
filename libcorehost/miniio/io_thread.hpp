// ── miniio/io_thread.hpp ─────────────────────────────────
// ConDrv 驱动通信 — IOCTL 原语
//
// ConDrv (condrv.sys) 是 Windows 内核控制台驱动。
// 用户态组件通过 IOCTL 与其交互。
//
// 三个 IOCTL 的含义：
//   IOCTL_READ_IO (1, METHOD_OUT_DIRECT)
//     — 读取下一条消息。lpInBuffer 传入上一轮的 CD_IO_COMPLETE
//       （首次为 null）。lpOutBuffer 接收 CD_IO_DESCRIPTOR + body。
//     返回 FALSE + ERROR_IO_PENDING 表示暂无消息，
//     调用方应 WaitForSingleObject(server, 0) 后再试。
//     ERROR_PIPE_NOT_CONNECTED / BROKEN_PIPE / NO_DATA
//     表示客户端全部断开，应退出循环。
//   IOCTL_COMPLETE_IO (2, METHOD_NEITHER)
//     — 提交完成结果。CONNECT 消息的完成包含 CD_CONNECTION_INFO。
//   IOCTL_SET_SERVER (7, METHOD_NEITHER)
//     — 注册 InputAvailableEvent。驱动在消息就绪时 SetEvent，
//       使 WaitForSingleObject(server, 0) 可唤醒。
//
// 异步完成模型：不立即 complete_io 非 CONNECT 消息——填充
//   msg.complete，在下一轮 read_io 作为 lpInBuffer 提交。
//   比每次调用 complete_io 少一次 IOCTL 往返。

#pragma once
#include <windows.h>
#include <winioctl.h>
#include <winternl.h>
#include <cstdint>
#include "win32/handle.hpp"
#include "win32/error.hpp"
#include "win32/string.hpp"
#include "os/Console/condrv.h"
#include "ntapi/condrv.hpp"
#include "IConsoleHandoff.h"
#include "utility/log.hpp"

namespace miniio
{

inline constexpr DWORD IOCTL_READ_IO = CTL_CODE(FILE_DEVICE_CONSOLE, 1, METHOD_OUT_DIRECT, FILE_ANY_ACCESS);
inline constexpr DWORD IOCTL_COMPLETE_IO = CTL_CODE(FILE_DEVICE_CONSOLE, 2, METHOD_NEITHER, FILE_ANY_ACCESS);
inline constexpr DWORD IOCTL_SET_SERVER = CTL_CODE(FILE_DEVICE_CONSOLE, 7, METHOD_NEITHER, FILE_ANY_ACCESS);

enum class read_io_result : uint8_t
{
    got_message,
    no_message,
    disconnected,
};

// ── 消息缓冲区 ──────────────────────────────────────────────
// 包含完成块 + 描述符 + 消息体（最大 4KB）。
//
// 消息在 ConDrv 驱动和用户态之间的流动：
//   1. read_io(server, nullptr, msg)      — 读取首条消息
//   2. Handler 填充 msg.complete            — 准备完成信息
//   3. read_io(server, &msg.complete, msg)  — 下一轮循环中提交上轮完成 + 读取新消息
//   4. 重复步骤 2-3 直到断开
//
// 这种设计避免了多余的 complete_io IOCTL：完成信息在下一轮
// read_io 的 lpInBuffer 中提交，而不是单独调用 DeviceIoControl。
struct io_msg
{
    CD_IO_COMPLETE complete;
    CD_IO_DESCRIPTOR descriptor;
    BYTE body[4096];
};

// ── 服务端 I/O 句柄对 ───────────────────────────────────────
// 服务端持有的客户端句柄，维持客户端 I/O 通道存活。
struct io_handles
{
    win32::handle input;
    win32::handle output;
};

// ── set_server_info ────────────────────────────────────────
// 对应原版 ConsoleCreateIoThread 中 else 分支:
//   CD_IO_SERVER_INFORMATION ServerInformation;
//   ServerInformation.InputAvailableEvent = g.hInputEvent.get();
//   g.pDeviceComm->SetServerInformation(&ServerInformation);
inline void set_server_info(win32::handle_view server, win32::handle_view event)
{
    CD_IO_SERVER_INFORMATION info{};
    info.InputAvailableEvent = event.get();
    DWORD r = 0;
    if (!::DeviceIoControl(server.get(), IOCTL_SET_SERVER, &info, sizeof(info), nullptr, 0, &r, nullptr))
    {
        // 忽略错误: COM marshal 句柄可能不支持 IOCTL_SET_SERVER (err=22)
        // 或收件箱已注册 (err=183)。均不致命——调用方使用外部传入的已注册事件。
    }
}

// ── read_io ────────────────────────────────────────────────
// 对应原版 ConDrvDeviceComm::ReadIo (terminal/src/server/ConDrvDeviceComm.cpp:39-55)
//
//   原始: DeviceIoControl(IOCTL_CONDRV_READ_IO) → ERROR_IO_PENDING 时
//         WaitForSingleObjectEx(server, 0, FALSE) — timeout=0 忙轮询, 不阻塞
//         返回 S_OK, 上层 ConsoleIoThread 继续循环
//
// 返回 false → 客户端全部断开 (ERROR_PIPE_NOT_CONNECTED/BROKEN_PIPE/NO_DATA)
inline bool read_io(win32::handle_view server, win32::handle_view event, CD_IO_COMPLETE *prev, io_msg &msg)
{
    std::memset(&msg.descriptor, 0, sizeof(msg.descriptor));
    DWORD r = 0;
    BOOL ok = ::DeviceIoControl(server.get(), IOCTL_READ_IO, prev, prev ? sizeof(CD_IO_COMPLETE) : 0, &msg.descriptor,
                                sizeof(msg.descriptor) + sizeof(msg.body), &r, nullptr);
    if (ok)
    {
        return true;
    }

    auto err = win32::get_last_error();
    if (err == win32::error::io_pending)
    {
        ::WaitForSingleObject(event.get(), 0);
        return true;
    }
    if (err == win32::error::pipe_not_connected || err == win32::error::broken_pipe || err == win32::error::no_data)
    {
        return false;
    }
    LOG("read_io: unexpected error %u", static_cast<unsigned>(err));
    throw err;
}

inline read_io_result read_io_try(win32::handle_view server, CD_IO_COMPLETE *prev, io_msg &msg)
{
    std::memset(&msg.descriptor, 0, sizeof(msg.descriptor));
    DWORD r = 0;
    BOOL ok = ::DeviceIoControl(server.get(), IOCTL_READ_IO, prev, prev ? sizeof(CD_IO_COMPLETE) : 0, &msg.descriptor,
                                sizeof(msg.descriptor) + sizeof(msg.body), &r, nullptr);
    if (ok)
        return read_io_result::got_message;

    auto err = win32::get_last_error();
    if (err == win32::error::io_pending)
        return read_io_result::no_message;
    if (err == win32::error::pipe_not_connected || err == win32::error::broken_pipe || err == win32::error::no_data)
        return read_io_result::disconnected;
    LOG("read_io_try: unexpected error %u", static_cast<unsigned>(err));
    throw err;
}
inline void complete_io(win32::handle_view server, CD_IO_COMPLETE &comp)
{
    DWORD r = 0;
    if (!::DeviceIoControl(server.get(), IOCTL_COMPLETE_IO, &comp, sizeof(comp), nullptr, 0, &r, nullptr))
        win32::throw_last_error();
}

// ── 准备完成信息 ────────────────────────────────────────────
//
// 设置 msg.complete 的公共字段：
//   - Identifier 从 msg.descriptor.Identifier 复制
//   - IoStatus.Status = status（0=成功，负值=NTSTATUS 错误）
//   - IoStatus.Information = info（完成的字节数或句柄值）
//   - Write 清空（调用方可继续填充 CONNECT 的 CD_CONNECTION_INFO）
//
// 返回值是对 msg.complete 的引用，允许链式调用：
//   prepare_completion(msg, 0, sizeof(info)).Write.Data = &info;
inline CD_IO_COMPLETE &prepare_completion(io_msg &msg, LONG status = 0, ULONG_PTR info = 0)
{
    msg.complete.Identifier = msg.descriptor.Identifier;
    msg.complete.IoStatus.Status = status;
    msg.complete.IoStatus.Information = info;
    msg.complete.Write.Data = nullptr;
    msg.complete.Write.Size = 0;
    msg.complete.Write.Offset = 0;
    return msg.complete;
}

// 从 io_msg 提取便携连接消息（供 COM 接口使用）。
inline CONSOLE_PORTABLE_ATTACH_MSG make_portable_attach_msg(const io_msg &msg)
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

// 从 COM 便携连接消息填充 io_msg 描述符（供 accept_connection 使用）。
inline io_msg make_connect_msg(PCCONSOLE_PORTABLE_ATTACH_MSG msg)
{
    io_msg m{};
    m.descriptor.Identifier.LowPart = msg->IdLowPart;
    m.descriptor.Identifier.HighPart = msg->IdHighPart;
    m.descriptor.Process = static_cast<decltype(m.descriptor.Process)>(msg->Process);
    m.descriptor.Object = static_cast<decltype(m.descriptor.Object)>(msg->Object);
    m.descriptor.Function = msg->Function;
    m.descriptor.InputSize = msg->InputSize;
    m.descriptor.OutputSize = msg->OutputSize;
    return m;
}

// ── CONNECT 处理 ──────────────────────────────────────────
// 创建客户端句柄对并返回连接信息。
//
// NtOpenFile
//   ConDrv 的客户端对象是内核对象，不通过 NT 命名空间暴露。
//   创建它们的唯一方式是 NtOpenFile，以 Server 句柄为 RootDirectory、
//   对象名为 \Input / \Output。
//
// CD_CONNECTION_INFORMATION 中 Process 为何复用 Input 句柄值？
//   ConDrv 的 PutHandle/GetHandle 最终调用 ObReferenceObjectByPointer，
//   ULONG_PTR 只是不透明令牌。三个字段可以不互异——原版 conhost 的
//   ProcessHandleList 也只在首次连接时创建 Process 句柄。
//
// accept_connection() 返回 io_handles 而非内部关闭：
//   调用方必须保持这些句柄存活。ConDrv 在客户端 I/O 操作时
//   通过已注册的句柄查找对应服务端。若服务端关闭句柄，客户端
//   后续 WriteFile/ReadFile 会收到 ERROR_BROKEN_PIPE。

inline io_handles accept_connection(win32::handle_view server, io_msg &msg)
{
    auto in_h = condrv::create_client_handle(server, L"\\Input");
    auto out_h = condrv::create_client_handle(server, L"\\Output");

    CD_CONNECTION_INFORMATION conn{};
    conn.Process = reinterpret_cast<ULONG_PTR>(in_h.get());
    conn.Input = reinterpret_cast<ULONG_PTR>(in_h.get());
    conn.Output = reinterpret_cast<ULONG_PTR>(out_h.get());

    prepare_completion(msg);
    msg.complete.IoStatus.Information = sizeof(CD_CONNECTION_INFORMATION);
    msg.complete.Write.Data = &conn;
    msg.complete.Write.Size = sizeof(CD_CONNECTION_INFORMATION);
    msg.complete.Write.Offset = 0;

    complete_io(server, msg.complete);

    return {std::move(in_h), std::move(out_h)};
}

// ── 非 CONNECT 消息分派（mini console 模式） ─────────────
//
// 提供最小化控制台所需的 I/O 响应，使客户端进程能正常运行
// 到结束而非卡死在 ReadConsole/WriteConsole。
//
//   DISCONNECT    — 客户端退出，释放 I/O 句柄
//   CREATE_OBJECT — 创建 \Input / \Output 句柄
//   CLOSE_OBJECT  — 关闭句柄，直接确认
//   RAW_WRITE     — 丢弃数据但确认（防止客户端阻塞）
//   RAW_READ      — 返回 0 字节 EOF
//   USER_DEFINED  — 不支持，返回 STATUS_UNSUCCESSFUL
//   RAW_FLUSH     — 直接确认
inline void dispatch_non_connect(win32::handle_view server, io_msg &msg, io_handles &handles)
{
    switch (msg.descriptor.Function)
    {
    case 0:
        break;
    case CONSOLE_IO_CONNECT:
        break;
    case CONSOLE_IO_DISCONNECT:
        handles.input.clear();
        handles.output.clear();
        prepare_completion(msg);
        break;

    case CONSOLE_IO_CREATE_OBJECT: {
        auto *req = reinterpret_cast<CD_CREATE_OBJECT_INFORMATION *>(msg.body);
        auto type = req->ObjectType;
        if (type == CD_IO_OBJECT_TYPE_GENERIC)
        {
            if ((req->DesiredAccess & (GENERIC_READ | GENERIC_WRITE)) == GENERIC_READ)
                type = CD_IO_OBJECT_TYPE_CURRENT_INPUT;
            else if ((req->DesiredAccess & (GENERIC_READ | GENERIC_WRITE)) == GENERIC_WRITE)
                type = CD_IO_OBJECT_TYPE_CURRENT_OUTPUT;
        }

        win32::handle new_handle;
        switch (type)
        {
        case CD_IO_OBJECT_TYPE_CURRENT_INPUT:
            new_handle = condrv::create_client_handle(server, L"\\Input");
            break;
        case CD_IO_OBJECT_TYPE_CURRENT_OUTPUT:
        case CD_IO_OBJECT_TYPE_NEW_OUTPUT:
            new_handle = condrv::create_client_handle(server, L"\\Output");
            break;
        default:
            prepare_completion(msg, 0xC0000001 /*STATUS_UNSUCCESSFUL*/);
            return;
        }
        prepare_completion(msg, 0, reinterpret_cast<ULONG_PTR>(new_handle.release()));
        break;
    }

    case CONSOLE_IO_CLOSE_OBJECT:
        prepare_completion(msg);
        break;

    case CONSOLE_IO_RAW_WRITE:
        prepare_completion(msg, 0, msg.descriptor.InputSize);
        break;

    case CONSOLE_IO_RAW_READ:
        prepare_completion(msg);
        break;

    case CONSOLE_IO_USER_DEFINED:
        prepare_completion(msg, 0xC0000001 /*STATUS_UNSUCCESSFUL*/);
        break;

    case CONSOLE_IO_RAW_FLUSH:
        prepare_completion(msg);
        break;

    default:
        std::unreachable();
    }
}

// ── passthrough_handler ──────────────────────────────────
// 最简单的 I/O 处理器：接受所有 CONNECT，非 CONNECT 消息
// 走 dispatch_non_connect。适用于不需要 COM 移交的
// 纯 mini console 场景。
//
// defterm 不需要单独的 fallback_handler，是因为它的
// connect_handler 在 handoff 失败时（handle_no_terminal /
// handle_elevated_connect）已经做了 accept_connection +
// GenerateConsoleCtrlEvent，然后返回 false 让 run_io_loop
// 继续用 dispatch_non_connect 处理残余消息——fallback 是
// 嵌入在正常流程中的，不需要额外 handler。
//
// comserver 则不同：terminal_handoff 运行在 COM RPC 线程上，
// 失败时不能"继续事件循环"（没有循环上下文），需要就地启动
// 一个独立的 passthrough_handler 来服务 ConDrv 消息直到客户端
// 断开。
struct passthrough_handler
{
    miniio::io_handles handles;

    bool on_connect(miniio::io_msg &msg)
    {
        handles = miniio::accept_connection(server, msg);
        return true;
    }

    bool on_message(miniio::io_msg &msg)
    {
        miniio::dispatch_non_connect(server, msg, handles);
        return true;
    }

    void on_idle()
    {
    } // no-op: passthrough 无需检查 PTY

    bool has_pending() const
    {
        return false;
    }
    bool should_exit() const
    {
        return false;
    }

    win32::handle_view server;
};

} // namespace miniio
