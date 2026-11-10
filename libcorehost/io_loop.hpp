// ── conpty/io_loop.hpp ────────────────────────────────────
// ConDrv I/O 主循环（事件驱动，无轮询）。
//
// 关键约束：READ_IO 在没有消息时会阻塞。它**不能**持有会话锁执行，否则输入
// 线程拿不到锁，终端按键要等到下一次 ConDrv 消息才被处理（表现为输入卡死）。
// 因此每轮分成三段：
//   1. 持锁：判断是否有挂起请求、准备上一条 completion、按需 flush 输出；
//   2. 不持锁：执行 READ_IO（阻塞等待由驱动完成）；
//   3. 持锁：分派收到的消息、提交 completion、判断退出。
//
// 等待集：{ConDrv InputAvailableEvent, state_changed}。输入/信号线程在锁内
// 处理完数据后置位 state_changed；这里没有超时参数（见 design/0001 4.6/4.9）。
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

inline void run_io_loop_no_setup(win32::handle_view server, win32::handle_view input_available,
                                 win32::shared_mutex &session_lock, win32::handle_view state_changed,
                                 win32::handle_view close_requested, message_router &router)
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
        bool do_read = false;
        bool submitted_previous = false;

        // ── 阶段 1：持锁准备 ──
        {
            std::unique_lock guard{session_lock};

            if (router.has_pending())
            {
                // 挂起请求未完成：不能复用接收缓冲，也不 READ_IO。
                wait_for_state = true;
            }
            else if (prev_done != nullptr)
            {
                // handler 返回 true 必须已准备 completion。
                assert(condrv_io::completion_matches_descriptor(*prev_done));
                LOG2_IF(true, "submitting completion status=0x%08lx info=%llu", prev_done->complete.IoStatus.Status,
                        prev_done->complete.IoStatus.Information);
                submitted_previous = true;

                // 提交 completion 前先把缓冲的 VT 输出交给终端。
                if (router.has_buffered_vt_output())
                    router.flush_vt_output();
            }

            do_read = !wait_for_state;
        }

        // ── 阶段 2：不持锁执行 READ_IO（可能阻塞到有消息）──
        condrv_io::read_io_result read_result = condrv_io::read_io_result::no_message;
        if (do_read)
        {
            CD_IO_COMPLETE *prev_comp = submitted_previous ? &prev_done->complete : nullptr;
            COREHOST_PERF_SCOPE(io_read_io_try);
            read_result = condrv_io::read_io_try(server, prev_comp, *cur);
        }

        // ── 阶段 3：持锁处理结果 ──
        {
            std::unique_lock guard{session_lock};

            if (do_read)
            {
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
                    if (submitted_previous && router.should_flush_vt_output())
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
                        if (router.on_message(*cur))
                        {
                            // 输出里可能含 CPR/DA/OSC 查询，终端响应只会从 vt_in
                            // 回来。先把输出交给终端并显式完成本条 I/O。
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
                        router.on_connect(*cur);
                        prev_done = cur;
                        cur = (cur == &msgA) ? &msgB : &msgA;
                    }
                }
            }

            if (router.should_exit())
                exit_loop = true;
        }

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
