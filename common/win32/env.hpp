#pragma once
#include <windows.h>
#include <string>
#include <algorithm>

#include "utility/string.hpp"
#include "win32/error.hpp"
#include "win32/string.hpp"

namespace win32
{
[[nodiscard]] inline std::wstring read_environment(win32::wcstring_view name)
{
    const DWORD required = ::GetEnvironmentVariableW(name.c_str(), nullptr, 0);
    if (required == 0)
    {
        auto err = win32::get_last_error();
        if (err == win32::error::envvar_not_found)
            return {};
        win32::throw_last_error();
    }

    std::wstring buffer(required, L'\0');
    const DWORD written = ::GetEnvironmentVariableW(name.c_str(), buffer.data(), required);
    win32::throw_last_error(written == 0);
    buffer.resize(written);
    return utility::trim(std::move(buffer));
}
} // namespace win32