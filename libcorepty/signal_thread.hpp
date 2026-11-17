#pragma once

#include "signal.hpp"

#include "utility/log.hpp"
#include "win32/cancellable_read.hpp"
#include "win32/handle.hpp"
#include "win32/thread.hpp"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <span>
#include <type_traits>

namespace corehost::conptyio
{

// Blocking PtySignal reader thread:
// a cancellable blocking read, an explicit stop request, and an infinite join.
template <typename Consumer>
class pty_signal_thread final
{
  public:
    pty_signal_thread(win32::handle_view pipe, Consumer &consumer) : consumer_{consumer}, reader_{pipe}
    {
    }

    pty_signal_thread(const pty_signal_thread &) = delete;
    pty_signal_thread &operator=(const pty_signal_thread &) = delete;

    void start()
    {
        thread_ = win32::basic_thread{&pty_signal_thread::thread_entry, this, {.name = L"corehost-pty-signal"}};
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
            DWORD id_bytes = 0;
            const auto id_status =
                read_exact(std::span<std::byte>{reinterpret_cast<std::byte *>(&id), sizeof(id)}, id_bytes);
            if (id_status == win32::cancellable_read::status::cancelled)
            {
                return;
            }
            if (id_status == win32::cancellable_read::status::closed)
            {
                if (id_bytes != 0)
                {
                    LOG("pty_signal_thread: truncated PtySignal id");
                    return;
                }
                consumer_.on_signal_eof();
                return;
            }
            assert(id_status == win32::cancellable_read::status::data);
            assert(id_bytes == sizeof(id));

            switch (static_cast<pty_signal>(id))
            {
            case pty_signal::show_hide_window:
            case pty_signal::clear_buffer: {
                std::uint16_t payload = 0;
                if (!read_payload(payload))
                {
                    return;
                }
                consumer_.on_signal(id, std::as_bytes(std::span{&payload, 1}));
                break;
            }
            case pty_signal::resize_window: {
                std::uint32_t payload = 0;
                if (!read_payload(payload))
                {
                    return;
                }
                consumer_.on_signal(id, std::as_bytes(std::span{&payload, 1}));
                break;
            }
            case pty_signal::set_parent: {
                std::uint64_t payload = 0;
                if (!read_payload(payload))
                {
                    return;
                }
                consumer_.on_signal(id, std::as_bytes(std::span{&payload, 1}));
                break;
            }
            default:
                LOG("pty_signal_thread: invalid PtySignal id %u", static_cast<unsigned>(id));
                return;
            }
        }
    }

    template <typename T>
    [[nodiscard]] bool read_payload(T &payload)
    {
        static_assert(std::is_trivially_copyable_v<T>);
        DWORD bytes = 0;
        const auto status =
            read_exact(std::span<std::byte>{reinterpret_cast<std::byte *>(&payload), sizeof(payload)}, bytes);
        if (status == win32::cancellable_read::status::cancelled)
        {
            return false;
        }
        if (status != win32::cancellable_read::status::data)
        {
            LOG("pty_signal_thread: truncated PtySignal payload");
            return false;
        }
        assert(bytes == sizeof(payload));
        return true;
    }

    [[nodiscard]] win32::cancellable_read::status read_exact(std::span<std::byte> destination, DWORD &bytes)
    {
        return reader_.read_exact(destination, bytes);
    }

    Consumer &consumer_;
    win32::cancellable_read reader_;
    win32::basic_thread thread_;
};

} // namespace corehost::conptyio