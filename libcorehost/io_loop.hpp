// ── conpty/io_loop.hpp ────────────────────────────────────
// ConDrv I/O 事件循环。
//
// 功能分解：
// 1. READ_IO 用 overlapped DeviceIoControl：请求挂起后由完成事件唤醒，
//    消息到达事件驱动，不再需要轮询 server 句柄或"消化 pending"等待。
// 2. 双缓冲：本轮 READ_IO 提交上一条 completion，同时读取下一条消息，
//    因此接收缓冲不能覆盖上一条消息的 completion 数据。
// 3. 每轮循环服务一次 VT 输入（vt_in 是外部同步句柄，只能轮询）；空闲
//    等待 16ms 超时用于轮询，READ_IO/信号完成事件可提前唤醒。
// 4. 挂起请求：handler 返回 false 时进入 wait_for_pending_input，直到
//    bridge 用 VT 输入完成挂起的 ReadConsole/RawRead/GetConsoleInput。
//
// message_router 提供 loop 需要的完整会话操作：ConDrv 消息分派、VT idle
// 输入服务、pending 请求等待和退出条件判断。

#pragma once
#include "connect_completion.hpp"
#include "message_router.hpp"
#include "condrv_io.hpp"
#include "perf_diag.hpp"
#include "signal.hpp"
#include "utility/log.hpp"
#include "win32/wait.hpp"

namespace corehost::conpty
{

inline constexpr DWORD io_loop_idle_wait_ms = 16;

// 信号就绪处理：消费信号管道数据；断开时按 EOF 完成 pending 并请求
// 退出（返回 false）。
inline bool handle_signal_ready(pty_signal_consumer *signal, message_router &router)
{
    if (signal == nullptr)
        return true;
    if (signal->handle_event())
        return true;
    LOG("run_io_loop_no_setup: signal pipe closed");
    router.on_signal_disconnected();
    return false;
}

// 等待挂起请求完成：只靠 VT 输入或信号事件推进（继续等 server 会在
// 没有终端输入时重复唤醒，造成空转）。返回 false 表示信号管道断开，
// 已按 EOF 完成 pending，会话应退出。
inline bool wait_pending_inputs(message_router &router, pty_signal_consumer *signal)
{
    while (router.has_pending())
    {
        router.wait_for_pending_input();
        // bridge 的时间片等待已包含信号完成事件；返回后补处理一次信号，
        // 捕获超时返回瞬间信号恰好到达的竞态窗口。
        if (signal != nullptr && !signal->try_handle_event())
        {
            LOG("run_io_loop_no_setup: signal pipe closed while pending");
            router.on_signal_disconnected();
            return false;
        }
    }
    return true;
}

// 运行 ConDrv READ_IO/COMPLETE_IO 主循环。server/event 都是非拥有句柄；
// router 持有实际状态机。READ_IO 通过 overlapped 完成事件驱动，对 pending
// 请求等待 VT 输入显式完成，并在 server 断开或 bridge 可退出时返回。
// signal 非空时，等待集合同时包含信号管道完成事件，单线程同时处理
// ConDrv I/O、VT 输入轮询与 PtySignal I/O。
inline void run_io_loop_no_setup(win32::handle_view server, win32::handle_view ev, message_router &router,
                                 pty_signal_consumer *signal = nullptr)
{
    // server 是 ConDrv \Server 等待/READ_IO 目标；ev 是客户端 input-available
    // 事件，只用于判断 completion 前是否需要先刷 VT 输出。router 持有
    // io_state/pipe_bridge/api_router，是本循环消费消息的唯一入口。
    LOG("run_io_loop_no_setup: enter");

    // READ_IO 会在同一次调用中提交上一条 completion 并读取下一条消息。
    // 双缓冲保证 completion 中指向的 body 不会被下一条消息覆盖。
    corehost::condrv_io::io_msg msgA{}, msgB{};

    // cur 只在 msgA/msgB 之间切换，指向本轮接收缓冲。另一块缓冲可能仍被
    // prev_done 指向，用作上一条消息的 completion 输入。
    corehost::condrv_io::io_msg *cur = &msgA;

    // nullptr 表示本轮不提交 completion；非空表示上一条消息已经处理完，
    // 其 completion 必须作为下一轮 READ_IO 的输入。
    corehost::condrv_io::io_msg *prev_done = nullptr;

    // 单个在飞的 overlapped READ_IO；完成事件与信号事件一起等待。
    corehost::condrv_io::read_io_op read_op;

    // 处理 cur 中的消息并推进双缓冲。返回 false 表示循环应退出。
    const auto dispatch_message = [&]() -> bool {
        if (cur->descriptor.Function == 0)
        {
            // Function==0 是空 descriptor，不对应可完成的 Console I/O。
            // cur 不切换：下一轮 READ_IO 直接覆盖。vt_in 服务由循环顶的
            // 阶段 1 统一完成。
            LOG2("empty ConDrv descriptor");
            return true;
        }

        if (cur->descriptor.Function != CONSOLE_IO_CONNECT)
        {
            LOG2("dispatch message func=%lu id=%08lx:%08lx", cur->descriptor.Function,
                 cur->descriptor.Identifier.HighPart, cur->descriptor.Identifier.LowPart);
            // true 表示 router 已经填好 cur->complete，下一轮 READ_IO 提交。
            // false 表示请求挂起，router 会在后续 VT 输入到达时显式完成。
            if (router.on_message(*cur))
            {
                // 应用输出可能包含 CPR/DA/OSC 查询，终端响应只会从 vt_in 回来，
                // 不会唤醒 ConDrv server。此时必须先让终端看见输出并完成本条
                // I/O，再回到 idle 读取响应；否则下一轮 READ_IO 可能阻塞，导致
                // 依赖终端查询响应的程序卡住。flush 后的终端应答由循环顶的
                // 阶段 1 on_idle 统一读取，无需在此重复。
                if (router.has_buffered_vt_output())
                {
                    router.flush_vt_output();
                    corehost::condrv_io::complete_io(server, cur->complete);
                    LOG2("message completed explicitly after VT flush func=%lu", cur->descriptor.Function);
                    prev_done = nullptr;
                }
                else
                {
                    // 普通输出保留 piggyback completion；下一轮 READ_IO 提交
                    // completion 前会 flush 缓冲 VT，避免额外 COMPLETE_IO 往返。
                    if (router.should_exit())
                    {
                        if (router.has_buffered_vt_output())
                            router.flush_vt_output();
                        corehost::condrv_io::complete_io(server, cur->complete);
                        LOG2("message completed explicitly before shutdown func=%lu", cur->descriptor.Function);
                        return false;
                    }
                    prev_done = cur;
                }
            }
            else
            {
                LOG2("message pending func=%lu", cur->descriptor.Function);
                // 挂起请求只能靠 VT 输入或信号事件推进。
                if (!wait_pending_inputs(router, signal))
                    return false;
                LOG2("pending message completed func=%lu", cur->descriptor.Function);
                if (router.should_exit())
                    return false;
            }
            cur = (cur == &msgA) ? &msgB : &msgA;
            return true;
        }

        connect_completion connect_result = connect_completion::explicit_complete;
        LOG2("dispatch CONNECT id=%08lx:%08lx", cur->descriptor.Identifier.HighPart,
             cur->descriptor.Identifier.LowPart);
        if (!router.on_connect(*cur, connect_result))
        {
            router.flush_vt_output();
            return false;
        }

        // inline_complete 说明 handler 只填了 cur->complete，仍需要下一轮 READ_IO
        // 把 completion 交给 ConDrv。
        if (connect_result == connect_completion::inline_complete)
        {
            prev_done = cur;
        }
        else
        {
            // 首个 CONNECT 可能由 corehost::condrv_io::accept_connection() 通过
            // IOCTL_COMPLETE_IO 显式完成。此时不能再把同一个 completion
            // 作为下一轮 READ_IO 的输入重复提交。
            prev_done = nullptr;
        }
        cur = (cur == &msgA) ? &msgB : &msgA;
        return true;
    };

    for (;;)
    {
        // ── 阶段 1: 服务 VT 输入 ──
        // 键盘输入不产生任何可等待事件，只能每轮循环轮询一次。
        if (!router.has_pending() && prev_done == nullptr)
        {
            router.on_idle();
            if (router.should_exit())
                break;
        }

        // ── 阶段 2: 确保 READ_IO 在飞 ──
        // 挂起请求期间不能发起 READ_IO（completion 未就绪）。
        if (!router.has_pending() && !read_op.pending())
        {
            // 提交上一条 completion 前，先让终端看到待刷输出（应用查询类
            // 输出依赖终端先看到请求再应答）。
            CD_IO_COMPLETE *prev_comp = prev_done ? &prev_done->complete : nullptr;
            if (prev_comp != nullptr && router.has_buffered_vt_output() && ev.valid())
            {
                COREHOST_PERF_SCOPE(io_server_wait_0);
                const auto wait = win32::wait_one(ev, 0);
                if (wait.abandoned())
                {
                    LOG("run_io_loop_no_setup: input event wait abandoned");
                    router.flush_vt_output();
                    return;
                }
                if (!wait.signaled())
                    router.flush_vt_output();
            }

            COREHOST_PERF_SCOPE(io_read_io_try);
            const auto result = corehost::condrv_io::read_io_begin(server, prev_comp, *cur, read_op);
            if (result == corehost::condrv_io::read_io_result::disconnected)
            {
                router.flush_vt_output();
                LOG("run_io_loop_no_setup: read_io disconnected, exiting");
                break;
            }
            // completion 已被驱动接受（无论消息是否立即可用）。
            prev_done = nullptr;
            if (result == corehost::condrv_io::read_io_result::got_message)
            {
                if (!dispatch_message())
                    break;
                continue;
            }
        }

        // ── 阶段 3: 等待活动 ──
        // READ_IO 完成 / 信号就绪事件驱动；16ms 超时用于轮询 vt_in
        //（vt_in 是外部同步句柄，无完成事件）。
        if (read_op.pending())
        {
            COREHOST_PERF_SCOPE(io_server_idle_wait);
            // 信号消费者存在时同时等待其完成事件；否则只等 READ_IO。
            const auto wait = signal != nullptr
                                  ? win32::wait_any(read_op.event(), signal->event(), io_loop_idle_wait_ms)
                                  : win32::wait_one(read_op.event(), io_loop_idle_wait_ms);
            if (signal != nullptr && wait.index == 1)
            {
                if (!handle_signal_ready(signal, router))
                    break;
                continue; // 信号已消费；回到循环顶重新评估状态
            }
            if (wait.abandoned())
            {
                LOG("run_io_loop_no_setup: read wait abandoned");
                router.flush_vt_output();
                return;
            }
            if (wait.signaled())
            {
                const auto result = corehost::condrv_io::read_io_finish(server, *cur, read_op);
                if (result == corehost::condrv_io::read_io_result::disconnected)
                {
                    router.flush_vt_output();
                    LOG("run_io_loop_no_setup: read_io disconnected, exiting");
                    break;
                }
                if (!dispatch_message())
                    break;
                continue;
            }
            // 16ms 超时：回到阶段 1 轮询 vt_in。
            continue;
        }

        // ── 阶段 4: pending 请求等待 ──
        // 挂起请求只能靠 VT 输入或信号事件推进，不能靠 ConDrv 消息推进。
        if (router.has_pending())
        {
            if (!wait_pending_inputs(router, signal))
                break;
            if (router.should_exit())
                break;
            continue;
        }

        // 既无 READ_IO 在飞也无 pending 请求：回到阶段 1 重新评估。
        continue;
    }

    router.flush_vt_output();
    LOG("run_io_loop_no_setup: exit");
}

} // namespace corehost::conpty
