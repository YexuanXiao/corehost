// ── conpty/io_state.hpp ──────────────────────────
// Layer 2: I/O 句柄管理
//
// 与 conpty/io_state.hpp 相同 — 无文本编码依赖。
#pragma once
#include <windows.h>
#include "connect_completion.hpp"
#include "win32/handle.hpp"
#include "miniio/io_thread.hpp"
#include "ntapi/condrv.hpp"

namespace conpty
{

struct io_state
{
    win32::handle_view server;
    win32::handle condrv_input;
    win32::handle condrv_output;

    ULONG_PTR input_id = 0;
    ULONG_PTR output_id = 0;

    void set_server(win32::handle_view s) noexcept
    {
        server = s;
    }

    ULONG_PTR client_pid = 0;

    static constexpr size_t max_processes = 64;
    DWORD process_list[max_processes]{};
    size_t process_count = 0;

    void add_process(DWORD pid)
    {
        for (size_t i = 0; i < process_count; ++i)
            if (process_list[i] == pid)
                return;
        if (process_count < max_processes)
            process_list[process_count++] = pid;
    }

    void remove_process(DWORD pid)
    {
        for (size_t i = 0; i < process_count; ++i)
        {
            if (process_list[i] == pid)
            {
                process_list[i] = process_list[--process_count];
                return;
            }
        }
    }

    bool handle_connect(miniio::io_msg &msg, connect_completion &completion)
    {
        client_pid = msg.descriptor.Process;
        add_process(static_cast<DWORD>(client_pid));
        if (!condrv_input.valid())
        {
            // ── 首个连接：创建客户端句柄对 + CD_CONNECTION_INFORMATION ──
            miniio::accept_connection(server, msg, condrv_input, condrv_output);
            completion = connect_completion::explicit_complete;
        }
        else
        {
            // ── 后续连接 (子进程接入)：仅标记成功，不重复创建句柄 ──
            miniio::prepare_completion(msg);
            completion = connect_completion::inline_complete;
        }
        return true;
    }

    bool handle_disconnect(miniio::io_msg &msg)
    {
        // ── 对标原始 ConsoleClientDisconnectRoutine:
        //     只从进程列表中移除，不关闭 I/O 句柄。
        //     父进程和子进程共享同一组 ConDrv 客户端句柄，
        //     关闭它们会导致父进程连接断裂 → read_io 返回 false → corehost 退出。
        auto pid = static_cast<DWORD>(msg.descriptor.Process);
        remove_process(pid);
        miniio::prepare_completion(msg);
        return true;
    }

    bool handle_create_object(miniio::io_msg &msg)
    {
        auto *req = reinterpret_cast<CD_CREATE_OBJECT_INFORMATION *>(msg.body);
        auto type = req->ObjectType;
        if (type == CD_IO_OBJECT_TYPE_GENERIC)
        {
            if ((req->DesiredAccess & (GENERIC_READ | GENERIC_WRITE)) == GENERIC_READ)
                type = CD_IO_OBJECT_TYPE_CURRENT_INPUT;
            else if ((req->DesiredAccess & (GENERIC_READ | GENERIC_WRITE)) == GENERIC_WRITE)
                type = CD_IO_OBJECT_TYPE_CURRENT_OUTPUT;
        }
        win32::handle nh;
        switch (type)
        {
        case CD_IO_OBJECT_TYPE_CURRENT_INPUT:
            nh = condrv::create_client_handle(server, L"\\Input");
            input_id = reinterpret_cast<ULONG_PTR>(nh.get());
            break;
        case CD_IO_OBJECT_TYPE_CURRENT_OUTPUT:
        case CD_IO_OBJECT_TYPE_NEW_OUTPUT:
            nh = condrv::create_client_handle(server, L"\\Output");
            output_id = reinterpret_cast<ULONG_PTR>(nh.get());
            break;
        default:
            miniio::prepare_completion(msg, 0xC0000001);
            return true;
        }
        miniio::prepare_completion(msg, 0, reinterpret_cast<ULONG_PTR>(nh.release()));
        return true;
    }

    bool handle_close_object(miniio::io_msg &msg)
    {
        auto id = msg.descriptor.Object;
        if (id == input_id)
            input_id = 0;
        if (id == output_id)
            output_id = 0;
        miniio::prepare_completion(msg);
        return true;
    }

    void handle_raw_flush(miniio::io_msg &msg)
    {
        miniio::prepare_completion(msg);
    }
};

} // namespace conpty
