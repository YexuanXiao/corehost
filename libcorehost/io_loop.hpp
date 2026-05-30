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
// Handler 需提供:
//   bool on_connect(miniio::io_msg &msg, connect_completion &completion)
//                                           — CONNECT 处理，并说明 completion
//                                             是否已由 CompleteIo 显式提交
//   bool on_message(miniio::io_msg &msg)  — 非 CONNECT 消息
//   void on_idle()                         — 空闲时调用
//   bool has_pending() const               — 检查是否有挂起 I/O
//   void wait_for_pending_input()          — 等待/服务终端输入直到 pending 可能完成
//   bool should_exit() const               — I/O 循环退出条件

#pragma once
#include "connect_completion.hpp"
#include "miniio/io_thread.hpp"
#include "utility/log.hpp"

namespace conpty
{

template <typename Handler>
inline void run_io_loop_no_setup(win32::handle_view server, win32::handle_view ev, Handler &handler)
{
    LOG("run_io_loop_no_setup: enter");

    // server 是 READ_IO/COMPLETE_IO 的等待对象。ev 是 ConDrv InputAvailableEvent，
    // 本循环不等待 ev：ev 只通知客户端输入可用，不能作为服务端消息节流信号。
    (void)ev;

    // READ_IO 会在同一次调用中提交上一条 completion 并读取下一条消息。
    // 双缓冲保证 completion 中指向的 body 不会被下一条消息覆盖。
    miniio::io_msg msgA{}, msgB{};

    // cur 只在 msgA/msgB 之间切换，指向本轮接收缓冲。
    miniio::io_msg *cur = &msgA;

    // nullptr 表示本轮不提交 completion；非空表示上一条消息已经处理完，
    // 其 completion 必须作为下一轮 READ_IO 的输入。
    miniio::io_msg *prev_done = nullptr;

    for (;;)
    {
        // 只有在没有 pending 请求、也没有 completion 待提交时才允许等待。
        // prev_done 非空时必须立即 READ_IO，把 completion 交回 ConDrv。
        if (!handler.has_pending() && prev_done == nullptr)
        {
            // on_idle 会主动读取 vt_in；键盘输入不会唤醒 server，所以等待前
            // 必须先服务一次终端输入。
            handler.on_idle();
            if (handler.should_exit())
                break;
            if (!handler.has_pending())
                // 1ms 只是空闲节流；不能无限等待，因为 vt_in 不会唤醒 server。
                ::WaitForSingleObject(server.get(), 1);
        }

        // prev_comp 非空时由 ConDrv 消费；read_io_try 返回 no_message 后也不
        // 能再次提交同一个 completion。
        CD_IO_COMPLETE *prev_comp = prev_done ? &prev_done->complete : nullptr;

        // read_io_try 的三个结果含义：
        // disconnected: server 已断开，本循环结束。
        // no_message   : completion 已提交，但暂时没有新消息。
        // message      : cur 中已有一条新的 ConDrv 消息。
        auto read_result = miniio::read_io_try(server, prev_comp, *cur);
        if (read_result == miniio::read_io_result::disconnected)
        {
            LOG("run_io_loop_no_setup: read_io false, exiting");
            break;
        }
        if (read_result == miniio::read_io_result::no_message)
        {
            // no_message 仍可能已经消费 prev_comp，因此必须清空 prev_done。
            prev_done = nullptr;

            // 0ms 只触发一次非阻塞等待，让 ConDrv 消化 pending 状态。
            ::WaitForSingleObject(server.get(), 0);
            handler.on_idle();
            if (handler.should_exit())
                break;
            if (!handler.has_pending())
                // 1ms 只是空闲节流；pending VT 输入仍由 on_idle/handler 处理。
                ::WaitForSingleObject(server.get(), 1);
            continue;
        }
        prev_done = nullptr; // completion 已被 read_io 消费

        if (cur->descriptor.Function == 0)
        {
            // Function==0 是空 descriptor，不对应可完成的 Console I/O。
            handler.on_idle();
            if (handler.should_exit())
                break;
            continue;
        }

        if (cur->descriptor.Function != CONSOLE_IO_CONNECT)
        {
            // true 表示 handler 已经填好 cur->complete，下一轮 READ_IO 提交。
            // false 表示请求挂起，handler 会在后续 VT 输入到达时显式完成。
            if (handler.on_message(*cur))
            {
                prev_done = cur;
                handler.on_idle();
                if (handler.should_exit())
                    break;
            }
            else
            {
                // pending 期间只等待终端输入或关闭信号。继续等 server 会在
                // 没有终端输入时重复唤醒，造成空转。
                while (handler.has_pending())
                {
                    handler.wait_for_pending_input();
                }
                if (handler.should_exit())
                    break;
            }
            cur = (cur == &msgA) ? &msgB : &msgA;
            continue;
        }

        connect_completion connect_result = connect_completion::explicit_complete;
        if (!handler.on_connect(*cur, connect_result))
            return;

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

    LOG("run_io_loop_no_setup: exit");
}

} // namespace conpty
