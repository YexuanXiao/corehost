#pragma once

#include <windows.h>
#include <utility>
#include "miniio/io_thread.hpp"
#include "ntapi/condrv.hpp"
#include "os/Console/conmsgl1.h"
#include "os/Console/condrv.h"
#include "utility/log.hpp"
#include "win32/handle.hpp"

namespace deftermv2
{

// NTSTATUS: STATUS_UNSUCCESSFUL。mini fallback 不实现完整 Console API，
// 因此对无法安全模拟的请求返回通用失败，而不是假装成功。
inline constexpr LONG ntstatus_unsuccessful = static_cast<LONG>(0xC0000001);

// ConDrv 空 descriptor 的 Function 值。它不代表一个可完成的 Console IO，
// 只表示当前读取结果没有业务消息需要处理。
inline constexpr ULONG no_console_io_function = 0;

struct mini_fallback_state
{
    // true 表示已经通知用户失败，但要等客户端真正进入输入等待后再发
    // CTRL_BREAK，避免打断进程初始化阶段的错误输出。
    bool break_when_input_waits = false;

    // true 表示 CTRL_BREAK 已经发送过。控制事件只发送一次，避免重复打断
    // 同一进程组。
    bool break_sent = false;

    // 0 表示没有可用目标；非 0 是 GenerateConsoleCtrlEvent 的进程组 id。
    // UAC fallback 优先使用 CONSOLE_SERVER_MSG::ProcessGroupId，缺失时用 pid。
    DWORD target_process_group_id = 0;
};

[[nodiscard]] inline bool is_waiting_for_user_input(const miniio::io_msg &msg) noexcept
{
    if (msg.descriptor.Function == CONSOLE_IO_RAW_READ)
    {
        LOG3("input wait detected: RAW_READ");
        return true;
    }
    if (msg.descriptor.Function != CONSOLE_IO_USER_DEFINED ||
        msg.descriptor.InputSize < sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_READCONSOLE_MSG))
        return false;

    auto *header = reinterpret_cast<const CONSOLE_MSG_HEADER *>(msg.body);
    LOG3("checking USER_DEFINED input wait api=0x%08lx", header->ApiNumber);
    return header->ApiNumber == ConsolepReadConsole;
}

inline void send_deferred_ctrl_break_if_needed(const miniio::io_msg &msg, mini_fallback_state &fallback)
{
    if (!fallback.break_when_input_waits || fallback.break_sent || !is_waiting_for_user_input(msg))
        return;

    LOG("deferred CTRL_BREAK expected now; target process group=%lu", fallback.target_process_group_id);
    ::GenerateConsoleCtrlEvent(CTRL_BREAK_EVENT, fallback.target_process_group_id);
    fallback.break_sent = true;
}

inline void dispatch_mini_fallback_message(win32::handle_view server, miniio::io_msg &msg, win32::handle &input,
                                           win32::handle &output)
{
    LOG3("mini fallback dispatch: func=%lu id=%08lx:%08lx pid=%llu object=%llu input=%p output=%p",
         msg.descriptor.Function, msg.descriptor.Identifier.HighPart, msg.descriptor.Identifier.LowPart,
         static_cast<unsigned long long>(msg.descriptor.Process),
         static_cast<unsigned long long>(msg.descriptor.Object), input.get(), output.get());

    switch (msg.descriptor.Function)
    {
    case no_console_io_function:
    case CONSOLE_IO_CONNECT:
        break;
    case CONSOLE_IO_DISCONNECT:
        input.clear();
        output.clear();
        miniio::prepare_completion(msg);
        break;
    case CONSOLE_IO_CREATE_OBJECT: {
        // request 指向 ConDrv 消息体中的 CD_CREATE_OBJECT_INFORMATION。
        // msg.descriptor.InputSize 必须由驱动保证足够大；这里沿用原始
        // conhost 的信任边界，不重复做短包校验。
        auto *request = reinterpret_cast<CD_CREATE_OBJECT_INFORMATION *>(msg.body);

        // ObjectType 可能是明确的 CurrentInput/CurrentOutput/NewOutput，
        // 也可能是 GENERIC。GENERIC 需按 DesiredAccess 推导真实对象。
        auto object_type = request->ObjectType;
        if (object_type == CD_IO_OBJECT_TYPE_GENERIC)
        {
            if ((request->DesiredAccess & (GENERIC_READ | GENERIC_WRITE)) == GENERIC_READ)
                object_type = CD_IO_OBJECT_TYPE_CURRENT_INPUT;
            else if ((request->DesiredAccess & (GENERIC_READ | GENERIC_WRITE)) == GENERIC_WRITE)
                object_type = CD_IO_OBJECT_TYPE_CURRENT_OUTPUT;
        }

        // new_handle 只有在对象类型被支持时有效；release 后所有权交给
        // ConDrv completion，当前进程不再关闭它。
        win32::handle new_handle;
        switch (object_type)
        {
        case CD_IO_OBJECT_TYPE_CURRENT_INPUT:
            new_handle = condrv::create_client_handle(server, L"\\Input");
            break;
        case CD_IO_OBJECT_TYPE_CURRENT_OUTPUT:
        case CD_IO_OBJECT_TYPE_NEW_OUTPUT:
            new_handle = condrv::create_client_handle(server, L"\\Output");
            break;
        default:
            miniio::prepare_completion(msg, ntstatus_unsuccessful);
            return;
        }
        miniio::prepare_completion(msg, 0, reinterpret_cast<ULONG_PTR>(new_handle.release()));
        break;
    }
    case CONSOLE_IO_CLOSE_OBJECT:
        miniio::prepare_completion(msg);
        break;
    case CONSOLE_IO_RAW_WRITE:
        miniio::prepare_completion(msg, 0, msg.descriptor.InputSize);
        break;
    case CONSOLE_IO_RAW_READ:
        miniio::prepare_completion(msg);
        break;
    case CONSOLE_IO_USER_DEFINED:
        miniio::prepare_completion(msg, ntstatus_unsuccessful);
        break;
    case CONSOLE_IO_RAW_FLUSH:
        miniio::prepare_completion(msg);
        break;
    default:
        std::unreachable();
    }
}

} // namespace deftermv2
