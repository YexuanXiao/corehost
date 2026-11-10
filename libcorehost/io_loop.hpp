// ── conpty/io_loop.hpp ────────────────────────────────────
// ConDrv I/O 主循环（事件驱动，无轮询）。
//
// 关键约束：
// 1. READ_IO 在没有消息时会阻塞，不能持会话锁执行（否则输入线程拿不到锁，
//    终端按键要等下一次 ConDrv 消息才被处理）。
// 2. 有挂起读请求（ReadConsole/RawRead/GetConsoleInput）时，循环仍要继续服务
//    其它 ConDrv 请求：多线程客户端会一边等待输入，一边查询光标/模式（典型是
//    PowerShell 的 PSReadLine）。挂起请求所在的接收缓冲必须保持不动，因此这时
//    用独立的 scratch 缓冲读取，并在处理完后立即显式 COMPLETE_IO。
//
// 每轮分三段：
//   1. 持锁准备（判断是否挂起、准备上一条 completion、按需 flush）；
//   2. 不持锁执行 READ_IO（可能阻塞到有消息）；
//   3. 持锁分派消息、提交 completion、判断退出。
//
// 等待集：{ConDrv InputAvailableEvent, state_changed}（可能再加窗口关闭事件）。
// 没有超时参数（见 design/0001 4.6/4.9）。
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

    // 挂起读期间使用的独立接收缓冲：pending 请求所在的缓冲不能被覆盖。
    condrv_io::io_msg scratch{};

    // cur 只在 msgA/msgB 之间切换，指向本轮接收缓冲。
    condrv_io::io_msg *cur = &msgA;

    // nullptr 表示本轮不提交 completion；非空表示上一条消息已经处理完，
    // 其 completion 必须作为下一轮 READ_IO 的输入。
    condrv_io::io_msg *prev_done = nullptr;

    for (;;)
    {
        bool exit_loop = false;
        bool wait_for_any = false;
        bool do_read = false;
        bool submitted_previous = false;
        bool serving_pending = false;

        // ── 阶段 1：持锁准备 ──
        {
            std::unique_lock guard{session_lock};

            if (router.has_pending())
            {
                // 挂起读：用 scratch 缓冲继续 READ_IO，服务其它请求。
                serving_pending = true;
                do_read = true;
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

                do_read = true;
            }
            else
            {
                do_read = true;
            }
        }

        // ── 阶段 2：不持锁执行 READ_IO（可能阻塞到有消息）──
        condrv_io::read_io_result read_result = condrv_io::read_io_result::no_message;
        if (do_read)
        {
            condrv_io::io_msg &target = serving_pending ? scratch : *cur;
            CD_IO_COMPLETE *prev_comp = submitted_previous ? &prev_done->complete : nullptr;
            COREHOST_PERF_SCOPE(io_read_io_try);
            read_result = condrv_io::read_io_try(server, prev_comp, target);
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
                else if (serving_pending)
                {
                    // 挂起读期间收到的其它请求：处理完立即显式完成，scratch 才能复用。
                    condrv_io::io_msg &msg = scratch;
                    if (msg.descriptor.Function == 0)
                    {
                        LOG2("empty ConDrv descriptor while pending");
                        wait_for_any = true;
                    }
                    else if (msg.descriptor.Function == CONSOLE_IO_CONNECT)
                    {
                        LOG2("dispatch CONNECT while pending");
                        router.on_connect(msg);
                        condrv_io::complete_io(server, msg.complete);
                    }
                    else
                    {
                        LOG2("dispatch message while pending func=%lu", msg.descriptor.Function);
                        if (router.on_message(msg))
                        {
                            if (router.has_buffered_vt_output())
                                router.flush_vt_output();
                            condrv_io::complete_io(server, msg.complete);
                        }
                        else
                        {
                            // 又出现新的挂起请求（同一时间只允许一个）：记录并继续，
                            // bridge 的 pending 状态会以后到的请求为准。
                            LOG("run_io_loop_no_setup: unexpected second pending request");
                        }
                    }
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
                            // 新挂起的请求：下一轮用 scratch 继续服务其它消息。
                            LOG2("message pending func=%lu", cur->descriptor.Function);
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

        if (wait_for_any)
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
