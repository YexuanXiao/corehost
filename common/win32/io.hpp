#pragma once

#include "win32/error.hpp"
#include "win32/handle.hpp"

#include <Windows.h>
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <span>
#include <type_traits>

namespace win32
{

enum class io_status
{
    success,
    empty,
    closed,
    failed,
};

struct io_result
{
    io_status status{};
    size_t bytes{};
    win32::error error{};

    [[nodiscard]] bool success() const noexcept
    {
        return status == io_status::success;
    }

    [[nodiscard]] bool empty() const noexcept
    {
        return status == io_status::empty;
    }

    [[nodiscard]] bool closed() const noexcept
    {
        return status == io_status::closed;
    }

    [[nodiscard]] bool failed() const noexcept
    {
        return status == io_status::failed;
    }
};

template <typename T>
concept byte_like = sizeof(T) == 1 && std::is_trivially_copyable_v<T>;

[[nodiscard]] inline bool is_pipe_closed_error(win32::error err) noexcept
{
    return err == win32::error::broken_pipe || err == win32::error::pipe_not_connected || err == win32::error::no_data;
}

[[nodiscard]] inline io_result failed_io_result(win32::error err) noexcept
{
    if (is_pipe_closed_error(err))
        return {io_status::closed, 0, err};
    return {io_status::failed, 0, err};
}

[[nodiscard]] inline io_result successful_io_result(size_t bytes) noexcept
{
    return {bytes == 0 ? io_status::empty : io_status::success, bytes, win32::error::success};
}

template <byte_like T>
[[nodiscard]] io_result read_some(win32::handle_view handle, std::span<T> buffer) noexcept
{
    if (buffer.empty())
        return {io_status::empty, 0, win32::error::success};

    const auto bytes_to_read = std::min<size_t>(buffer.size(), static_cast<size_t>(MAXDWORD));
    DWORD bytes_read = 0;
    if (::ReadFile(handle.get(), buffer.data(), static_cast<DWORD>(bytes_to_read), &bytes_read, nullptr))
        return successful_io_result(bytes_read);

    return failed_io_result(win32::get_last_error());
}

template <byte_like T>
[[nodiscard]] io_result write_some(win32::handle_view handle, std::span<const T> buffer) noexcept
{
    if (buffer.empty())
        return {io_status::empty, 0, win32::error::success};

    const auto bytes_to_write = std::min<size_t>(buffer.size(), static_cast<size_t>(MAXDWORD));
    DWORD bytes_written = 0;
    if (::WriteFile(handle.get(), buffer.data(), static_cast<DWORD>(bytes_to_write), &bytes_written, nullptr))
        return successful_io_result(bytes_written);

    return failed_io_result(win32::get_last_error());
}

template <byte_like T>
[[nodiscard]] io_result peek_named_pipe(win32::handle_view pipe, std::span<T> buffer, DWORD &bytes_read,
                                        DWORD &bytes_available) noexcept
{
    bytes_read = 0;
    bytes_available = 0;

    const auto bytes_to_peek = std::min<size_t>(buffer.size(), static_cast<size_t>(MAXDWORD));
    if (::PeekNamedPipe(pipe.get(), buffer.data(), static_cast<DWORD>(bytes_to_peek), &bytes_read, &bytes_available,
                        nullptr))
    {
        return successful_io_result(bytes_read);
    }

    return failed_io_result(win32::get_last_error());
}

[[nodiscard]] inline io_result peek_named_pipe(win32::handle_view pipe, DWORD &bytes_available) noexcept
{
    DWORD bytes_read = 0;
    return peek_named_pipe(pipe, std::span<std::byte>{}, bytes_read, bytes_available);
}

template <byte_like T>
[[nodiscard]] io_result write_all(win32::handle_view handle, std::span<const T> buffer) noexcept
{
    size_t total_written = 0;
    while (!buffer.empty())
    {
        const auto result = write_some(handle, buffer);
        if (!result.success())
            return {result.status, total_written, result.error};

        total_written += result.bytes;
        buffer = buffer.subspan(result.bytes);
    }
    return successful_io_result(total_written);
}

} // namespace win32
