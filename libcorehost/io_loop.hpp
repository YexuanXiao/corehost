// ── conpty/io_loop.hpp ────────────────────────────────────
// ConDrv I/O 事件循环 — conpty 专用 (不调用 set_server_info)
//
// 同 defterm::run_io_loop 但不调用 set_server_info，
// 因为 cli/main.cpp 已在进入 conpty_entry 之前通过
// miniio::set_server_info 注册了 InputAvailableEvent。
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
    miniio::io_msg msgA{}, msgB{};
    miniio::io_msg *cur = &msgA;
    miniio::io_msg *prev_done = nullptr; // 上一条已完成消息，其 completion 需在下一轮 read_io 中提交

    for (;;)
    {
        // ══ rate-limit: 无 pending 且无 completion 待提交时，先服务 VT 输入再短等待 ══
        // vt_in 键盘管道可读不会触发 ConDrv ev；如果先睡 16ms，打字只能按 16ms
        // 轮询粒度被发现，并会被 PowerShell/PSReadLine 的多请求路径继续放大。
        // 但如果 prev_done != nullptr，completion 必须零延迟提交。
        if (!handler.has_pending() && prev_done == nullptr)
        {
            handler.on_idle();
            if (handler.should_exit())
                break;
            if (!handler.has_pending())
                ::WaitForSingleObject(server.get(), 1);
        }

        // ── 对标原始 ReadIo(ReplyMsg, &ReceiveMsg):
        //     将上一条已完成消息的 completion 作为 lpInBuffer 传入，
        //     ConDrv 由此确认消息已处理，才会转发同一客户端的下一条消息。
        CD_IO_COMPLETE *prev_comp = prev_done ? &prev_done->complete : nullptr;
        auto read_result = miniio::read_io_try(server, prev_comp, *cur);
        if (read_result == miniio::read_io_result::disconnected)
        {
            LOG("run_io_loop_no_setup: read_io false, exiting");
            break;
        }
        if (read_result == miniio::read_io_result::no_message)
        {
            prev_done = nullptr;
            // 对标原版 ConDrvDeviceComm::ReadIo: ERROR_IO_PENDING 后先
            // wait server handle 一次，让 ConDrv 消化这次无消息状态。
            // ev 是传给 ConDrv 的 InputAvailableEvent，用于唤醒客户端
            // 读输入；PowerShell/PSReadLine 路径可能让它保持 signaled，
            // 因此不能用它给服务端消息循环节流。
            ::WaitForSingleObject(server.get(), 0);
            handler.on_idle();
            if (handler.should_exit())
                break;
            if (!handler.has_pending())
                ::WaitForSingleObject(server.get(), 1);
            continue;
        }
        prev_done = nullptr; // completion 已被 read_io 消费

        if (cur->descriptor.Function == 0)
        {
            handler.on_idle();
            if (handler.should_exit())
                break;
            continue;
        }

        if (cur->descriptor.Function != CONSOLE_IO_CONNECT)
        {
            if (handler.on_message(*cur))
            {
                // 立即完成 → 标记 completion，下一轮 read_io 提交
                prev_done = cur;
                handler.on_idle();
                if (handler.should_exit())
                    break;
            }
            else
            {
                // 挂起: 阻塞等待 VT 输入，再由 bridge 完成挂起请求。
                // 不能在这里等 server 句柄；挂起期间 ConDrv 可能保持 server 可等待，
                // 用它节流会造成 corehost 在没有终端输入时高频空转。
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

        if (connect_result == connect_completion::inline_complete)
        {
            prev_done = cur; // 后续 CONNECT completion → 下一轮 read_io 提交
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
