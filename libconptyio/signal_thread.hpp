#pragma once

#include "signal.hpp"

#include "win32/cancellable_read.hpp"
#include "win32/handle.hpp"
#include "win32/thread.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>

namespace corehost::conptyio
{

class signal_sink
{
  public:
    virtual ~signal_sink() = default;

    virtual void on_signal(std::uint16_t id, std::span<const std::byte> payload) = 0;
    virtual void on_signal_eof() = 0;
};

// Blocking PtySignal reader thread. Mirrors libcorehost's pty_signal_thread:
// a cancellable blocking read, an explicit stop request, and an infinite join.
class pty_signal_thread final
{
  public:
    pty_signal_thread(win32::handle_view pipe, signal_sink &sink)
        : sink_{sink}, reader_{pipe}
    {
    }

    pty_signal_thread(const pty_signal_thread &) = delete;
    pty_signal_thread &operator=(const pty_signal_thread &) = delete;

    void start()
    {
        thread_ = win32::basic_thread{&pty_signal_thread::thread_entry, this,
                                      {.name = L"corehost2-pty-signal"}};
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
        reader_.request_stop();
        reader_.wait_for_exit(thread_);
        thread_.join();
    }

  private:
    static DWORD WINAPI thread_entry(LPVOID self)
    {
        static_cast<pty_signal_thread *>(self)->run();
        return 0;
    }

    void run()
    {
        for (;;)
        {
            std::uint16_t id = 0;
            const auto id_status = read_exact(
                std::span<std::byte>{reinterpret_cast<std::byte *>(&id), sizeof(id)});
            if (id_status == win32::cancellable_read::status::cancelled)
            {
                return;
            }
            if (id_status == win32::cancellable_read::status::closed)
            {
                sink_.on_signal_eof();
                return;
            }

            const auto size = payload_size_for(id);
            if (size == 0 || size > payload_.size())
            {
                throw std::runtime_error{"invalid PtySignal id"};
            }

            const auto payload_status =
                read_exact(std::span<std::byte>{payload_.data(), size});
            if (payload_status == win32::cancellable_read::status::cancelled)
            {
                return;
            }
            if (payload_status == win32::cancellable_read::status::closed)
            {
                throw std::runtime_error{"truncated PtySignal payload"};
            }

            sink_.on_signal(id, std::span<const std::byte>{payload_.data(), size});
        }
    }

    [[nodiscard]] win32::cancellable_read::status read_exact(std::span<std::byte> destination)
    {
        DWORD bytes = 0;
        const auto status = reader_.read_exact(destination, bytes);
        if (status == win32::cancellable_read::status::data && bytes != destination.size())
        {
            throw std::runtime_error{"truncated PtySignal read"};
        }
        return status;
    }

    signal_sink &sink_;
    win32::cancellable_read reader_;
    win32::basic_thread thread_;
    std::array<std::byte, sizeof(std::uint64_t)> payload_{};
};

} // namespace corehost::conptyio
