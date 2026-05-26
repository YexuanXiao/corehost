// ── conpty/io_loop.hpp ────────────────────────────────────
// ConDrv I/O 事件循环 — conpty 专用 (不调用 set_server_info)
//
// 同 defterm::run_io_loop 但不调用 set_server_info，
// 因为 cli/main.cpp 已在进入 conpty_entry 之前通过
// miniio::set_server_info 注册了 InputAvailableEvent。
//
// Handler 需提供:
//   bool on_connect(miniio::io_msg &msg)   — CONNECT 处理
//   bool on_message(miniio::io_msg &msg)  — 非 CONNECT 消息
//   void on_idle()                         — 空闲时调用
//   bool has_pending() const               — 检查是否有挂起 I/O
//   bool should_exit() const               — I/O 循环退出条件

#pragma once
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
                ::WaitForSingleObject(ev.get(), 1);
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
            handler.on_idle();
            if (handler.should_exit())
                break;
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
                // 挂起: 轮询 on_idle 直到 pending 解除
                // WaitForSingleObject(ev,16) 防止 PeekNamedPipe 空转 100% CPU
                while (handler.has_pending())
                {
                    handler.on_idle();
                    if (handler.has_pending())
                        ::WaitForSingleObject(ev.get(), 16);
                }
                if (handler.should_exit())
                    break;
            }
            cur = (cur == &msgA) ? &msgB : &msgA;
            continue;
        }

        if (!handler.on_connect(*cur))
            return;

        prev_done = cur; // CONNECT completion → 下一轮 read_io 提交
        cur = (cur == &msgA) ? &msgB : &msgA;
    }

    LOG("run_io_loop_no_setup: exit");
}

} // namespace conpty
