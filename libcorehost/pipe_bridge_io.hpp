#pragma once
#include <windows.h>
#include <algorithm>
#include <span>
#include "win32/handle.hpp"
#include "miniio/io_thread.hpp"
#include "utility/log.hpp"

namespace conpty
{

enum class vt_pipe_read_status
{
    bytes,
    empty,
    eof,
};

class pipe_bridge_io
{
  public:
    void set_server(win32::handle_view server) noexcept
    {
        _server = server;
    }

    void set_vt_input(win32::handle_view pipe) noexcept
    {
        _vt_input = pipe;
    }

    void set_shutdown_event(win32::handle_view event) noexcept
    {
        _shutdown_event = event;
    }

    void complete(CD_IO_COMPLETE &completion) const
    {
        miniio::complete_io(_server, completion);
    }

    [[nodiscard]] bool has_shutdown_event() const noexcept
    {
        return _shutdown_event.valid();
    }

    [[nodiscard]] bool shutdown_signaled() const noexcept
    {
        return _shutdown_event.valid() && ::WaitForSingleObject(_shutdown_event.get(), 0) == WAIT_OBJECT_0;
    }

    [[nodiscard]] bool wait_shutdown_slice(DWORD timeout_ms) const noexcept
    {
        return _shutdown_event.valid() && ::WaitForSingleObject(_shutdown_event.get(), timeout_ms) == WAIT_OBJECT_0;
    }

    [[nodiscard]] bool peek_available(DWORD &available) const noexcept
    {
        available = 0;
        if (::PeekNamedPipe(_vt_input.get(), nullptr, 0, nullptr, &available, nullptr))
            return true;

        LOG("[bridge_io] PeekNamedPipe failed err=%lu", ::GetLastError());
        return false;
    }

    [[nodiscard]] vt_pipe_read_status read_available(std::span<BYTE> destination, DWORD &read_bytes) noexcept
    {
        read_bytes = 0;

        DWORD available = 0;
        if (!peek_available(available))
            return vt_pipe_read_status::eof;
        if (available == 0)
            return vt_pipe_read_status::empty;

        const auto to_read = std::min<DWORD>(available, static_cast<DWORD>(destination.size()));
        return read_from_vt_input(destination.first(to_read), read_bytes, "read_available");
    }

    [[nodiscard]] vt_pipe_read_status read_blocking(std::span<BYTE> destination, DWORD &read_bytes) noexcept
    {
        read_bytes = 0;
        return read_from_vt_input(destination, read_bytes, "read_blocking");
    }

    [[nodiscard]] bool try_consume_byte(BYTE expected, BYTE &consumed) noexcept
    {
        consumed = 0;

        BYTE next = 0;
        DWORD peeked = 0;
        if (!::PeekNamedPipe(_vt_input.get(), &next, sizeof(next), &peeked, nullptr, nullptr) || peeked == 0 ||
            next != expected)
        {
            return false;
        }

        DWORD read = 0;
        if (!::ReadFile(_vt_input.get(), &next, sizeof(next), &read, nullptr) || read != sizeof(next))
            return false;

        consumed = next;
        return true;
    }

  private:
    [[nodiscard]] vt_pipe_read_status read_from_vt_input(std::span<BYTE> destination, DWORD &read_bytes,
                                                         const char *operation) noexcept
    {
        if (destination.empty())
            return vt_pipe_read_status::empty;

        if (!::ReadFile(_vt_input.get(), destination.data(), static_cast<DWORD>(destination.size()), &read_bytes,
                        nullptr) ||
            read_bytes == 0)
        {
            LOG("[bridge_io] %s failed read=%lu err=%lu", operation, read_bytes, ::GetLastError());
            return vt_pipe_read_status::eof;
        }

        return vt_pipe_read_status::bytes;
    }

    win32::handle_view _server;
    win32::handle_view _vt_input;
    win32::handle_view _shutdown_event;
};

} // namespace conpty
