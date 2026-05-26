// ── conpty/message_router.hpp ─────────────────────
// Layer 1: ConDrv 消息路由器 (char32_t 版本)
//
// 与 conpty/message_router.hpp 相同 — 纯调度, 无文本编码。
#pragma once
#include <windows.h>
#include "miniio/io_thread.hpp"
#include "io_state.hpp"
#include "pipe_bridge.hpp"
#include "api_router.hpp"
#include "utility/log.hpp"

namespace conpty
{

struct message_router
{
    io_state *io = nullptr;
    pipe_bridge *bridge = nullptr;
    api_router *api = nullptr;

    bool on_connect(miniio::io_msg &msg)
    {
        return io ? io->handle_connect(msg) : true;
    }

    bool on_message(miniio::io_msg &msg)
    {
        if (!io || !bridge || !api)
        {
            miniio::prepare_completion(msg);
            return true;
        }
        return dispatch(msg);
    }

    void on_idle()
    {
        if (bridge)
            bridge->on_idle();
    }

    bool has_pending() const
    {
        return bridge ? bridge->has_pending() : false;
    }

    bool should_exit() const
    {
        return bridge ? bridge->should_exit() : false;
    }

  private:
    bool dispatch(miniio::io_msg &msg)
    {
        switch (msg.descriptor.Function)
        {
        case CONSOLE_IO_CONNECT: {
            bool ok = io->handle_connect(msg);
            if (bridge && io)
            {
                bridge->proc_count = io->process_count;
                for (size_t i = 0; i < io->process_count; ++i)
                    bridge->proc_list[i] = io->process_list[i];
            }
            return ok;
        }
        case CONSOLE_IO_DISCONNECT:
            return io->handle_disconnect(msg);
        case CONSOLE_IO_CREATE_OBJECT:
            return io->handle_create_object(msg);
        case CONSOLE_IO_CLOSE_OBJECT:
            return io->handle_close_object(msg);
        case CONSOLE_IO_RAW_WRITE:
            return bridge->handle_raw_write(msg);
        case CONSOLE_IO_RAW_READ:
            return bridge->handle_raw_read(msg);
        case CONSOLE_IO_USER_DEFINED:
            return api->handle_user_defined(msg);
        case CONSOLE_IO_RAW_FLUSH:
            io->handle_raw_flush(msg);
            return true;
        default:
            miniio::prepare_completion(msg);
            return true;
        }
    }
};

} // namespace conpty
