
#pragma once

#include <windows.h>
#include <shlobj.h>
#include <string>
#include "win32/string.hpp"

namespace shell
{
struct get_shell_result
{
    win32::wcstring_view name;
    std::wstring path;
};

namespace details
{
// 保证路径结尾不包含反斜线
class known_folder_path
{
    PWSTR m_path = nullptr;
    size_t m_size = 0;

  public:
    explicit known_folder_path(REFKNOWNFOLDERID rfid) noexcept
    {
        if (SUCCEEDED(::SHGetKnownFolderPath(rfid, 0, nullptr, &m_path)))
            m_size = wcslen(m_path);
    }

    ~known_folder_path() noexcept
    {
        if (m_path)
            ::CoTaskMemFree(m_path);
    }

    known_folder_path(const known_folder_path &) = delete;
    known_folder_path &operator=(const known_folder_path &) = delete;

    const wchar_t *c_str() const noexcept
    {
        return m_path;
    }

    size_t size() const noexcept
    {
        return m_size;
    }

    bool empty() const noexcept
    {
        return m_path == nullptr;
    }
};
} // namespace details

// Windows 商店应用的别名是一种特殊的重分析点
inline bool file_exists(PCWSTR file_path) noexcept
{
    HANDLE hFile = ::CreateFileW(file_path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                                 FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (hFile != INVALID_HANDLE_VALUE)
    {
        ::CloseHandle(hFile);
        return true;
    }
    return false;
}

inline get_shell_result get_shell() noexcept
{
    constexpr win32::wcstring_view prefix = L"\\\\?\\";
    // 直接假定存在pwsh别名，因此不需要判断是否安装了包
    constexpr win32::wcstring_view pwsh_suffix = L"\\Microsoft\\WindowsApps\\pwsh.exe";
    constexpr win32::wcstring_view powershell_suffix = L"\\WindowsPowerShell\\v1.0\\powershell.exe";
    constexpr win32::wcstring_view cmd_exe_suffix = L"\\cmd.exe";

    details::known_folder_path local_app_data(FOLDERID_LocalAppData);
    if (!local_app_data.empty())
    {
        std::wstring pwsh_path;
        pwsh_path.reserve(prefix.size() + local_app_data.size() + pwsh_suffix.size());
        pwsh_path.assign(prefix.data(), prefix.size());
        pwsh_path.append(local_app_data.c_str(), local_app_data.size());
        pwsh_path.append(pwsh_suffix.data(), pwsh_suffix.size());

        if (file_exists(pwsh_path.c_str()))
            return {L"PowerShell", pwsh_path};
    }

    details::known_folder_path system_dir(FOLDERID_System);
    if (system_dir.empty())
        return {};

    std::wstring powershell_path;
    powershell_path.reserve(prefix.size() + system_dir.size() + powershell_suffix.size());
    powershell_path.assign(prefix.data(), prefix.size());
    powershell_path.append(system_dir.c_str(), system_dir.size());
    powershell_path.append(powershell_suffix.data(), powershell_suffix.size());
    if (file_exists(powershell_path.c_str()))
        return {L"Windows PowerShell", powershell_path};

    std::wstring cmd_path;
    cmd_path.reserve(prefix.size() + system_dir.size() + cmd_exe_suffix.size());
    cmd_path.assign(prefix.data(), prefix.size());
    cmd_path.append(system_dir.c_str(), system_dir.size());
    cmd_path.append(cmd_exe_suffix.data(), cmd_exe_suffix.size());
    return {L"cmd", cmd_path};
}

inline std::wstring get_system_conhost_path() noexcept
{
    constexpr win32::wcstring_view prefix = L"\\\\?\\";
    constexpr win32::wcstring_view conhost_suffix = L"\\conhost.exe";

    details::known_folder_path system_dir(FOLDERID_System);
    if (system_dir.empty())
        return {};

    std::wstring conhost_path;
    conhost_path.reserve(prefix.size() + system_dir.size() + conhost_suffix.size());
    conhost_path.assign(prefix.data(), prefix.size());
    conhost_path.append(system_dir.c_str(), system_dir.size());
    conhost_path.append(conhost_suffix.data(), conhost_suffix.size());
    return conhost_path;
}

inline std::wstring get_module_path() noexcept
{
    std::wstring path;
    path.resize_and_overwrite(32768, [&](wchar_t *buffer, size_t capacity) noexcept {
        return static_cast<size_t>(::GetModuleFileNameW(nullptr, buffer, static_cast<DWORD>(capacity)));
    });
    return path;
}

inline std::wstring get_module_dir_path() noexcept
{
    std::wstring module_path = get_module_path();

    if (module_path.empty())
        return {};

    auto it = module_path.end();
    while (it != module_path.begin())
    {
        --it;
        if (*it == L'\\')
            break;
    }

    size_t dir_length = static_cast<size_t>(it - module_path.begin()) + 1;
    module_path.resize(dir_length);
    return module_path;
}
} // namespace shell
