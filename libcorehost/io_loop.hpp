// ── conpty/io_loop.hpp ────────────────────────────────────
// ConDrv I/O 主循环（事件驱动，无轮询）。
//
// 职责：
// 1. READ_IO 双缓冲：本轮 READ_IO 提交上一条 completion，同时读取下一条
//    消息，因此接收缓冲不能覆盖上一条消息的 completion 数据。
// 2. 同步处理：每一轮在会话锁内完成 READ_IO、消息分派与输出 flush；锁外
//    只做无限等待。
// 3. 等待集：{ConDrv InputAvailableEvent, state_changed}。输入/信号线程在
//    锁内处理完数据后置位 state_changed，主循环醒来重读真实状态，不依赖
//    事件携带任何数据。
// 4. 挂起请求：handler 返回 false 时请求已交给 bridge 的 pending 状态，主
//    循环只等 state_changed；输入线程完成它之后主循环自然继续 READ_IO。
//
// 这里没有任何超时参数：所有等待都是 INFINITE，终止性由"输入线程 EOF/取消"
// 与"ConDrv 断开"保证（见 doc/design/0001.md 4.6、4.9）。
#pragma once
#include <cassert>
#include <mutex>
#include "message_router.hpp"
#include "condrv_io.hpp"
#include "perf_diag.hpp"
#include "utility/log.hpp"
#include "win32/srwlock.hpp"
#include "win32/wait.hpp"

namespace corehost::conpty
{

// 运行 ConDrv READ_IO/COMPLETE_IO 主循环。server/input_available/state_changed
// 都是非拥有句柄；router 持有实际状态机，session_lock 保护全部控制台共享状态。
inline void run_io_loop_no_setup(win32::handle_view server, win32::handle_view input_available,
                                 win32::shared_mutex &session_lock, win32::handle_view state_changed,
                                 win32::handle_view close_requested,
                                 message_router &router)
{
    LOG("run_io_loop_no_setup: enter");

    // READ_IO 会在同一次调用中提交上一条 completion 并读取下一条消息。
    // 双缓冲保证 completion 中指向的 body 不会被下一条消息覆盖。
    condrv_io::io_msg msgA{}, msgB{};

    // cur 只在 msgA/msgB 之间切换，指向本轮接收缓冲。
    condrv_io::io_msg *cur = &msgA;

    // nullptr 表示本轮不提交 completion；非空表示上一条消息已经处理完，
    // 其 completion 必须作为下一轮 READ_IO 的输入。
    condrv_io::io_msg *prev_done = nullptr;

    for (;;)
    {
        bool exit_loop = false;
        bool wait_for_state = false;
        bool wait_for_any = false;

        {
            // 整个处理阶段持有会话锁：输入线程与信号线程只在拿不到锁时等待。
            std::unique_lock guard{session_lock};

            if (router.has_pending())
            {
                // 挂起请求仍未完成：不能复用接收缓冲，只等状态变化。
                wait_for_state = true;
            }
            else
            {
                CD_IO_COMPLETE *prev_comp = prev_done ? &prev_done->complete : nullptr;
                if (prev_comp)
                {
                    // handler 返回 true 必须已准备 completion，否则会把上一轮
                    // 残留的标识符交给驱动。
                    assert(condrv_io::completion_matches_descriptor(*prev_done));
                }
                LOG2_IF(prev_comp != nullptr, "submitting completion status=0x%08lx info=%llu",
                        prev_done->complete.IoStatus.Status, prev_done->complete.IoStatus.Information);

                // 提交 completion 前先把缓冲的 VT 输出交给终端，避免应用输出
                // 落后于它已经完成的 I/O。
                if (prev_comp != nullptr && router.has_buffered_vt_output())
                    router.flush_vt_output();

                auto read_result = condrv_io::read_io_result::no_message;
                {
                    COREHOST_PERF_SCOPE(io_read_io_try);
                    read_result = condrv_io::read_io_try(server, prev_comp, *cur);
                }

                if (read_result == condrv_io::read_io_result::disconnected)
                {
                    router.flush_vt_output();
                    LOG("run_io_loop_no_setup: read_io false, exiting");
                    exit_loop = true;
                }
                else if (read_result == condrv_io::read_io_result::no_message)
                {
                    // no_message 仍可能已经消费 prev_comp，必须清空 prev_done。
                    prev_done = nullptr;
                    LOG2("READ_IO returned no message");

                    if (router.has_buffered_vt_output())
                        router.flush_vt_output();

                    wait_for_any = true;
                }
                else
                {
                    prev_done = nullptr; // completion 已被 read_io 消费
                    if (prev_comp != nullptr && router.should_flush_vt_output())
                        router.flush_vt_output();

                    if (cur->descriptor.Function == 0)
                    {
                        // Function==0 是空 descriptor，不对应可完成的 Console I/O。
                        LOG2("empty ConDrv descriptor");
                        wait_for_any = true;
                    }
                    else if (cur->descriptor.Function != CONSOLE_IO_CONNECT)
                    {
                        LOG2("dispatch message func=%lu id=%08lx:%08lx", cur->descriptor.Function,
                             cur->descriptor.Identifier.HighPart, cur->descriptor.Identifier.LowPart);
                        // true 表示 router 已经填好 cur->complete，下一轮 READ_IO 提交。
                        // false 表示请求挂起，输入线程会在后续 VT 输入到达时完成它。
                        if (router.on_message(*cur))
                        {
                            // 应用输出可能包含 CPR/DA/OSC 查询，终端响应只会从
                            // vt_in 回来。先把输出交给终端并显式完成本条 I/O。
                            if (router.has_buffered_vt_output())
                            {
                                router.flush_vt_output();
                                condrv_io::complete_io(server, cur->complete);
                                prev_done = nullptr;
                            }
                            else
                            {
                                prev_done = cur;
                            }
                        }
                        else
                        {
                            LOG2("message pending func=%lu", cur->descriptor.Function);
                            wait_for_state = true;
                        }
                        cur = (cur == &msgA) ? &msgB : &msgA;
                    }
                    else
                    {
                        LOG2("dispatch CONNECT id=%08lx:%08lx", cur->descriptor.Identifier.HighPart,
                             cur->descriptor.Identifier.LowPart);
                        // CONNECT 的 completion 总是由 on_connect 填好并留在 msg 里，
                        // 作为下一轮 READ_IO 的 piggyback completion 提交。
                        router.on_connect(*cur);
                        prev_done = cur;
                        cur = (cur == &msgA) ? &msgB : &msgA;
                    }
                }
            }

            if (router.should_exit())
                exit_loop = true;
        } // 释放会话锁

        if (exit_loop)
            break;

        if (wait_for_state)
        {
            // 只等输入线程完成挂起请求、关闭输入，或窗口请求关闭会话。
            if (!close_requested.valid())
            {
                const auto wait = win32::wait_one(state_changed, INFINITE);
                if (wait.abandoned())
                    win32::throw_last_error(win32::error::invalid_state);
            }
            else
            {
                const auto wait = win32::wait_any(state_changed, close_requested, INFINITE);
                if (wait.abandoned())
                    win32::throw_last_error(win32::error::invalid_state);
                if (wait.index == 1)
                {
                    LOG("run_io_loop_no_setup: close requested while pending");
                    break;
                }
            }
        }
        else if (wait_for_any)
        {
            // ConDrv 有新消息、输入/信号线程改变状态，或窗口请求关闭会话。
            if (!close_requested.valid())
            {
                const auto wait = win32::wait_any(input_available, state_changed, INFINITE);
                if (wait.abandoned())
                    win32::throw_last_error(win32::error::invalid_state);
            }
            else
            {
                const auto wait = win32::wait_any(input_available, state_changed, close_requested, INFINITE);
                if (wait.abandoned())
                    win32::throw_last_error(win32::error::invalid_state);
                if (wait.index == 2)
                {
                    LOG("run_io_loop_no_setup: close requested by window");
                    break;
                }
            }
        }
    }

    router.flush_vt_output();
    LOG("run_io_loop_no_setup: exit");
}

} // namespace corehost::conpty
