// ── conpty/io_loop.hpp ────────────────────────────────────
// ConDrv I/O 事件循环。
//
// 功能分解：
// 1. READ_IO 双缓冲：本轮 READ_IO 提交上一条 completion，同时读取下一条
//    消息，因此接收缓冲不能覆盖上一条消息的 completion 数据。
// 2. 空闲节流：没有 pending 和 completion 时先让 handler 处理 VT 输入，
//    再短暂等待 server；不能等待 InputAvailableEvent，因为它服务客户端输入。
// 3. 挂起请求：handler 返回 false 时进入 wait_for_pending_input，直到
//    bridge 用 VT 输入完成挂起的 ReadConsole/RawRead/GetConsoleInput。
//
// message_router 提供 loop 需要的完整会话操作：ConDrv 消息分派、VT idle
// 输入服务、pending 请求等待和退出条件判断。

#pragma once
#include "connect_completion.hpp"
#include "message_router.hpp"
#include "miniio/io_thread.hpp"
#include "perf_diag.hpp"
#include "utility/log.hpp"

namespace conpty
{

inline constexpr DWORD io_loop_idle_wait_ms = 16;

// 运行 ConDrv READ_IO/COMPLETE_IO 主循环。server/event 都是非拥有句柄；
// router 持有实际状态机。函数通过 READ_IO piggyback 提交同步 completion，
// 对 pending 请求等待 VT 输入显式完成，并在 server 断开或 bridge 可退出时返回。
inline void run_io_loop_no_setup(win32::handle_view server, win32::handle_view ev, message_router &router)
{
    // server 是 ConDrv \Server 等待/READ_IO 目标；ev 是客户端 input-available
    // 事件，只用于判断 completion 前是否需要先刷 VT 输出。router 持有
    // io_state/pipe_bridge/api_router，是本循环消费消息的唯一入口。
    LOG("run_io_loop_no_setup: enter");

    // READ_IO 会在同一次调用中提交上一条 completion 并读取下一条消息。
    // 双缓冲保证 completion 中指向的 body 不会被下一条消息覆盖。
    miniio::io_msg msgA{}, msgB{};

    // cur 只在 msgA/msgB 之间切换，指向本轮接收缓冲。另一块缓冲可能仍被
    // prev_done 指向，用作上一条消息的 completion 输入。
    miniio::io_msg *cur = &msgA;

    // nullptr 表示本轮不提交 completion；非空表示上一条消息已经处理完，
    // 其 completion 必须作为下一轮 READ_IO 的输入。
    miniio::io_msg *prev_done = nullptr;

    for (;;)
    {
        // 只有在没有 pending 请求、也没有 completion 待提交时才允许等待。
        // prev_done 非空时必须立即 READ_IO，把 completion 交回 ConDrv。
        if (!router.has_pending() && prev_done == nullptr)
        {
            // router.on_idle 会主动读取 vt_in；键盘输入不会唤醒 server，所以等待前
            // 必须先服务一次终端输入。
            router.on_idle();
            if (router.should_exit())
                break;
            if (!router.has_pending())
            {
                // 16ms 只在已经主动服务过一次 vt_in 后用于空闲节流；新的
                // ConDrv 消息会唤醒 server，终端输入由下一轮 on_idle 轮询。
                COREHOST_PERF_SCOPE(io_server_idle_wait);
                ::WaitForSingleObject(server.get(), io_loop_idle_wait_ms);
            }
        }

        // prev_comp 非空时由 ConDrv 消费；read_io_try 返回 no_message 后也不
        // 能再次提交同一个 completion。
        CD_IO_COMPLETE *prev_comp = prev_done ? &prev_done->complete : nullptr;
        const bool submitting_previous_completion = prev_comp != nullptr;
        LOG2_IF(submitting_previous_completion, "submitting completion id=%08lx:%08lx status=0x%08lx info=%llu",
                prev_done->descriptor.Identifier.HighPart, prev_done->descriptor.Identifier.LowPart,
                prev_done->complete.Status, prev_done->complete.Information);
        if (submitting_previous_completion && router.has_buffered_vt_output() && ev.valid())
        {
            COREHOST_PERF_SCOPE(io_server_wait_0);
            if (::WaitForSingleObject(ev.get(), 0) != WAIT_OBJECT_0)
                router.flush_vt_output();
        }

        // read_io_try 的三个结果含义：
        // disconnected: server 已断开，本循环结束。
        // no_message   : completion 已提交，但暂时没有新消息。
        // message      : cur 中已有一条新的 ConDrv 消息。
        auto read_result = miniio::read_io_result::no_message;
        {
            COREHOST_PERF_SCOPE(io_read_io_try);
            read_result = miniio::read_io_try(server, prev_comp, *cur);
        }
        if (read_result == miniio::read_io_result::disconnected)
        {
            router.flush_vt_output();
            LOG("run_io_loop_no_setup: read_io false, exiting");
            break;
        }
        if (read_result == miniio::read_io_result::no_message)
        {
            // no_message 仍可能已经消费 prev_comp，因此必须清空 prev_done。
            prev_done = nullptr;
            LOG2("READ_IO returned no message");

            router.flush_vt_output();

            // 0ms 只触发一次非阻塞等待，让 ConDrv 消化 pending 状态。
            {
                COREHOST_PERF_SCOPE(io_server_wait_0);
                ::WaitForSingleObject(server.get(), 0);
            }
            router.on_idle();
            if (router.should_exit())
                break;
            if (!router.has_pending())
            {
                // 16ms 只在 READ_IO 已确认暂时无消息后让步。
                COREHOST_PERF_SCOPE(io_server_idle_wait);
                ::WaitForSingleObject(server.get(), io_loop_idle_wait_ms);
            }
            continue;
        }
        prev_done = nullptr; // completion 已被 read_io 消费
        if (submitting_previous_completion && router.should_flush_vt_output())
            router.flush_vt_output();

        if (cur->descriptor.Function == 0)
        {
            // Function==0 是空 descriptor，不对应可完成的 Console I/O。
            LOG2("empty ConDrv descriptor");
            router.on_idle();
            if (router.should_exit())
                break;
            continue;
        }

        if (cur->descriptor.Function != CONSOLE_IO_CONNECT)
        {
            LOG2("dispatch message func=%lu id=%08lx:%08lx", cur->descriptor.Function,
                 cur->descriptor.Identifier.HighPart, cur->descriptor.Identifier.LowPart);
            // true 表示 router 已经填好 cur->complete，下一轮 READ_IO 提交。
            // false 表示请求挂起，router 会在后续 VT 输入到达时显式完成。
            if (router.on_message(*cur))
            {
                // 下一轮 READ_IO 会在读取下一条消息的同时提交本条 completion。
                // 即使 VT 输出暂存在 bridge 缓冲中，也不需要额外 COMPLETE_IO；
                // 当 READ_IO 已提交 completion 后再按阈值刷新 VT，可同时减少
                // ConDrv IOCTL 和终端管道 WriteFile 次数。
                prev_done = cur;
            }
            else
            {
                LOG2("message pending func=%lu", cur->descriptor.Function);
                // pending 期间只等待终端输入或关闭信号。继续等 server 会在
                // 没有终端输入时重复唤醒，造成空转。
                while (router.has_pending())
                {
                    router.wait_for_pending_input();
                }
                LOG2("pending message completed func=%lu", cur->descriptor.Function);
                if (router.should_exit())
                    break;
            }
            cur = (cur == &msgA) ? &msgB : &msgA;
            continue;
        }

        connect_completion connect_result = connect_completion::explicit_complete;
        LOG2("dispatch CONNECT id=%08lx:%08lx", cur->descriptor.Identifier.HighPart,
             cur->descriptor.Identifier.LowPart);
        if (!router.on_connect(*cur, connect_result))
        {
            router.flush_vt_output();
            return;
        }

        // inline_complete 说明 handler 只填了 cur->complete，仍需要下一轮 READ_IO
        // 把 completion 交给 ConDrv。
        if (connect_result == connect_completion::inline_complete)
        {
            prev_done = cur;
        }
        else
        {
            // 首个 CONNECT 可能由 miniio::accept_connection() 通过
            // IOCTL_COMPLETE_IO 显式完成。此时不能再把同一个 completion
            // 作为下一轮 READ_IO 的输入重复提交。
            prev_done = nullptr;
        }
        cur = (cur == &msgA) ? &msgB : &msgA;
    }

    router.flush_vt_output();
    LOG("run_io_loop_no_setup: exit");
}

} // namespace conpty
