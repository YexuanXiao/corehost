// ── conpty/io_threads.hpp ─────────────────────────────────
// 输入源线程：终端输入与 WT 信号管道。
//
// 输入线程用 PeekNamedPipe 判断可读后 ReadFile；信号线程阻塞读取，只调用一次 message_router 入口；
// 会话状态锁和批处理都在 message_router 内完成，线程本身不持有锁/状态。
//
// 退出：信号线程 request_stop + wait_for_exit；输入线程 _stop + join。
// 输入线程的 16ms 等待只用于 PeekNamedPipe 空闲节流；停止由 _stop 保证。
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
#include "win32/thread.hpp"
#include "utility/log.hpp"

namespace corehost::conpty
{

// 终端输入线程。
class vt_input_thread
{
  public:
    vt_input_thread(win32::handle_view vt_in, message_router &router)
        : _vt_in{vt_in}, _stop{win32::create_tag, /*manual_reset=*/true, /*initial=*/false}, _router{router}
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
        _stop.set();
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

        std::array<std::byte, 64 * 1024> staging{};
        for (;;)
        {
            if (::WaitForSingleObject(_stop.get(), 0) == WAIT_OBJECT_0)
                return;

            DWORD available = 0;
            if (::PeekNamedPipe(_vt_in.get(), nullptr, 0, nullptr, &available, nullptr) == FALSE)
            {
                const auto err = win32::get_last_error();
                if (err == win32::error::broken_pipe || err == win32::error::pipe_not_connected ||
                    err == win32::error::no_data)
                {
                    LOG("vt_input_thread: vt_in closed, mark EOF");
                    _router.on_input_eof();
                    return;
                }
                if (err == win32::error::invalid_state)
                {
                    // WT 的终端管道是异步 duplex；高频输入时 PeekNamedPipe/
                    // ReadFile 偶尔会返回 ERROR_INVALID_STATE，不能当作致命错误。
                    (void)::WaitForSingleObject(_stop.get(), 16);
                    continue;
                }
                win32::throw_last_error(err);
            }

            if (available == 0)
            {
                (void)::WaitForSingleObject(_stop.get(), 16);
                continue;
            }

            DWORD read = 0;
            if (::ReadFile(_vt_in.get(), staging.data(),
                           static_cast<DWORD>(std::min<size_t>(available, staging.size())), &read, nullptr) == FALSE)
            {
                const auto err = win32::get_last_error();
                if (err == win32::error::broken_pipe || err == win32::error::pipe_not_connected ||
                    err == win32::error::no_data)
                {
                    LOG("vt_input_thread: vt_in closed, mark EOF");
                    _router.on_input_eof();
                    return;
                }
                if (err == win32::error::invalid_state)
                {
                    // WT 的终端管道是异步 duplex；高频输入时 PeekNamedPipe/
                    // ReadFile 偶尔会返回 ERROR_INVALID_STATE，不能当作致命错误。
                    (void)::WaitForSingleObject(_stop.get(), 16);
                    continue;
                }
                win32::throw_last_error(err);
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
    win32::event _stop;
    message_router &_router;
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