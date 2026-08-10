// ── libcorehost/condrv_io.hpp ───────────────────────────────
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
//     调用方应先对 server 做一次非阻塞等待后再试。
//     ERROR_PIPE_NOT_CONNECTED / BROKEN_PIPE / NO_DATA
//     表示客户端全部断开，应退出循环。
//   IOCTL_COMPLETE_IO (2, METHOD_NEITHER)
//     — 提交完成结果。CONNECT 消息的完成包含 CD_CONNECTION_INFO。
//   IOCTL_SET_SERVER (7, METHOD_NEITHER)
//     — 注册 InputAvailableEvent。驱动在消息就绪时 SetEvent，
//       使 server 的非阻塞等待可唤醒。
//
// 异步完成模型：不立即 complete_io 非 CONNECT 消息——填充
//   msg.complete，在下一轮 read_io 作为 lpInBuffer 提交。
//   比每次调用 complete_io 少一次 IOCTL 往返。

#pragma once
#include <windows.h>
#include <winioctl.h>
#include <winternl.h>
#include <cstdint>
#include <span>
#include "win32/handle.hpp"
#include "win32/error.hpp"
#include "win32/event.hpp"
#include "win32/io.hpp"
#include "win32/string.hpp"
#include "os/Console/condrv.h"
#include "ntapi/condrv.hpp"
#include "utility/log.hpp"

namespace corehost::condrv_io
{

inline constexpr DWORD IOCTL_READ_IO = CTL_CODE(FILE_DEVICE_CONSOLE, 1, METHOD_OUT_DIRECT, FILE_ANY_ACCESS);
inline constexpr DWORD IOCTL_COMPLETE_IO = CTL_CODE(FILE_DEVICE_CONSOLE, 2, METHOD_NEITHER, FILE_ANY_ACCESS);
inline constexpr DWORD IOCTL_READ_INPUT = CTL_CODE(FILE_DEVICE_CONSOLE, 3, METHOD_NEITHER, FILE_ANY_ACCESS);
inline constexpr DWORD IOCTL_SET_SERVER = CTL_CODE(FILE_DEVICE_CONSOLE, 7, METHOD_NEITHER, FILE_ANY_ACCESS);

enum class read_io_result : uint8_t
{
    got_message,  // READ_IO 完成，cur 中有一条新消息
    pending,      // READ_IO 已挂起，等待完成事件
    disconnected, // server 已断开
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

// ── set_server_info ────────────────────────────────────────
// 对应原版 ConsoleCreateIoThread 中 else 分支:
//   CD_IO_SERVER_INFORMATION ServerInformation;
//   ServerInformation.InputAvailableEvent = g.hInputEvent.get();
//   g.pDeviceComm->SetServerInformation(&ServerInformation);
inline void set_server_info(win32::handle_view server, win32::handle_view event) noexcept
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

// ── overlapped READ_IO 状态 ──────────────────────────────
// 同一时刻最多一个在飞的 READ_IO；完成事件（自动复位）与主循环的其他
// 事件一起交给 WaitForMultipleObjects，消息到达事件驱动，无需轮询。
//
// 自动复位依赖两个前提（勿改，除非同步改造等待者）：
// 1. 唯一等待者是阶段 3 的 wait，signaled 返回后必跟 read_io_finish，
//    事件总是被消费；
// 2. ConDrv 同步完成（DeviceIoControl 返回 TRUE）时不 SetEvent——否则
//    残留事件会让下一轮 read_io_finish 对未完成的新请求误取回。
struct read_io_op
{
    OVERLAPPED ov{};
    win32::event ev{win32::create_tag, false, false};
    bool in_flight{};

    read_io_op()
    {
        ov.hEvent = ev.get();
    }

    // Move-only；移动后重绑 hEvent 到本对象的事件。
    read_io_op(const read_io_op &) = delete;
    read_io_op &operator=(const read_io_op &) = delete;

    read_io_op(read_io_op &&other) noexcept = default;
    read_io_op &operator=(read_io_op &&other) noexcept
    {
        if (this != &other)
        {
            ev = std::move(other.ev);
            ov = {};
            ov.hEvent = ev.get();
            in_flight = other.in_flight;
            other.in_flight = false;
        }
        return *this;
    }

    [[nodiscard]] bool pending() const noexcept
    {
        return in_flight;
    }

    // overlapped 完成事件；与主循环其他事件一起交给 WaitForMultipleObjects。
    [[nodiscard]] win32::handle_view event() const noexcept
    {
        return ev.view();
    }
};

// 发起 overlapped READ_IO。prev 是上一轮的 completion（首轮为 null），
// msg 是接收缓冲（调用方保证 pending 期间存活）。返回 got_message 表示
// 立即可用；pending 表示请求已挂起，完成事件就绪后调用 read_io_finish；
// disconnected 表示 server 断开。不可恢复错误抛 win32::error。
inline read_io_result read_io_begin(win32::handle_view server, CD_IO_COMPLETE *prev, io_msg &msg, read_io_op &op)
{
    std::memset(&msg.descriptor, 0, sizeof(msg.descriptor));
    DWORD r = 0;
    BOOL ok = ::DeviceIoControl(server.get(), IOCTL_READ_IO, prev, prev ? sizeof(CD_IO_COMPLETE) : 0, &msg.descriptor,
                                sizeof(msg.descriptor) + sizeof(msg.body), &r, &op.ov);
    if (ok)
        return read_io_result::got_message;

    auto err = win32::get_last_error();
    if (err == win32::error::io_pending)
    {
        op.in_flight = true;
        return read_io_result::pending;
    }
    if (err == win32::error::pipe_not_connected || err == win32::error::broken_pipe || err == win32::error::no_data)
        return read_io_result::disconnected;
    LOG("read_io_begin: unexpected error %u", static_cast<unsigned>(err));
    throw err;
}

// 完成事件触发后取回 overlapped READ_IO 结果。返回 got_message（msg 中有
// 新消息）或 disconnected。不可恢复错误抛 win32::error。
inline read_io_result read_io_finish(win32::handle_view server, io_msg &msg, read_io_op &op)
{
    op.in_flight = false;
    DWORD r = 0;
    BOOL ok = ::GetOverlappedResult(server.get(), &op.ov, &r, FALSE);
    if (ok)
        return read_io_result::got_message;

    auto err = win32::get_last_error();
    if (err == win32::error::pipe_not_connected || err == win32::error::broken_pipe || err == win32::error::no_data)
        return read_io_result::disconnected;
    LOG("read_io_finish: unexpected error %u", static_cast<unsigned>(err));
    throw err;
}
inline void complete_io(win32::handle_view server, CD_IO_COMPLETE &comp)
{
    DWORD r = 0;
    if (!::DeviceIoControl(server.get(), IOCTL_COMPLETE_IO, &comp, sizeof(comp), nullptr, 0, &r, nullptr))
        win32::throw_last_error();
}

inline void read_input(win32::handle_view server, LUID identifier, ULONG offset, std::span<BYTE> destination)
{
    if (destination.empty())
        return;

    CD_IO_OPERATION operation{};
    operation.Identifier = identifier;
    operation.Buffer.Data = destination.data();
    operation.Buffer.Size = static_cast<ULONG>(destination.size());
    operation.Buffer.Offset = offset;

    DWORD r = 0;
    if (!::DeviceIoControl(server.get(), IOCTL_READ_INPUT, &operation, sizeof(operation), nullptr, 0, &r, nullptr))
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
inline CD_IO_COMPLETE &prepare_completion(io_msg &msg, LONG status = 0, ULONG_PTR info = 0) noexcept
{
    msg.complete.Identifier = msg.descriptor.Identifier;
    msg.complete.IoStatus.Status = status;
    msg.complete.IoStatus.Information = info;
    msg.complete.Write.Data = nullptr;
    msg.complete.Write.Size = 0;
    msg.complete.Write.Offset = 0;
    return msg.complete;
}

// ── CONNECT 处理 ──────────────────────────────────────────
// 创建客户端句柄对并返回连接信息。
//
// NtOpenFile/NtCreateFile
//   ConDrv 的客户端对象是内核对象，不通过 NT 命名空间暴露。
//   创建它们的唯一方式是 NtOpenFile/NtCreateFile，以 Server 句柄为 RootDirectory、
//   对象名为 \Input / \Output。
//
// CD_CONNECTION_INFORMATION 中 Process 为何复用 Input 句柄值？
//   ConDrv 的 PutHandle/GetHandle 最终调用 ObReferenceObjectByPointer，
//   ULONG_PTR 只是不透明令牌。三个字段可以不互异——原版 conhost 的
//   ProcessHandleList 也只在首次连接时创建 Process 句柄。
//
// accept_connection() 通过出参返回客户端 I/O 句柄：
//   调用方必须保持这些句柄存活。ConDrv 在客户端 I/O 操作时
//   通过已注册的句柄查找对应服务端。若服务端关闭句柄，客户端
//   后续 WriteFile/ReadFile 会收到 ERROR_BROKEN_PIPE。

inline void accept_connection(win32::handle_view server, io_msg &msg, win32::handle &input, win32::handle &output)
{
    input = condrv::create_client_handle(server, L"\\Input");
    output = condrv::create_client_handle(server, L"\\Output");

    CD_CONNECTION_INFORMATION conn{};
    conn.Process = reinterpret_cast<ULONG_PTR>(input.get());
    conn.Input = reinterpret_cast<ULONG_PTR>(input.get());
    conn.Output = reinterpret_cast<ULONG_PTR>(output.get());

    prepare_completion(msg);
    msg.complete.IoStatus.Information = sizeof(CD_CONNECTION_INFORMATION);
    msg.complete.Write.Data = &conn;
    msg.complete.Write.Size = sizeof(CD_CONNECTION_INFORMATION);
    msg.complete.Write.Offset = 0;

    complete_io(server, msg.complete);
}

} // namespace corehost::condrv_io
