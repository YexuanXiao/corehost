#pragma once

#include <windows.h>
#include <psapi.h>
#include <string>
#include "win32/error.hpp"
#include "win32/handle.hpp"

namespace win32
{

[[nodiscard]] inline std::wstring query_full_process_image_name(win32::handle_view process)
{
    std::wstring path;
    path.resize_and_overwrite(32768, [&](wchar_t *buffer, size_t capacity) {
        auto length = static_cast<DWORD>(capacity);
        if (!::QueryFullProcessImageNameW(process.get(), 0, buffer, &length))
            win32::throw_last_error();
        return static_cast<size_t>(length);
    });
    return path;
}

} // namespace win32
