#include <windows.h>
#include <shlobj.h>
#include <appmodel.h>
#include "win32/string.hpp"
#include "shell.hpp"

namespace shell
{
// 保证路径结尾不包含反斜线
class known_folder_path
{
    PWSTR m_path = nullptr;
    size_t m_size = 0;

  public:
    explicit known_folder_path(REFKNOWNFOLDERID rfid)
    {
        if (SUCCEEDED(SHGetKnownFolderPath(rfid, 0, nullptr, &m_path)))
        {
            m_size = wcslen(m_path);
        }
    }
    ~known_folder_path()
    {
        if (m_path)
            CoTaskMemFree(m_path);
    }
    known_folder_path(const known_folder_path &) = delete;
    known_folder_path &operator=(const known_folder_path &) = delete;

    const wchar_t *c_str() const
    {
        return m_path;
    }
    size_t size() const
    {
        return m_size;
    }
    bool empty() const
    {
        return m_path == nullptr;
    }
};

bool is_package_installed(PCWSTR package_family_name)
{
    UINT32 count = 0;
    LONG result = FindPackagesByPackageFamily(package_family_name, PACKAGE_FILTER_HEAD | PACKAGE_FILTER_DIRECT, &count,
                                              nullptr, nullptr, nullptr, nullptr);
    return (result == ERROR_SUCCESS && count > 0);
}

bool file_exists(PCWSTR file_path)
{
    HANDLE hFile = CreateFileW(file_path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                               FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (hFile != INVALID_HANDLE_VALUE)
    {
        CloseHandle(hFile);
        return true;
    }
    return false;
}

get_shell_result get_shell()
{
    constexpr win32::wcstring_view prefix = L"\\\\?\\";
    // constexpr win32::wcstring_view package_family = L"Microsoft.PowerShell-LTS_8wekyb3d8bbwe";
    // 直接假定存在pwsh别名，因此不需要判断是否安装了包
    constexpr win32::wcstring_view pwsh_suffix = L"\\Microsoft\\WindowsApps\\pwsh.exe";
    constexpr win32::wcstring_view powershell_suffix = L"\\WindowsPowerShell\\v1.0\\powershell.exe";
    constexpr win32::wcstring_view cmd_exe_suffix = L"\\cmd.exe";

    known_folder_path local_app_data(FOLDERID_LocalAppData);
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

    known_folder_path system_dir(FOLDERID_System);
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

std::wstring get_system_conhost_path()
{
    constexpr win32::wcstring_view prefix = L"\\\\?\\";
    constexpr win32::wcstring_view conhost_suffix = L"\\conhost.exe";

    known_folder_path system_dir(FOLDERID_System);
    if (system_dir.empty())
        return {};

    std::wstring conhost_path;
    conhost_path.reserve(prefix.size() + system_dir.size() + conhost_suffix.size());
    conhost_path.assign(prefix.data(), prefix.size());
    conhost_path.append(system_dir.c_str(), system_dir.size());
    conhost_path.append(conhost_suffix.data(), conhost_suffix.size());
    return conhost_path;
}
} // namespace shell