// ── miniio/io_loop.hpp ────────────────────────────────────
// ConDrv I/O 事件循环
//
// 提供可复用的消息循环骨架，由 defterm 和 comserver 共同使用。
// 调用方提供 Handler 对象，通过模板参数定制 CONNECT 和非 CONNECT
// 消息的处理方式。
//
// Handler 需提供:
//   bool on_connect(miniio::io_msg &msg)   — CONNECT 处理, 应调用 accept_connection
//   bool on_message(miniio::io_msg &msg)  — 非 CONNECT 消息, true=已完成, false=挂起
//   void on_idle()                         — 空闲时调用, 服务挂起消息
//   bool has_pending() const               — 检查是否有挂起 I/O
//   bool should_exit() const               — VT pipe 断开后 I/O 循环退出

#pragma once
#include "io_thread.hpp"
#include "utility/log.hpp"

namespace miniio
{

// ── run_io_loop ────────────────────────────────────────────
// 对应原版 ConsoleCreateIoThread 后半段 + ConsoleIoThread 的消息循环。
//
// 原版用两个不同的 CONSOLE_API_MSG (ReceiveMsg + ReplyMsg):
//   ReadIo(ReplyMsg, &ReceiveMsg)
//   → DeviceIoControl(IOCTL_READ_IO, ReplyMsg, ..., &ReceiveMsg, ...)
//   input (ReplyMsg) 和 output (ReceiveMsg) 是不同对象。
//
// ★ 关键: 不能像之前那样 prev=&msg.complete 同时 output=&msg.descriptor,
//   因为 prev->Write.Data 指向 msg.body，而新消息也写入 msg.body，
//   驱动可能先写后读导致完成数据被覆盖。
//
// 此处用双缓冲: msgA/msgB 交替，保证 input 和 output 永不重叠。
template <typename Handler>
inline void run_io_loop(win32::handle_view server, win32::handle_view ev, Handler &handler)
{
    set_server_info(server, ev);

    io_msg msgA{}, msgB{};
    io_msg *cur = &msgA;
    io_msg *prev_done = nullptr;

    for (;;)
    {
        // 有上一条完成包待提交时，必须立即进入 read_io(prev_comp, ...)。
        // 若在提交 completion 前先等待，会把每个 Console API 往返都人为延迟。
        //
        // 另外，终端键盘数据走 vt_in 管道，不会触发 ConDrv 的 ev。
        // 因此空闲时必须先 on_idle() 扫 vt_in，再短等待 ConDrv 事件；否则键入只能被
        // 16ms 轮询发现，PowerShell/PSReadLine 多轮小 API 会把延迟放大成明显卡顿。
        if (!handler.has_pending() && prev_done == nullptr)
        {
            handler.on_idle();
            if (handler.should_exit())
                break;
            if (!handler.has_pending())
                ::WaitForSingleObject(ev.get(), 1);
        }

        CD_IO_COMPLETE *prev_comp = prev_done ? &prev_done->complete : nullptr;
        auto read_result = read_io_try(server, prev_comp, *cur);
        if (read_result == read_io_result::disconnected)
            break;
        if (read_result == read_io_result::no_message)
        {
            prev_done = nullptr;
            handler.on_idle();
            if (handler.should_exit())
                break;
            continue;
        }
        prev_done = nullptr;

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
                prev_done = cur;
                handler.on_idle();
                if (handler.should_exit())
                    break;
            }
            else
            {
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

        prev_done = cur;
        cur = (cur == &msgA) ? &msgB : &msgA;
    }
}

// ── run_io_loop_no_setup ───────────────────────────────────
// 同 run_io_loop 但不调用 set_server_info (event 已由调用方注册)
template <typename Handler>
inline void run_io_loop_no_setup(win32::handle_view server, win32::handle_view ev, Handler &handler)
{
    LOG("run_io_loop_no_setup: enter");
    io_msg msgA{}, msgB{};
    io_msg *cur = &msgA;
    io_msg *prev_done = nullptr; // 上一条已完成消息，其 completion 需在下一轮 read_io 中提交

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
        auto read_result = read_io_try(server, prev_comp, *cur);
        if (read_result == read_io_result::disconnected)
        {
            LOG("run_io_loop_no_setup: read_io false, exiting");
            break;
        }
        if (read_result == read_io_result::no_message)
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
    LOG("run_io_loop_no_setup: loop returned");
}

} // namespace miniio
