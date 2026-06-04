#include "temp_path.hpp"

#include <windows.h>

namespace utility
{

std::wstring temp_directory() noexcept
{
    std::wstring path;
    path.resize(path.capacity());

    // GetTempPathW is intentional here. CoreHost only writes diagnostic files,
    // so it does not need GetTempPath2W's protected-location behavior.
    DWORD len = ::GetTempPathW(static_cast<DWORD>(path.size()), path.data());
    if (len == 0)
        return {};

    if (len > path.size())
    {
        path.resize(len);
        len = ::GetTempPathW(static_cast<DWORD>(path.size()), path.data());
    }

    if (len == 0 || len > path.size())
        return {};

    path.resize(len);
    if (!path.empty() && path.back() != L'\\' && path.back() != L'/')
        path.push_back(L'\\');
    return path;
}

} // namespace utility
