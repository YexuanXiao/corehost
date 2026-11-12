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
//     没有消息时阻塞；被 CancelIoEx 取消时返回 ERROR_OPERATION_ABORTED，
//     调用方应先对 server 做一次非阻塞等待后再试。
//     ERROR_PIPE_NOT_CONNECTED / BROKEN_PIPE / NO_DATA
//     表示客户端全部断开，应退出循环。
//   IOCTL_COMPLETE_IO (2, METHOD_NEITHER)
//     — 提交完成结果。CONNECT 消息的完成包含 CD_CONNECTION_INFO。
//   IOCTL_SET_SERVER (7, METHOD_NEITHER)
//     — 注册 InputAvailableEvent。驱动在消息就绪时 SetEvent，
//       使 server 的非阻塞等待可唤醒。
//
// 异步完成模型：默认不立即 complete_io——填充 msg.complete，在下一轮
//   read_io 作为 lpInBuffer 提交，比每次调用 complete_io 少一次 IOCTL 往返。
//   CONNECT 也走同一模型；只有无法 piggyback 的调用方（循环马上退出、或
//   io_msg 是临时对象）才显式 complete_io。

#pragma once
#include <windows.h>
#include <winioctl.h>
#include <winternl.h>
#include <cstdint>
#include <cstring>
#include <span>
#include "win32/handle.hpp"
#include "win32/error.hpp"
#include "win32/io.hpp"
#include "win32/string.hpp"
#include "os/Console/condrv.h"
#include "ntapi/condrv.hpp"
#include "utility/log.hpp"

namespace corehost::condrv_io
{

namespace error
{
inline constexpr LONG status_invalid_parameter = 0xC000000D;
inline constexpr LONG status_invalid_handle = 0xC0000008;
inline constexpr LONG status_buffer_too_small = 0xC0000023;
inline constexpr LONG status_illegal_function = 0xC00000AF;
inline constexpr LONG status_not_implemented = 0xC0000002;
inline constexpr LONG status_not_supported = 0xC00000BB;
// Ctrl+C / Ctrl+Break 结束挂起读时返回的状态；
// 读路径共用这一状态。
inline constexpr LONG status_alerted = 0x00000101;

} // namespace error

using namespace error;

inline constexpr DWORD IOCTL_READ_IO = CTL_CODE(FILE_DEVICE_CONSOLE, 1, METHOD_OUT_DIRECT, FILE_ANY_ACCESS);
inline constexpr DWORD IOCTL_COMPLETE_IO = CTL_CODE(FILE_DEVICE_CONSOLE, 2, METHOD_NEITHER, FILE_ANY_ACCESS);
inline constexpr DWORD IOCTL_READ_INPUT = CTL_CODE(FILE_DEVICE_CONSOLE, 3, METHOD_NEITHER, FILE_ANY_ACCESS);
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
    // READ_IO 在没有消息时会阻塞；关闭会话时窗口线程用 CancelSynchronousIo
    // 打断它。ERROR_OPERATION_ABORTED 表示"本次等待被取消"，等价于本轮没有
    // 消息，调用方会回到事件等待并重新评估会话状态（见 design/0001 4.10）。
    if (err == win32::error::operation_aborted)
        return read_io_result::no_message;
    if (err == win32::error::pipe_not_connected || err == win32::error::broken_pipe || err == win32::error::no_data)
        return read_io_result::disconnected;
    LOG("read_io_try: unexpected error %u", static_cast<unsigned>(err));
    throw err;
}
// ── read_exact ────────────────────────────────────────────
// 从管道读取精确字节数。区别于 ReadFile 的"尽量读"语义，
// 这里要求恰好 s 字节，否则返回 false（管道断开或数据不足）。
// 供 libcorehost 的 pty_signal_reader 与 defterm 的 console_control_forwarder 轮询路径共用。
inline bool read_exact(win32::handle_view p, void *b, DWORD s) noexcept
{
    return win32::read_exact(p, std::span{static_cast<std::byte *>(b), s});
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
// 覆盖 msg.complete 的全部字段：
//   - Identifier 从 msg.descriptor.Identifier 复制
//   - IoStatus.Status = status（0=成功，负值=NTSTATUS 错误）
//   - IoStatus.Information = info（完成的字节数或句柄值）
//   - Write.Data/Size = data/size（返回给客户端的输出载荷）
//   - Write.Offset = 0（ConDrv 的 Write 偏移恒为 0，不作为参数）
//
// Information 与 Write.Size 是两个独立语义，不能合并成一个参数：Write 为空
// 但 Information 非零的场景确实存在（CREATE_OBJECT 返回句柄值、RAW_WRITE
// 返回写入字节数、EOF 空读返回 0）。
//
// 生命周期规则：io_msg.complete 通常在下一轮 READ_IO 才交给驱动，因此
// Write.Data 必须指向 msg.body 或其他长生命周期存储；指向栈变量只在
// 同步 complete_io() 期间安全（驱动在 IOCTL 返回前已取走载荷）。
inline void prepare_completion(io_msg &msg, LONG status, ULONG_PTR info, PVOID data, ULONG size) noexcept
{
    msg.complete.Identifier = msg.descriptor.Identifier;
    msg.complete.IoStatus.Status = status;
    msg.complete.IoStatus.Information = info;
    msg.complete.Write.Data = data;
    msg.complete.Write.Size = size;
    msg.complete.Write.Offset = 0;
}

// 无输出载荷的完成：Write 清零，失败分支据此不会回写客户端缓冲区，
// 与 conhost 只在 SUCCEEDED_NTSTATUS 时 WriteOutput 的行为一致。
inline void prepare_completion(io_msg &msg, LONG status = 0, ULONG_PTR info = 0) noexcept
{
    prepare_completion(msg, status, info, nullptr, 0);
}

// ── completion 自检 ────────────────────────────────────────
// prepare_completion 会把 Identifier 同步为 descriptor.Identifier。若某个
// handler 返回"已完成"却没有调用它，msg.complete 会残留同一缓冲上一轮的
// 标识符（或为全零）。提交前断言这一点，可以把"忘记完成"从静默提交旧结果
// 变成可检测错误。
[[nodiscard]] inline bool completion_matches_descriptor(const io_msg &msg) noexcept
{
    return std::bit_cast<ULONGLONG>(msg.complete.Identifier) == std::bit_cast<ULONGLONG>(msg.descriptor.Identifier);
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
//
// 提交时机由调用方决定：本函数只填 completion，不调用 complete_io。
//   - 循环还能继续：留给下一轮 READ_IO piggyback，省掉一次 COMPLETE_IO；
//   - 循环马上退出、或 io_msg 是临时对象：必须立即 complete_io，否则
//     载荷随 msg 销毁，客户端永远收不到连接结果。
//
// 注意：回复会覆盖 msg.body（CONNECT 的请求体到这里已解析完毕），调用方
// 在本函数之后不得再引用之前从 msg.body 取得的指针或引用。

// 把 \Input/\Output 句柄写成 CD_CONNECTION_INFORMATION，放进 msg.body 并
// 填充 msg.complete。放 msg.body 而不是栈变量，是因为 completion 可能由
// 下一轮 READ_IO 携带：message_router 的双缓冲保证该缓冲在提交前不被覆盖。
inline void prepare_connection_completion(io_msg &msg, win32::handle_view input, win32::handle_view output,
                                         ULONG_PTR process_token) noexcept
{
    CD_CONNECTION_INFORMATION conn{};
    // ConDrv 会把 Process 原样带回后续消息（含 DISCONNECT）；必须用客户端 pid，
    // 否则 DISCONNECT 指不到进程，进程列表只增不减（fork 42ef837）。
    conn.Process = process_token;
    conn.Input = reinterpret_cast<ULONG_PTR>(input.get());
    conn.Output = reinterpret_cast<ULONG_PTR>(output.get());
    static_assert(sizeof(conn) <= sizeof(msg.body));
    // memcpy 而不是直接 cast 写，避免对 msg.body 做对齐假设。
    std::memcpy(msg.body, &conn, sizeof(conn));
    prepare_completion(msg, 0, sizeof(conn), msg.body, sizeof(conn));
}

inline void accept_connection(win32::handle_view server, io_msg &msg, win32::handle &input, win32::handle &output)
{
    input = condrv::create_client_handle(server, L"\\Input");
    output = condrv::create_client_handle(server, L"\\Output");
    prepare_connection_completion(msg, input.view(), output.view(), msg.descriptor.Process);
}

} // namespace corehost::condrv_io