// ── conpty/message_router.hpp ─────────────────────
// Layer 1: ConDrv 消息路由器，同时是唯一持有会话状态锁的 IO 模块。
//
// 功能分解：
// 1. CONNECT/DISCONNECT/CREATE/CLOSE/FLUSH 交给 io_state 维护 ConDrv 状态。
// 2. RAW_READ/RAW_WRITE 交给 pipe_bridge 处理 VT 管道。
// 3. USER_DEFINED 交给 api_router 分派到具体 Console API handler。
// 4. run() 是主 I/O 循环；输入线程只调用 on_* 入口，每个入口只加一次锁。
#pragma once
#include <windows.h>
#include <cassert>
#include <cstddef>
#include <cstring>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>
#include "condrv_io.hpp"
#include "io_state.hpp"
#include "pipe_bridge.hpp"
#include "api_router.hpp"
#include "pseudo_window.hpp"
#include "perf_diag.hpp"
#include "utility/log.hpp"
#include "win32/event.hpp"
#include "win32/srwlock.hpp"
#include "win32/wait.hpp"

namespace corehost::conpty
{

using namespace std::literals;

class message_router
{
  public:
    // 生产路径：所有依赖先构造，再由构造函数接收。
    message_router(io_state &io_state_ref, pipe_bridge &bridge_ref, api_router &api_ref,
                   pseudo_window &window, win32::handle_view server, win32::handle_view input_available) noexcept
        : io(io_state_ref), bridge(bridge_ref), api(api_ref), _window(&window), _server(server),
          _input_available(input_available)
    {
        bridge.set_process_registry(io.process_list());
    }

    // 测试路径：只分派消息，不跑循环。
    message_router(io_state &io_state_ref, pipe_bridge &bridge_ref, api_router &api_ref) noexcept
        : io(io_state_ref), bridge(bridge_ref), api(api_ref)
    {
        bridge.set_process_registry(io.process_list());
    }

    // 消费 CONNECT 消息；进程列表由 io_state 的 process_registry 直接共享。
    // 本函数不自己加锁：由 run()/测试在持锁或单线程下调用。
    void on_connect(condrv_io::io_msg &msg)
    {
        log_descriptor("enter CONNECT", msg);
        LOG2("consume CONNECT via io_state; before processCount=%zu bridgeProcessCount=%zu", io.process_list().count(),
             bridge.process_count());
        io.handle_connect(msg);
        _console_connected = true;
        apply_early_pty_signals();
        {
            _client_process = win32::handle{::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, static_cast<DWORD>(msg.descriptor.Process))};
            win32::handle &process = _client_process;
            if (process.valid())
            {
                wchar_t path[512]{};
                DWORD size = ARRAYSIZE(path);
                if (::QueryFullProcessImageNameW(process.get(), 0, path, &size) != FALSE)
                    LOG("message_router::on_connect: client image=%ls", path);
            }
            else
            {
                LOG("message_router::on_connect: OpenProcess client failed err=%lu", ::GetLastError());
            }
        }
        LOG2("CONNECT consumed after processCount=%zu bridgeProcessCount=%zu", io.process_list().count(),
             bridge.process_count());
        log_completion("CONNECT completion", msg);
    }

    // 消费一条非首个 CONNECT 的消息。返回 true 表示 msg.complete 可以提交；
    // false 表示请求已挂起，调用方应继续 pump_pending。
    bool on_message(condrv_io::io_msg &msg)
    {
        log_descriptor("enter message", msg);
        const bool completed_inline = dispatch(msg);
        LOG2("message consumed func=%lu completedInline=%d pendingKind=%d vtInBuffered=%lu vtInQueued=%zu vtOut=%zu",
             msg.descriptor.Function, completed_inline, static_cast<int>(bridge.pending_kind()),
             bridge.buffered_vt_input_bytes(), bridge.queued_vt_input_bytes(), bridge.buffered_vt_output_bytes());
        if (completed_inline)
            log_completion("message inline completion", msg);
        return completed_inline;
    }

    // 主循环：READ_IO 在锁外，收到消息后只加一次锁处理。
    void run();

    // 输入源入口：每次调用只加一次锁，处理完整批次。
    void on_input_bytes(std::span<const std::byte> bytes);
    void on_input_eof();
    void on_pty_signal(unsigned short id, std::span<const std::byte> payload);
    void on_signal_closed();
    void on_window_visibility(bool visible);

  private:
    // 会话状态锁：全仓库只在 message_router.hpp 内出现。
    win32::shared_mutex _mutex;

    // 输入线程改变状态后唤醒 wait_any 中的主循环。
    win32::event _state_changed{win32::create_tag, /*manual_reset=*/false, /*initial=*/false};

    win32::handle _client_process;

    // 由构造函数注入，生命周期由 conpty 会话覆盖。
    io_state &io;
    pipe_bridge &bridge;
    api_router &api;
    pseudo_window *_window = nullptr;
    win32::handle_view _server;
    win32::handle_view _input_available;

    // 连接前到达的 show/hide 与 resize 必须像系统宿主一样暂存，连接后再应用。
    bool _console_connected = false;
    std::optional<bool> _initial_visibility;
    std::optional<COORD> _early_resize;


    void signal_state()
    {
        _state_changed.set();
    }


    static void deliver_ctrl_events(pipe_bridge &target, std::span<const std::pair<DWORD, DWORD>> events) noexcept
    {
        for (const auto &[event, pid] : events)
            (void)target.deliver_ctrl_event(pid, event);
    }

    // 连接前的 show/hide/resize 只在 CONNECT 后应用；ClearBuffer 在连接前丢弃。
    void apply_early_pty_signals()
    {
        if (_early_resize)
        {
            const COORD size = *_early_resize;
            _early_resize.reset();
            const auto bytes = std::as_bytes(std::span{&size, 1});
            bridge.signal_reader().apply(static_cast<unsigned short>(PtySignal::ResizeWindow), bytes);
            bridge.queue_window_buffer_size_event();
        }
        if (_initial_visibility)
        {
            const bool visible = *_initial_visibility;
            _initial_visibility.reset();
            if (_window != nullptr)
                _window->set_visibility(visible);
        }
    }


    // 记录 ConDrv descriptor 的路由关键字段，用于诊断消息进入 router 时的
    // 原始状态。
    static void log_descriptor(const char *label, const condrv_io::io_msg &msg)
    {
        LOG2("%hs func=%lu id=%08lx:%08lx pid=%llu object=%llu inputSize=%lu outputSize=%lu", label,
             msg.descriptor.Function, msg.descriptor.Identifier.HighPart, msg.descriptor.Identifier.LowPart,
             msg.descriptor.Process, msg.descriptor.Object, msg.descriptor.InputSize, msg.descriptor.OutputSize);
    }

    // 记录 corehost 准备交回 ConDrv 的 completion 状态。
    static void log_completion(const char *label, const condrv_io::io_msg &msg)
    {
        LOG2("%hs id=%08lx:%08lx status=0x%08lx information=%llu", label, msg.descriptor.Identifier.HighPart,
             msg.descriptor.Identifier.LowPart, msg.complete.IoStatus.Status, msg.complete.IoStatus.Information);
    }

    // 按 descriptor.Function 选择 io_state、pipe_bridge 或 api_router 处理消息。
    // 返回 false 只表示请求被挂起，不表示失败。
    bool dispatch(condrv_io::io_msg &msg)
    {
        switch (msg.descriptor.Function)
        {
        case CONSOLE_IO_CONNECT:
            std::unreachable();
        case CONSOLE_IO_DISCONNECT: {
            LOG2("dispatch consumes DISCONNECT through io_state after flushing vtOut=%zu",
                 bridge.buffered_vt_output_bytes());
            bridge.vt_flush();
            bool ok = io.handle_disconnect(msg);
            LOG2("DISCONNECT consumed ok=%d processCount=%zu bridgeProcessCount=%zu", ok, io.process_list().count(),
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
            LOG2("dispatch consumes RAW_WRITE through api_raw_write_console bytes=%lu", msg.descriptor.InputSize);
            return api_raw_write_console(msg, api.state, api.active_screen_buffer(), api.inp, bridge);
        case CONSOLE_IO_RAW_READ:
            LOG2("dispatch consumes RAW_READ through pipe_bridge outputSize=%lu pendingKindBefore=%d",
                 msg.descriptor.OutputSize, static_cast<int>(bridge.pending_kind()));
            return bridge.handle_raw_read(msg);
        case CONSOLE_IO_USER_DEFINED:
            LOG2("dispatch consumes USER_DEFINED through api_router inputSize=%lu outputSize=%lu",
                 msg.descriptor.InputSize, msg.descriptor.OutputSize);
            return api.handle_user_defined(msg);
        case CONSOLE_IO_RAW_FLUSH:
            LOG2("dispatch consumes RAW_FLUSH through input_buffer/io_state");
            api.inp.flush();
            io.handle_raw_flush(msg);
            return true;
        default:
            LOG2("dispatch consumes unknown function as empty completion func=%lu", msg.descriptor.Function);
            condrv_io::prepare_completion(msg);
            return true;
        }
    }
};

inline void message_router::run()
{
    LOG("message_router::run: enter");
    assert(_window != nullptr);
    assert(_server.valid());
    assert(_input_available.valid());

    // READ_IO 会在同一次调用中提交上一条 completion 并读取下一条消息。
    // 双缓冲保证 completion 中指向的 body 不会被下一条消息覆盖。
    condrv_io::io_msg msgA{}, msgB{};
    condrv_io::io_msg scratch{};
    condrv_io::io_msg *cur = &msgA;

    // nullptr 表示本轮不提交 completion；非空表示上一轮 handler 已完成，
    // 其 completion 由本轮 READ_IO 携带。
    condrv_io::io_msg *prev_done = nullptr;
    bool pending = false;

    for (;;)
    {
        // 窗口可能在主线程进入 READ_IO 前关闭；先处理已置位的关闭事件，
        // 避免 CancelIoEx 发生在 READ_IO 之前导致关闭丢失。
        if (::WaitForSingleObject(_window->close_requested().get(), 0) == WAIT_OBJECT_0)
        {
            LOG("message_router::run: close requested before read");
            LOG("message_router::run: close event was already signaled before READ_IO");
            break;
        }

        const bool serving_pending = pending;
        condrv_io::io_msg &target = serving_pending ? scratch : *cur;
        CD_IO_COMPLETE *prev_comp = prev_done ? &prev_done->complete : nullptr;

        COREHOST_PERF_SCOPE(io_read_io_try);
        const auto read_result = condrv_io::read_io_try(_server, prev_comp, target);

        bool exit_loop = false;
        bool wait_for_any = false;
        std::vector<std::pair<DWORD, DWORD>> ctrl_events;



        {
            std::unique_lock guard{_mutex};

            if (read_result == condrv_io::read_io_result::disconnected)
            {
                bridge.vt_flush();
                LOG("message_router::run: disconnected, exiting");
                exit_loop = true;
            }
            else if (read_result == condrv_io::read_io_result::no_message)
            {
                prev_done = nullptr;
                if (bridge.has_buffered_vt_output())
                    bridge.vt_flush();
                wait_for_any = true;
            }
            else
            {
                prev_done = nullptr;
                condrv_io::io_msg &msg = target;

                if (msg.descriptor.Function == 0)
                {
                    LOG2("empty ConDrv descriptor");
                    wait_for_any = true;
                }
                else if (msg.descriptor.Function == CONSOLE_IO_CONNECT)
                {
                    LOG2("dispatch CONNECT id=%08lx:%08lx", msg.descriptor.Identifier.HighPart,
                         msg.descriptor.Identifier.LowPart);
                    on_connect(msg);
                    if (serving_pending)
                        condrv_io::complete_io(_server, msg.complete);
                    else
                    {
                        prev_done = &msg;
                        cur = (cur == &msgA) ? &msgB : &msgA;
                    }
                }
                else
                {
                    LOG2("dispatch message func=%lu id=%08lx:%08lx", msg.descriptor.Function,
                         msg.descriptor.Identifier.HighPart, msg.descriptor.Identifier.LowPart);
                    const bool completed = on_message(msg);
                    if (completed)
                    {
                        if (bridge.has_buffered_vt_output())
                            bridge.vt_flush();
                        if (serving_pending)
                            condrv_io::complete_io(_server, msg.complete);
                        else
                        {
                            prev_done = &msg;
                            cur = (cur == &msgA) ? &msgB : &msgA;
                        }
                    }
                    else
                    {
                        // handler 只建立 pending；在这里推进它。完成时 completion
                        // 直接提交，不由本轮 READ_IO 再次携带。
                        if (bridge.pump_pending())
                        {
                            if (bridge.has_buffered_vt_output())
                                bridge.vt_flush();
                        }
                        else if (serving_pending)
                        {
                            // 同一时间只允许一个 pending；scratch 已在服务它，
                            // 再出现一个说明状态机被破坏。
                            LOG("message_router::run: unexpected second pending request");
                            win32::throw_last_error(win32::error::invalid_state);
                        }
                    }
                }

                pending = bridge.has_pending();

                if (bridge.should_flush_vt_output())
                    bridge.vt_flush();

                bridge.take_ctrl_events(ctrl_events);

                if (bridge.should_exit())
                {
                    LOG("message_router::run: session exit requested");
                    exit_loop = true;
                }
            }
        }

        deliver_ctrl_events(bridge, ctrl_events);




        if (exit_loop)
            break;

        if (wait_for_any)
        {
            const auto wait = win32::wait_any(_input_available, _window->close_requested(), _window->show_event(),
                                              _window->hide_event(), _state_changed.view(), INFINITE);
            if (wait.abandoned())
                win32::throw_last_error(win32::error::invalid_state);
            if (wait.index == 1)
            {
                LOG("message_router::run: close requested by window");
                break;
            }
            if (wait.index == 2)
                on_window_visibility(true);
            else if (wait.index == 3)
                on_window_visibility(false);
        }
    }

    // 退出前提交可能残留的 completion、控制事件和输出。
    {
        std::unique_lock guard{_mutex};
        if (prev_done != nullptr && condrv_io::completion_matches_descriptor(*prev_done))
            condrv_io::complete_io(_server, prev_done->complete);
        bridge.vt_flush();
    }
    LOG("message_router::run: exit");
}

inline void message_router::on_input_bytes(std::span<const std::byte> bytes)
{
    std::vector<std::pair<DWORD, DWORD>> ctrl_events;
    {
        std::unique_lock guard{_mutex};
        bridge.on_input_bytes(bytes);
        bridge.pump_pending();
        bridge.take_ctrl_events(ctrl_events);
    }
    deliver_ctrl_events(bridge, ctrl_events);
    signal_state();
}

inline void message_router::on_input_eof()
{
    LOG("message_router::on_input_eof");
    if (_client_process.valid())
    {
        DWORD exit_code = 0;
        if (::GetExitCodeProcess(_client_process.get(), &exit_code) != FALSE)
            LOG("message_router::on_input_eof: client stillActive=%d exitCode=0x%08lx",
                exit_code == STILL_ACTIVE ? 1 : 0, static_cast<unsigned long>(exit_code));
    }
    if (_window != nullptr)
    {
        const HWND owner = reinterpret_cast<HWND>(::GetWindowLongPtrW(_window->hwnd(), GWLP_HWNDPARENT));
        DWORD owner_pid = 0;
        if (owner != nullptr)
            ::GetWindowThreadProcessId(owner, &owner_pid);
        LOG("message_router::on_input_eof: owner=%p pid=%lu isWindow=%d", owner, owner_pid,
            owner != nullptr && ::IsWindow(owner) != FALSE);
        if (owner_pid != 0)
        {
            win32::handle owner_process{::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, owner_pid)};
            if (owner_process.valid())
            {
                DWORD owner_exit = 0;
                if (::GetExitCodeProcess(owner_process.get(), &owner_exit) != FALSE)
                    LOG("message_router::on_input_eof: owner stillActive=%d exitCode=0x%08lx",
                        owner_exit == STILL_ACTIVE ? 1 : 0, static_cast<unsigned long>(owner_exit));
            }
        }
    }
    std::vector<std::pair<DWORD, DWORD>> ctrl_events;
    {
        std::unique_lock guard{_mutex};
        bridge.on_input_eof();
        bridge.pump_pending();
        bridge.take_ctrl_events(ctrl_events);
    }
    deliver_ctrl_events(bridge, ctrl_events);
    signal_state();
}

inline void message_router::on_pty_signal(unsigned short id, std::span<const std::byte> payload)
{
    LOG("message_router::on_pty_signal: id=%u payload=%zu", id, payload.size());
    assert(_window != nullptr);
    std::vector<std::pair<DWORD, DWORD>> ctrl_events;
    {
        std::unique_lock guard{_mutex};
        const auto expected = pty_signal_reader::payload_size_for(id);
        if (expected == 0 || payload.size() != expected)
            win32::throw_last_error(win32::error::invalid_state);

        bool apply_state = true;
        if (!_console_connected)
        {
            if (id == static_cast<unsigned short>(PtySignal::ShowHideWindow))
            {
                unsigned short show = 0;
                std::memcpy(&show, payload.data(), sizeof(show));
                _initial_visibility = show != 0;
                apply_state = false;
            }
            else if (id == static_cast<unsigned short>(PtySignal::ResizeWindow))
            {
                COORD size{};
                std::memcpy(&size, payload.data(), sizeof(size));
                _early_resize = size;
                apply_state = false;
            }
            else if (id == static_cast<unsigned short>(PtySignal::ClearBuffer))
            {
                apply_state = false;
            }
        }

        if (apply_state)
            bridge.signal_reader().apply(id, payload);

        if (id == static_cast<unsigned short>(PtySignal::ShowHideWindow) && _console_connected)
        {
            unsigned short show = 0;
            std::memcpy(&show, payload.data(), sizeof(show));
            _window->set_visibility(show != 0);
        }
        else if (id == static_cast<unsigned short>(PtySignal::SetParent))
        {
            std::uint64_t owner = 0;
            std::memcpy(&owner, payload.data(), sizeof(owner));
            _window->set_owner(reinterpret_cast<HWND>(owner));
        }
        if (id == static_cast<unsigned short>(PtySignal::ResizeWindow) && _console_connected)
            bridge.queue_window_buffer_size_event();

        bridge.take_ctrl_events(ctrl_events);
    }
    deliver_ctrl_events(bridge, ctrl_events);
    signal_state();
}

inline void message_router::on_signal_closed()
{
    LOG("message_router::on_signal_closed");
    std::vector<std::pair<DWORD, DWORD>> ctrl_events;
    {
        std::unique_lock guard{_mutex};
        bridge.on_signal_closed();
        bridge.pump_pending();
        bridge.take_ctrl_events(ctrl_events);
    }
    deliver_ctrl_events(bridge, ctrl_events);
    signal_state();
}

inline void message_router::on_window_visibility(bool visible)
{
    LOG("message_router::on_window_visibility: visible=%d", visible ? 1 : 0);
    std::unique_lock guard{_mutex};
    bridge.vt_append_str(visible ? "\x1b[1t"sv : "\x1b[2t"sv);
    bridge.vt_flush();
}

} // namespace corehost::conpty