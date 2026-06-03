// ── conpty/message_router.hpp ─────────────────────
// Layer 1: ConDrv 消息路由器。
//
// 功能分解：
// 1. CONNECT/DISCONNECT/CREATE/CLOSE/FLUSH 交给 io_state 维护 ConDrv 状态。
// 2. RAW_READ/RAW_WRITE 交给 pipe_bridge 处理 VT 管道。
// 3. USER_DEFINED 交给 api_router 分派到具体 Console API handler。
#pragma once
#include <windows.h>
#include <span>
#include "miniio/io_thread.hpp"
#include "io_state.hpp"
#include "pipe_bridge.hpp"
#include "api_router.hpp"
#include "utility/log.hpp"

namespace conpty
{

struct message_router
{
    // io 维护 ConDrv 连接对象、CREATE/CLOSE_OBJECT 返回的客户端句柄值和
    // GetConsoleProcessList 使用的进程快照。router 只调用它消费对象层消息。
    io_state &io;

    // bridge 是 VT 侧状态机：RAW_READ、ReadConsole/GetConsoleInput 的 pending
    // 请求、终端输入字节和待刷 VT 输出都在这里推进。
    pipe_bridge &bridge;

    // api 是 USER_DEFINED Console API 的第二层分派器。message_router 不解析
    // CONSOLE_MSG_HEADER 之后的 API body，只把完整消息交给 api_router。
    api_router &api;

    // 消费 CONNECT 消息，并把 io_state 的进程列表同步给 pipe_bridge。completion
    // 返回首个 CONNECT 是显式完成还是可由下一轮 READ_IO 内联提交。
    bool on_connect(miniio::io_msg &msg, connect_completion &completion)
    {
        // msg 是 ConDrv 的 CONNECT 请求；completion 返回给 io_loop，说明
        // handle_connect 是已经显式 COMPLETE_IO，还是只填好 msg.complete。
        // CONNECT 走单独入口，因为 I/O loop 需要知道 completion 是显式完成
        // 还是要在下一轮 READ_IO 中提交。
        log_descriptor("enter CONNECT", msg);
        LOG2("consume CONNECT via io_state; before processCount=%zu bridgeProcessCount=%zu", io.process_count,
             bridge.process_count());
        auto ok = io.handle_connect(msg, completion);
        sync_process_snapshot();
        LOG2("CONNECT consumed ok=%d completionMode=%d after processCount=%zu bridgeProcessCount=%zu", ok,
             static_cast<int>(completion), io.process_count, bridge.process_count());
        if (completion == connect_completion::inline_complete)
            log_completion("CONNECT inline completion", msg);
        return ok;
    }

    // 消费一条非首个 CONNECT 的 ConDrv 消息。返回 true 表示 msg.complete 可由
    // io_loop 提交；false 表示 bridge 已持有该请求等待 VT 输入。
    bool on_message(miniio::io_msg &msg)
    {
        // msg 是除首个 CONNECT 外的一条 ConDrv 消息。返回 true 表示 msg.complete
        // 已准备好，可由下一轮 READ_IO 提交；false 表示 bridge 挂起了请求。
        // 非 CONNECT 消息不需要额外 completion 分类，dispatch 的 bool 直接
        // 告诉 I/O loop 是否可以下一轮提交 completion。
        log_descriptor("enter message", msg);
        const bool completed_inline = dispatch(msg);
        LOG2("message consumed func=%lu completedInline=%d pendingKind=%d vtInBuffered=%lu vtInQueued=%zu vtOut=%zu",
             msg.descriptor.Function, completed_inline, static_cast<int>(bridge.pending_kind()),
             bridge.buffered_vt_input_bytes(), bridge.queued_vt_input_bytes(), bridge.buffered_vt_output_bytes());
        if (completed_inline)
            log_completion("message inline completion", msg);
        return completed_inline;
    }

    // 没有新的 ConDrv 消息时推进 VT 输入和 EOF 检测。它不提交 completion，
    // 只让 bridge 有机会把终端输入转成 pending 结果或 input_buffer 事件。
    void on_idle()
    {
        // idle 阶段没有新的 ConDrv 消息；corehost 必须主动抽取 vt_in，因为
        // 终端键盘输入不会唤醒 ConDrv server 句柄。
        LOG3("idle enter pendingKind=%d vtEof=%d vtInBuffered=%lu vtInQueued=%zu vtOut=%zu",
             static_cast<int>(bridge.pending_kind()), bridge.pending_vt_eof(), bridge.buffered_vt_input_bytes(),
             bridge.queued_vt_input_bytes(), bridge.buffered_vt_output_bytes());
        bridge.on_idle();
        LOG3("idle consumed pendingKind=%d vtEof=%d vtInBuffered=%lu vtInQueued=%zu vtOut=%zu",
             static_cast<int>(bridge.pending_kind()), bridge.pending_vt_eof(), bridge.buffered_vt_input_bytes(),
             bridge.queued_vt_input_bytes(), bridge.buffered_vt_output_bytes());
    }

    // 将 bridge 中已缓冲的 VT 输出写到终端。I/O loop 在 completion 边界和
    // 退出前调用它，避免终端可见输出滞留。
    void flush_vt_output()
    {
        // vt_out 由 bridge 内部批量缓冲；router 在 completion 前后根据 I/O
        // loop 时序调用这里，避免应用已完成但终端仍看不到输出。
        LOG2_IF(bridge.buffered_vt_output_bytes() != 0, "flushing VT output bytes=%zu",
                bridge.buffered_vt_output_bytes());
        bridge.vt_flush();
    }

    // 查询是否有等待写入终端的 VT 输出字节。
    [[nodiscard]] bool has_buffered_vt_output() const noexcept
    {
        const bool result = bridge.has_buffered_vt_output();
        LOG3_IF(result, "VT output buffered bytes=%zu", bridge.buffered_vt_output_bytes());
        return result;
    }

    // 查询 VT 输出缓冲是否达到主动刷新阈值。
    [[nodiscard]] bool should_flush_vt_output() const noexcept
    {
        const bool result = bridge.should_flush_vt_output();
        LOG3_IF(result, "VT output reached flush threshold bytes=%zu", bridge.buffered_vt_output_bytes());
        return result;
    }

    // 查询 bridge 是否持有尚未完成的 RawRead/ReadConsole/GetConsoleInput 请求。
    bool has_pending() const
    {
        // pending 状态只存在于 bridge：RawRead/ReadConsole/GetConsoleInput。
        const bool result = bridge.has_pending();
        LOG3_IF(result, "pending request kind=%d vtEof=%d vtInBuffered=%lu vtInQueued=%zu",
                static_cast<int>(bridge.pending_kind()), bridge.pending_vt_eof(), bridge.buffered_vt_input_bytes(),
                bridge.queued_vt_input_bytes());
        return result;
    }

    // 等待并推进当前 pending 输入请求。函数返回时 pending 可能完成，也可能
    // 因短时间片轮询仍然存在。
    void wait_for_pending_input()
    {
        // pending 请求只能靠 VT 输入或终端关闭推进，不能靠 ConDrv 消息推进。
        LOG3("wait pending input begin kind=%d vtEof=%d vtInBuffered=%lu vtInQueued=%zu",
             static_cast<int>(bridge.pending_kind()), bridge.pending_vt_eof(), bridge.buffered_vt_input_bytes(),
             bridge.queued_vt_input_bytes());
        bridge.wait_for_pending_vt_input();
        LOG3("wait pending input end kind=%d vtEof=%d vtInBuffered=%lu vtInQueued=%zu",
             static_cast<int>(bridge.pending_kind()), bridge.pending_vt_eof(), bridge.buffered_vt_input_bytes(),
             bridge.queued_vt_input_bytes());
    }

    // 查询会话是否可以退出。只有 VT 输入 EOF 且没有 pending 请求时返回 true。
    bool should_exit() const
    {
        // vt_in EOF 且没有 pending 请求时，bridge 认为会话可以退出。
        const bool result = bridge.should_exit();
        LOG2_IF(result, "session exit requested vtEof=%d vtInBuffered=%lu vtInQueued=%zu vtOut=%zu",
                bridge.pending_vt_eof(), bridge.buffered_vt_input_bytes(), bridge.queued_vt_input_bytes(),
                bridge.buffered_vt_output_bytes());
        return result;
    }

  private:
    // 记录 ConDrv descriptor 的路由关键字段，用于诊断消息进入 router 时的
    // 原始状态。
    static void log_descriptor(const char *label, const miniio::io_msg &msg)
    {
        // label 标识消息进入 router 的阶段；msg.descriptor 是 ConDrv 给
        // corehost 的原始路由数据，后续 dispatch 只依据这些字段决定消费路径。
        LOG2("%hs func=%lu id=%08lx:%08lx pid=%llu object=%llu inputSize=%lu outputSize=%lu", label,
             msg.descriptor.Function, msg.descriptor.Identifier.HighPart, msg.descriptor.Identifier.LowPart,
             msg.descriptor.Process, msg.descriptor.Object, msg.descriptor.InputSize, msg.descriptor.OutputSize);
    }

    // 记录 corehost 准备交回 ConDrv 的 completion 状态。
    static void log_completion(const char *label, const miniio::io_msg &msg)
    {
        // label 标识 completion 的来源；msg.complete 是 corehost 回给 ConDrv
        // 的结果，io_loop 可能同步提交，也可能由 bridge 显式 COMPLETE_IO。
        LOG2("%hs id=%08lx:%08lx status=0x%08lx information=%llu", label, msg.descriptor.Identifier.HighPart,
             msg.descriptor.Identifier.LowPart, msg.complete.Status, msg.complete.Information);
    }

    // 按 descriptor.Function 选择 io_state、pipe_bridge 或 api_router 处理消息。
    // 返回 false 只表示请求被挂起，不表示失败。
    bool dispatch(miniio::io_msg &msg)
    {
        // msg.body 的解释权由 Function 决定：对象管理消息由 io_state 解释，
        // RAW_* 由 bridge/API raw 路径解释，USER_DEFINED 由 api_router 解释。
        // descriptor.Function 只使用 ConDrv 定义的 CONSOLE_IO_* 值。
        // 未识别值按成功完成，避免未知/废弃请求阻塞客户端。
        switch (msg.descriptor.Function)
        {
        case CONSOLE_IO_CONNECT: {
            // 理论上 CONNECT 已由 on_connect 处理；这里保留分支给直接 dispatch
            // 的调用路径，仍然同步进程列表快照。
            LOG2("dispatch consumes CONNECT through io_state fallback");
            connect_completion completion = connect_completion::explicit_complete;
            bool ok = io.handle_connect(msg, completion);
            sync_process_snapshot();
            LOG2("fallback CONNECT consumed ok=%d completionMode=%d processCount=%zu bridgeProcessCount=%zu", ok,
                 static_cast<int>(completion), io.process_count, bridge.process_count());
            return ok;
        }
        case CONSOLE_IO_DISCONNECT: {
            LOG2("dispatch consumes DISCONNECT through io_state after flushing vtOut=%zu",
                 bridge.buffered_vt_output_bytes());
            bridge.vt_flush();
            bool ok = io.handle_disconnect(msg);
            sync_process_snapshot();
            LOG2("DISCONNECT consumed ok=%d processCount=%zu bridgeProcessCount=%zu", ok, io.process_count,
                 bridge.process_count());
            return ok;
        }
        case CONSOLE_IO_CREATE_OBJECT:
            LOG2("dispatch consumes CREATE_OBJECT through io_state inputSize=%lu", msg.descriptor.InputSize);
            return io.handle_create_object(msg);
        case CONSOLE_IO_CLOSE_OBJECT:
            LOG2("dispatch consumes CLOSE_OBJECT through io_state object=%llu", msg.descriptor.Object);
            return io.handle_close_object(msg);
        case CONSOLE_IO_RAW_WRITE:
            // 对齐原版 IoSorter: RAW_WRITE 按 WriteConsoleA 处理，再转成 VT 输出。
            LOG2("dispatch consumes RAW_WRITE through api_raw_write_console bytes=%lu", msg.descriptor.InputSize);
            return api_raw_write_console(msg, api.state, api.active_screen_buffer(), api.inp, bridge);
        case CONSOLE_IO_RAW_READ:
            // 客户端请求原始输入。若当前没有足够 VT 输入，bridge 会挂起请求。
            LOG2("dispatch consumes RAW_READ through pipe_bridge outputSize=%lu pendingKindBefore=%d",
                 msg.descriptor.OutputSize, static_cast<int>(bridge.pending_kind()));
            return bridge.handle_raw_read(msg);
        case CONSOLE_IO_USER_DEFINED:
            // Console API 消息体以 CONSOLE_MSG_HEADER 开头，由 api_router 继续拆层。
            LOG2("dispatch consumes USER_DEFINED through api_router inputSize=%lu outputSize=%lu",
                 msg.descriptor.InputSize, msg.descriptor.OutputSize);
            return api.handle_user_defined(msg);
        case CONSOLE_IO_RAW_FLUSH:
            // 原版 RAW_FLUSH 复用 ServerFlushConsoleInputBuffer；这里没有
            // USER_DEFINED header，因此只清空事件队列并返回普通 IO completion。
            LOG2("dispatch consumes RAW_FLUSH through input_buffer/io_state");
            api.inp.flush();
            io.handle_raw_flush(msg);
            return true;
        default:
            // 未实现的 Function 不能阻塞客户端；按空成功完成。
            LOG2("dispatch consumes unknown function as empty completion func=%lu", msg.descriptor.Function);
            miniio::prepare_completion(msg);
            return true;
        }
    }

    // CONNECT/DISCONNECT 后把 io_state 的实时进程列表复制到 bridge 的 API 快照。
    void sync_process_snapshot() noexcept
    {
        // io_state 保存的是当前真实连接进程列表；bridge 保存的是 API 查询可见
        // 快照。CONNECT/DISCONNECT 后必须同步，否则 GetConsoleProcessList 会滞后。
        bridge.set_process_list(std::span<const DWORD>{io.process_list.data(), io.process_count});
        LOG2("process snapshot copied count=%zu", io.process_count);
    }
};

} // namespace conpty
