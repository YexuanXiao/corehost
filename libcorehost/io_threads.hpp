// ── conpty/io_threads.hpp ─────────────────────────────────
// 输入源线程：终端输入与 WT 信号管道。
//
// 每个线程阻塞在自己的管道上，读到数据后获取会话锁、同步完成处理、释放锁，
// 最后置位 state_changed 事件告诉主线程"状态可能变了"。没有事件队列：
// 管道本身就是缓冲，线程处理完上一块才读下一块（见 doc/design/0001）。
//
// 退出：request_stop + 事件驱动的取消（cancellable_read::wait_for_exit），
// join 为无限等待；终止性由构造保证，不使用任何超时。
#pragma once
#include <windows.h>
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <span>
#include "pipe_bridge.hpp"
#include "signal.hpp"
#include "vt_input_state.hpp"
#include "win32/cancellable_read.hpp"
#include "win32/error.hpp"
#include "win32/event.hpp"
#include "win32/handle.hpp"
#include "win32/srwlock.hpp"
#include "win32/thread.hpp"
#include "utility/log.hpp"

namespace corehost::conpty
{

// 终端输入线程。
class vt_input_thread
{
  public:
    explicit vt_input_thread(win32::handle_view vt_in) : _reader{vt_in}
    {
    }

    vt_input_thread(const vt_input_thread &) = delete;
    vt_input_thread &operator=(const vt_input_thread &) = delete;

    // 输入状态必须在 bridge 构造前可用：bridge 只保存它的引用别名。
    [[nodiscard]] vt_input_state &state() noexcept
    {
        return _state;
    }

    // 绑定处理目标。必须在 start() 之前调用，参数生命周期覆盖线程。
    void bind(pipe_bridge &bridge, win32::shared_mutex &session_lock, win32::event &state_changed) noexcept
    {
        _bridge = &bridge;
        _session_lock = &session_lock;
        _state_changed = state_changed.view();
    }

    void start()
    {
        assert(_bridge != nullptr && _session_lock != nullptr && _state_changed.valid());
        _thread = win32::basic_thread{&vt_input_thread::thread_entry, this, {.name = L"corehost-input"}};
    }

    // 停止并无限等待线程退出。只允许在没有持有会话锁时调用。
    void stop_and_join()
    {
        if (!_thread.valid())
            return;
        _reader.request_stop();
        _reader.wait_for_exit(_thread);
        _thread.join();
    }

  private:
    static DWORD WINAPI thread_entry(LPVOID self)
    {
        static_cast<vt_input_thread *>(self)->run();
        return 0;
    }

    void run()
    {
        LOG("vt_input_thread: enter");

        std::array<std::byte, 4096> staging{};
        for (;;)
        {
            DWORD read = 0;
            const auto status = _reader.read(staging, read);
            if (status == win32::cancellable_read::status::cancelled)
            {
                LOG("vt_input_thread: cancelled, exit");
                return;
            }
            if (status == win32::cancellable_read::status::closed)
            {
                LOG("vt_input_thread: vt_in closed, mark EOF");
                {
                    std::unique_lock guard{*_session_lock};
                    _bridge->on_input_eof();
                }
                // 由输入线程在 EOF 时向仍存活的客户端投递 CTRL_CLOSE_EVENT。
                signal_state();
                return;
            }

            {
                std::unique_lock guard{*_session_lock};
                _bridge->on_input_bytes(std::span<const std::byte>{staging.data(), read});
            }
            // 控制事件在释放会话锁后投递：EndTask 可能阻塞到客户端处理器返回/
            // CSRSS 超时，不能占着会话锁（conhost 的 ProcessCtrlEvents 也在解锁后）。
            _bridge->deliver_ctrl_events();
            signal_state();
        }
    }

    win32::cancellable_read _reader;
    vt_input_state _state;

    pipe_bridge *_bridge = nullptr;
    win32::shared_mutex *_session_lock = nullptr;
    // 置位状态变化事件：只提示"状态可能变了"，不携带数据。
    void signal_state() const
    {
        if (::SetEvent(_state_changed.get()) == FALSE)
            win32::throw_last_error();
    }

    win32::handle_view _state_changed;
    win32::basic_thread _thread;
};

// WT 信号管道线程。
class pty_signal_thread
{
  public:
    pty_signal_thread(win32::handle_view pipe, pty_signal_reader &signals, pipe_bridge &bridge,
                      win32::shared_mutex &session_lock)
        : _reader{pipe}, _signals{signals}, _bridge{bridge}, _session_lock{session_lock}
    {
    }

    pty_signal_thread(const pty_signal_thread &) = delete;
    pty_signal_thread &operator=(const pty_signal_thread &) = delete;

    void bind(win32::event &state_changed) noexcept
    {
        _state_changed = state_changed.view();
    }

    void start()
    {
        _thread = win32::basic_thread{&pty_signal_thread::thread_entry, this, {.name = L"corehost-signal"}};
    }

    void stop_and_join()
    {
        if (!_thread.valid())
            return;
        _reader.request_stop();
        _reader.wait_for_exit(_thread);
        _thread.join();
    }

  private:
    static DWORD WINAPI thread_entry(LPVOID self)
    {
        static_cast<pty_signal_thread *>(self)->run();
        return 0;
    }

    void run()
    {
        LOG("pty_signal_thread: enter");

        // payload 最大是 ULONG_PTR（SetParent）；用固定缓冲接收。
        std::array<std::byte, sizeof(std::uintptr_t)> payload{};

        for (;;)
        {
            unsigned short id = 0;
            DWORD read = 0;
            auto status = _reader.read_exact(std::span{reinterpret_cast<std::byte *>(&id), sizeof(id)}, read);
            if (status == win32::cancellable_read::status::cancelled)
                return;
            if (status == win32::cancellable_read::status::closed)
            {
                if (read != 0)
                    win32::throw_last_error(win32::error::invalid_state);
                {
                    std::unique_lock guard{_session_lock};
                    _bridge.on_signal_closed();
                }
                signal_state();
                return;
            }

            const auto payload_size = pty_signal_reader::payload_size_for(id);
            if (payload_size == 0)
                win32::throw_last_error(win32::error::invalid_state);

            status = _reader.read_exact(std::span{payload}.first(payload_size), read);
            if (status == win32::cancellable_read::status::cancelled)
                return;
            if (status != win32::cancellable_read::status::data)
                win32::throw_last_error(win32::error::invalid_state);

            {
                std::unique_lock guard{_session_lock};
                _signals.apply(id, std::span{payload}.first(payload_size));
                // 终端 resize 后，VT 输入模式的客户端需要一条尺寸事件。
                if (id == static_cast<unsigned short>(PtySignal::ResizeWindow))
                    _bridge.queue_window_buffer_size_event();
            }
        }
    }

    win32::cancellable_read _reader;
    pty_signal_reader &_signals;
    pipe_bridge &_bridge;
    win32::shared_mutex &_session_lock;
    // 置位状态变化事件：只提示"状态可能变了"，不携带数据。
    void signal_state() const
    {
        if (::SetEvent(_state_changed.get()) == FALSE)
            win32::throw_last_error();
    }

    win32::handle_view _state_changed;
    win32::basic_thread _thread;
};

} // namespace corehost::conpty