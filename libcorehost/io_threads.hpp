// ── conpty/io_threads.hpp ─────────────────────────────────
// 输入源线程：终端输入与 WT 信号管道。
//
// 输入线程与信号线程都阻塞在各自管道上，只调用一次 message_router 入口；
// 会话状态锁和批处理都在 message_router 内完成，线程本身不持有锁/状态。
//
// 退出：request_stop + 事件驱动的 wait_for_exit，再无限 join。
#pragma once
#include <windows.h>
#include <array>
#include <cassert>
#include <cstddef>
#include <cstring>
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

// 终端输入线程。
class vt_input_thread
{
  public:
    vt_input_thread(win32::handle_view vt_in, message_router &router)
        : _vt_in{vt_in}, _router{router}, _async{win32::handle_is_async(vt_in)},
          _stop{win32::create_tag, /*manual_reset=*/true, /*initial=*/false}, _reader{vt_in}
    {
    }

    vt_input_thread(const vt_input_thread &) = delete;
    vt_input_thread &operator=(const vt_input_thread &) = delete;

    void start()
    {
        _thread = win32::basic_thread{&vt_input_thread::thread_entry, this, {.name = L"corehost-input"}};
    }

    void stop_and_join()
    {
        if (!_thread.valid())
            return;
        if (_async)
        {
            _stop.set();
            _thread.join();
            return;
        }
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
        if (_async)
            run_overlapped();
        else
            run_blocking();
    }

    // WT 的异步 duplex 管道使用 OVERLAPPED ReadFile；没有数据时等待
    // completion event，不轮询。停止时 CancelIoEx 打断唯一的挂起读。
    void run_overlapped()
    {
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

    // 普通 CreatePipe 的同步句柄走阻塞 ReadFile + 事件驱动取消。
    void run_blocking()
    {
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

    win32::handle_view _vt_in;
    message_router &_router;
    bool _async = false;
    win32::event _stop;
    win32::cancellable_read _reader;
    win32::basic_thread _thread;
};
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