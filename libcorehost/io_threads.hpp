// ── conpty/io_threads.hpp ─────────────────────────────────
// 输入源线程：终端输入与 WT 信号管道。
//
// 终端输入有两种完全独立的实现：
// - overlapped_vt_input_thread：WT 的异步 duplex 管道，OVERLAPPED ReadFile；
// - blocking_vt_input_thread：同步 CreatePipe，阻塞 ReadFile + 取消协议。
//
// 每个线程只调用一次 message_router 入口；会话锁和批处理都在
// message_router 内完成，线程本身不持有锁/状态。
//
// 退出：事件驱动的停止协议 + 无限 join，不使用任何超时。
#pragma once
#include <windows.h>
#include <array>
#include <cassert>
#include <cstddef>
#include <cstring>
#include <memory>
#include <span>
#include "message_router.hpp"
#include "win32/cancellable_read.hpp"
#include "win32/error.hpp"
#include "win32/handle.hpp"
#include "win32/io.hpp"
#include "win32/thread.hpp"
#include "utility/log.hpp"

namespace corehost::conpty
{

// 终端输入线程的公共接口。两种句柄模式的生命周期入口相同，但读循环完全分离。
class vt_input_thread
{
  public:
    virtual ~vt_input_thread() = default;
    virtual void start() = 0;
    virtual void stop_and_join() = 0;
};

// WT 异步输入：OVERLAPPED ReadFile + completion event + 停止事件。
class overlapped_vt_input_thread final : public vt_input_thread
{
  public:
    overlapped_vt_input_thread(win32::handle_view vt_in, message_router &router)
        : _vt_in{vt_in}, _router{router},
          _stop{win32::create_tag, /*manual_reset=*/true, /*initial=*/false}
    {
    }

    overlapped_vt_input_thread(const overlapped_vt_input_thread &) = delete;
    overlapped_vt_input_thread &operator=(const overlapped_vt_input_thread &) = delete;

    void start() override
    {
        _thread = win32::basic_thread{&overlapped_vt_input_thread::thread_entry, this,
                                      {.name = L"corehost-input"}};
    }

    void stop_and_join() override
    {
        if (!_thread.valid())
            return;
        _stop.set();
        _thread.join();
    }

  private:
    static DWORD WINAPI thread_entry(LPVOID self)
    {
        static_cast<overlapped_vt_input_thread *>(self)->run();
        return 0;
    }

    void run()
    {
        LOG("vt_input_thread: enter (overlapped)");

        win32::event io_event{win32::create_tag, /*manual_reset=*/true, /*initial=*/false};
        OVERLAPPED overlapped{};
        overlapped.hEvent = io_event.get();

        std::array<std::byte, 64 * 1024> staging{};
        for (;;)
        {
            ::ResetEvent(io_event.get());
            DWORD read = 0;
            const BOOL started = ::ReadFile(_vt_in.get(), staging.data(),
                                            static_cast<DWORD>(staging.size()), &read, &overlapped);
            if (started == FALSE)
            {
                const auto err = win32::get_last_error();
                if (err != win32::error::io_pending)
                {
                    if (err == win32::error::broken_pipe || err == win32::error::pipe_not_connected ||
                        err == win32::error::no_data)
                    {
                        LOG("vt_input_thread: vt_in closed, mark EOF");
                        _router.on_input_eof();
                        return;
                    }
                    win32::throw_last_error(err);
                }

                const HANDLE waits[2] = {io_event.get(), _stop.get()};
                const DWORD wait = ::WaitForMultipleObjects(2, waits, FALSE, INFINITE);
                if (wait == WAIT_OBJECT_0 + 1)
                {
                    (void)::CancelIoEx(_vt_in.get(), &overlapped);
                    (void)::WaitForSingleObject(io_event.get(), INFINITE);
                    return;
                }
                if (wait != WAIT_OBJECT_0)
                    win32::throw_last_error();

                DWORD bytes = 0;
                if (::GetOverlappedResult(_vt_in.get(), &overlapped, &bytes, FALSE) == FALSE)
                {
                    const auto result_err = win32::get_last_error();
                    if (result_err == win32::error::operation_aborted)
                        return;
                    if (result_err == win32::error::broken_pipe || result_err == win32::error::pipe_not_connected ||
                        result_err == win32::error::no_data)
                    {
                        LOG("vt_input_thread: vt_in closed, mark EOF");
                        _router.on_input_eof();
                        return;
                    }
                    win32::throw_last_error(result_err);
                }
                read = bytes;
            }

            if (read == 0)
            {
                LOG("vt_input_thread: vt_in closed, mark EOF");
                _router.on_input_eof();
                return;
            }
            _router.on_input_bytes(std::span<const std::byte>{staging.data(), read});
        }
    }

    win32::handle_view _vt_in;
    message_router &_router;
    win32::event _stop;
    win32::basic_thread _thread;
};

// 同步输入：阻塞 ReadFile + 事件驱动取消。
class blocking_vt_input_thread final : public vt_input_thread
{
  public:
    blocking_vt_input_thread(win32::handle_view vt_in, message_router &router) : _reader{vt_in}, _router{router}
    {
    }

    blocking_vt_input_thread(const blocking_vt_input_thread &) = delete;
    blocking_vt_input_thread &operator=(const blocking_vt_input_thread &) = delete;

    void start() override
    {
        _thread = win32::basic_thread{&blocking_vt_input_thread::thread_entry, this,
                                      {.name = L"corehost-input"}};
    }

    void stop_and_join() override
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
        static_cast<blocking_vt_input_thread *>(self)->run();
        return 0;
    }

    void run()
    {
        LOG("vt_input_thread: enter (blocking)");

        std::array<std::byte, 64 * 1024> staging{};
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
                _router.on_input_eof();
                return;
            }
            if (read != 0)
                _router.on_input_bytes(std::span<const std::byte>{staging.data(), read});
        }
    }

    win32::cancellable_read _reader;
    message_router &_router;
    win32::basic_thread _thread;
};

// 按句柄模式选择实现。WT 的句柄由 winconpty 创建为异步模式；测试/defterm
// 的普通管道仍走同步实现。
[[nodiscard]] inline std::unique_ptr<vt_input_thread> make_vt_input_thread(win32::handle_view vt_in,
                                                                           message_router &router)
{
    if (win32::handle_is_async(vt_in))
        return std::make_unique<overlapped_vt_input_thread>(vt_in, router);
    return std::make_unique<blocking_vt_input_thread>(vt_in, router);
}

// WT 信号管道线程。读取 id 与 payload 后交给 message_router 处理。
class pty_signal_thread
{
  public:
    pty_signal_thread(win32::handle_view pipe, message_router &router) : _reader{pipe}, _router{router}
    {
    }

    pty_signal_thread(const pty_signal_thread &) = delete;
    pty_signal_thread &operator=(const pty_signal_thread &) = delete;

    void start()
    {
        _thread = win32::basic_thread{&pty_signal_thread::thread_entry, this, {.name = L"corehost-signal"}};
    }

    void stop_and_join()
    {
        assert(_thread.valid());
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

        unsigned short id = 0;
        std::array<std::byte, max_signal_payload_size> payload{};

        for (;;)
        {
            DWORD read = 0;
            auto status = _reader.read_exact(std::as_writable_bytes(std::span{&id, 1}), read);
            if (status == win32::cancellable_read::status::cancelled)
                return;
            if (status == win32::cancellable_read::status::closed)
            {
                if (read != 0)
                    win32::throw_last_error(win32::error::invalid_state);
                _router.on_signal_closed();
                return;
            }
            if (status != win32::cancellable_read::status::data)
                win32::throw_last_error(win32::error::invalid_state);

            const auto payload_size = pty_signal_reader::payload_size_for(id);
            if (payload_size == 0 || payload_size > payload.size())
                win32::throw_last_error(win32::error::invalid_state);

            status = _reader.read_exact(std::span{payload}.first(payload_size), read);
            if (status == win32::cancellable_read::status::cancelled)
                return;
            if (status != win32::cancellable_read::status::data)
                win32::throw_last_error(win32::error::invalid_state);

            _router.on_pty_signal(id, std::span{payload}.first(payload_size));
        }
    }

    win32::cancellable_read _reader;
    message_router &_router;
    win32::basic_thread _thread;
};

} // namespace corehost::conpty
