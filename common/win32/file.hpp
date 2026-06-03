#pragma once
#include <type_traits>
#include <windows.h>
#include "win32/error.hpp"
#include "win32/handle.hpp"
#include "win32/io.hpp"
#include <span>
#include <string>

namespace win32
{
inline unsigned long long get_size(win32::handle_view file)
{
    LARGE_INTEGER file_size{};
    auto res = ::GetFileSizeEx(file.get(), &file_size);
    win32::throw_last_error(res == 0);
    return file_size.QuadPart;
}
template <typename T>
    requires std::is_scalar_v<T>
inline void read(win32::handle_view file, std::span<T> buffer)
{
    auto bytes = std::as_writable_bytes(buffer);
    while (!bytes.empty())
    {
        const auto result = win32::read_some(file, bytes);
        if (result.failed() || result.closed())
            win32::throw_last_error(result.error);
        win32::throw_last_error(result.empty(), win32::error::invalid_data);
        bytes = bytes.subspan(result.bytes);
    }
}
[[nodiscard]] inline bool is_missing_file_error(win32::error error) noexcept
{
    return error == win32::error::file_not_found || error == win32::error::path_not_found;
}
[[nodiscard]] inline std::wstring concat_path(std::wstring base, const std::wstring_view component) noexcept
{
    if (!base.empty())
    {
        const wchar_t tail = base.back();
        if (tail != L'\\' && tail != L'/')
            base.push_back(L'\\');
    }
    base.append(component);
    return base;
}

[[nodiscard]] inline bool is_pipe_like_handle(const handle_view handle) noexcept
{
    if (!handle)
    {
        return false;
    }
    if (auto type = ::GetFileType(handle.get()); type != FILE_TYPE_UNKNOWN)
    {
        return type == FILE_TYPE_PIPE;
    }
    win32::throw_last_error();
}

} // namespace win32
