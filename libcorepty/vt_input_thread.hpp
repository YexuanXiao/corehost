#pragma once

#include "win32/cancellable_read.hpp"
#include "win32/event.hpp"
#include "win32/handle.hpp"
#include "win32/io.hpp"
#include "win32/thread.hpp"

#include <Windows.h>

#include <array>
#include <cstddef>
#include <span>

namespace corehost::conptyio
{

// the byte stream; parsing and console state stay on the session side.
// Blocking terminal-input thread.
// Asynchronous pipes use OVERLAPPED reads and a stop event; synchronous pipes
// use cancellable_read. Neither path polls and neither path uses a timeout.
template <typename Consumer>
class vt_input_thread final
{
  public:
    vt_input_thread(win32::handle_view vt_in, Consumer &consumer)
        : vt_in_{vt_in}, consumer_{consumer}, is_overlapped_{win32::handle_is_async(vt_in)},
          stop_{win32::create_tag, true, false}, reader_{vt_in}
    {
    }

    vt_input_thread(const vt_input_thread &) = delete;
    vt_input_thread &operator=(const vt_input_thread &) = delete;

    void start()
    {
        thread_ = win32::basic_thread{&vt_input_thread::thread_entry, this, {.name = L"corehost-vt-input"}};
    }

    [[nodiscard]] win32::handle_view view() const noexcept
    {
        return thread_.view();
    }

    void join()
    {
        if (thread_.valid())
        {
            thread_.join();
        }
    }

    void stop_and_join()
    {
        if (!thread_.valid())
        {
            return;
        }

        if (is_overlapped_)
        {
            stop_.set();
            thread_.join();
            return;
        }

        reader_.request_stop();
        reader_.wait_for_exit(thread_);
        thread_.join();
    }

  private:
    static DWORD WINAPI thread_entry(LPVOID self)
    {
        static_cast<vt_input_thread *>(self)->run();
        return 0;
    }

    void run()
    {
        if (is_overlapped_)
        {
            run_overlapped();
        }
        else
        {
            run_synchronous();
        }
    }

    void run_overlapped()
    {
        win32::event io_event{win32::create_tag, true, false};
        OVERLAPPED overlapped{};
        overlapped.hEvent = io_event.get();

        for (;;)
        {
            (void)::ResetEvent(io_event.get());
            DWORD read = 0;
            const BOOL started =
                ::ReadFile(vt_in_.get(), staging_.data(), static_cast<DWORD>(staging_.size()), &read, &overlapped);
            if (started == FALSE)
            {
                const auto error = win32::get_last_error();
                if (error != win32::error::io_pending)
                {
                    if (win32::is_pipe_closed_error(error))
                    {
                        consumer_.on_input_eof();
                        return;
                    }
                    win32::throw_last_error(error);
                }

                const HANDLE waits[2] = {io_event.get(), stop_.get()};
                const auto wait = ::WaitForMultipleObjects(2, waits, FALSE, INFINITE);
                if (wait == WAIT_OBJECT_0 + 1)
                {
                    (void)::CancelIoEx(vt_in_.get(), &overlapped);
                    (void)::WaitForSingleObject(io_event.get(), INFINITE);
                    return;
                }
                if (wait != WAIT_OBJECT_0)
                {
                    win32::throw_last_error();
                }

                DWORD bytes = 0;
                if (::GetOverlappedResult(vt_in_.get(), &overlapped, &bytes, FALSE) == FALSE)
                {
                    const auto result_error = win32::get_last_error();
                    if (result_error == win32::error::operation_aborted)
                    {
                        return;
                    }
                    if (win32::is_pipe_closed_error(result_error))
                    {
                        consumer_.on_input_eof();
                        return;
                    }
                    win32::throw_last_error(result_error);
                }
                read = bytes;
            }

            if (read == 0)
            {
                consumer_.on_input_eof();
                return;
            }

            consumer_.on_input_bytes(std::span<const std::byte>{staging_.data(), static_cast<std::size_t>(read)});
        }
    }

    void run_synchronous()
    {
        for (;;)
        {
            DWORD read = 0;
            const auto status = reader_.read(staging_, read);
            if (status == win32::cancellable_read::status::cancelled)
            {
                return;
            }
            if (status == win32::cancellable_read::status::closed)
            {
                consumer_.on_input_eof();
                return;
            }
            if (read != 0)
            {
                consumer_.on_input_bytes(std::span<const std::byte>{staging_.data(), static_cast<std::size_t>(read)});
            }
        }
    }

    win32::handle_view vt_in_;
    Consumer &consumer_;
    bool is_overlapped_{};
    win32::event stop_;
    win32::cancellable_read reader_;
    win32::basic_thread thread_;
    std::array<std::byte, 64 * 1024> staging_{};
};

} // namespace corehost::conptyio