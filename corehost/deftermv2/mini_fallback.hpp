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

struct mini_fallback_state
{
    bool break_when_input_waits = false;
    bool break_sent = false;
    DWORD target_process_group_id = 0;
};

[[nodiscard]] inline bool is_waiting_for_user_input(const miniio::io_msg &msg) noexcept
{
    if (msg.descriptor.Function == CONSOLE_IO_RAW_READ)
    {
        LOG("deftermv2::is_waiting_for_user_input: RAW_READ");
        return true;
    }
    if (msg.descriptor.Function != CONSOLE_IO_USER_DEFINED ||
        msg.descriptor.InputSize < sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_READCONSOLE_MSG))
        return false;

    auto *header = reinterpret_cast<const CONSOLE_MSG_HEADER *>(msg.body);
    LOG("deftermv2::is_waiting_for_user_input: USER_DEFINED api=0x%08lx", header->ApiNumber);
    return header->ApiNumber == ConsolepReadConsole;
}

inline void send_deferred_ctrl_break_if_needed(const miniio::io_msg &msg, mini_fallback_state &fallback)
{
    if (!fallback.break_when_input_waits || fallback.break_sent || !is_waiting_for_user_input(msg))
        return;

    LOG("deftermv2::fallback: sending deferred CTRL_BREAK to pgid=%lu", fallback.target_process_group_id);
    ::GenerateConsoleCtrlEvent(CTRL_BREAK_EVENT, fallback.target_process_group_id);
    fallback.break_sent = true;
}

inline void dispatch_mini_fallback_message(win32::handle_view server, miniio::io_msg &msg, win32::handle &input,
                                           win32::handle &output)
{
    LOG("deftermv2::dispatch_mini_fallback_message: func=%lu id=%08lx:%08lx pid=%llu object=%llu input=%p "
        "output=%p",
        msg.descriptor.Function, msg.descriptor.Identifier.HighPart, msg.descriptor.Identifier.LowPart,
        static_cast<unsigned long long>(msg.descriptor.Process), static_cast<unsigned long long>(msg.descriptor.Object),
        input.get(), output.get());

    switch (msg.descriptor.Function)
    {
    case 0:
    case CONSOLE_IO_CONNECT:
        break;
    case CONSOLE_IO_DISCONNECT:
        input.clear();
        output.clear();
        miniio::prepare_completion(msg);
        break;
    case CONSOLE_IO_CREATE_OBJECT: {
        auto *request = reinterpret_cast<CD_CREATE_OBJECT_INFORMATION *>(msg.body);
        auto object_type = request->ObjectType;
        if (object_type == CD_IO_OBJECT_TYPE_GENERIC)
        {
            if ((request->DesiredAccess & (GENERIC_READ | GENERIC_WRITE)) == GENERIC_READ)
                object_type = CD_IO_OBJECT_TYPE_CURRENT_INPUT;
            else if ((request->DesiredAccess & (GENERIC_READ | GENERIC_WRITE)) == GENERIC_WRITE)
                object_type = CD_IO_OBJECT_TYPE_CURRENT_OUTPUT;
        }

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
            miniio::prepare_completion(msg, 0xC0000001);
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
        miniio::prepare_completion(msg, 0xC0000001);
        break;
    case CONSOLE_IO_RAW_FLUSH:
        miniio::prepare_completion(msg);
        break;
    default:
        std::unreachable();
    }
}

} // namespace deftermv2
