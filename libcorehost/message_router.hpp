// ── conpty/message_router.hpp ─────────────────────
// Layer 1: ConDrv 消息路由器。
//
// 功能分解：
// 1. CONNECT/DISCONNECT/CREATE/CLOSE/FLUSH 交给 io_state 维护 ConDrv 状态。
// 2. RAW_READ/RAW_WRITE 交给 pipe_bridge 处理 VT 管道。
// 3. USER_DEFINED 交给 api_router 分派到具体 Console API handler。
#pragma once
#include <windows.h>
#include <cstring>
#include "miniio/io_thread.hpp"
#include "io_state.hpp"
#include "pipe_bridge.hpp"
#include "api_router.hpp"
#include "utility/log.hpp"

namespace conpty
{

struct message_router
{
    io_state &io;
    pipe_bridge &bridge;
    api_router &api;

    bool on_connect(miniio::io_msg &msg, connect_completion &completion)
    {
        // CONNECT 走单独入口，因为 I/O loop 需要知道 completion 是显式完成
        // 还是要在下一轮 READ_IO 中提交。
        auto ok = io.handle_connect(msg, completion);
        sync_process_snapshot();
        return ok;
    }

    bool on_message(miniio::io_msg &msg)
    {
        // 非 CONNECT 消息不需要额外 completion 分类，dispatch 的 bool 直接
        // 告诉 I/O loop 是否可以下一轮提交 completion。
        return dispatch(msg);
    }

    void on_idle()
    {
        // idle 阶段由 bridge 主动抽取 vt_in，处理终端输入和延迟完成。
        bridge.on_idle();
    }

    bool has_pending() const
    {
        // pending 状态只存在于 bridge：RawRead/ReadConsole/GetConsoleInput。
        return bridge.has_pending();
    }

    void wait_for_pending_input()
    {
        // pending 请求只能靠 VT 输入或终端关闭推进，不能靠 ConDrv 消息推进。
        bridge.wait_for_pending_vt_input();
    }

    bool should_exit() const
    {
        // vt_in EOF 且没有 pending 请求时，bridge 认为会话可以退出。
        return bridge.should_exit();
    }

  private:
    bool dispatch(miniio::io_msg &msg)
    {
        // descriptor.Function 只使用 ConDrv 定义的 CONSOLE_IO_* 值。
        // 未识别值按成功完成，避免未知/废弃请求阻塞客户端。
        switch (msg.descriptor.Function)
        {
        case CONSOLE_IO_CONNECT: {
            // 理论上 CONNECT 已由 on_connect 处理；这里保留分支给直接 dispatch
            // 的调用路径，仍然同步进程列表快照。
            connect_completion completion = connect_completion::explicit_complete;
            bool ok = io.handle_connect(msg, completion);
            sync_process_snapshot();
            return ok;
        }
        case CONSOLE_IO_DISCONNECT: {
            bool ok = io.handle_disconnect(msg);
            sync_process_snapshot();
            return ok;
        }
        case CONSOLE_IO_CREATE_OBJECT:
            return io.handle_create_object(msg);
        case CONSOLE_IO_CLOSE_OBJECT:
            return io.handle_close_object(msg);
        case CONSOLE_IO_RAW_WRITE:
            // 对齐原版 IoSorter: RAW_WRITE 按 WriteConsoleA 处理，再转成 VT 输出。
            return api_raw_write_console(msg, api.state, api.active_screen_buffer(), api.inp, bridge);
        case CONSOLE_IO_RAW_READ:
            // 客户端请求原始输入。若当前没有足够 VT 输入，bridge 会挂起请求。
            return bridge.handle_raw_read(msg);
        case CONSOLE_IO_USER_DEFINED:
            // Console API 消息体以 CONSOLE_MSG_HEADER 开头，由 api_router 继续拆层。
            return api.handle_user_defined(msg);
        case CONSOLE_IO_RAW_FLUSH:
            // 原版 RAW_FLUSH 复用 ServerFlushConsoleInputBuffer；这里没有
            // USER_DEFINED header，因此只清空事件队列并返回普通 IO completion。
            api.inp.flush();
            io.handle_raw_flush(msg);
            return true;
        default:
            // 未实现的 Function 不能阻塞客户端；按空成功完成。
            miniio::prepare_completion(msg);
            return true;
        }
    }

    void sync_process_snapshot() noexcept
    {
        // bridge 持有一份快照，供 GetConsoleProcessList 的 API handler 读取。
        bridge.proc_count = io.process_count;
        std::memcpy(bridge.proc_list, io.process_list, io.process_count * sizeof(DWORD));
    }
};

} // namespace conpty
