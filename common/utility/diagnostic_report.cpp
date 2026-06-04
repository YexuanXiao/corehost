#include "diagnostic_report.hpp"

#include <windows.h>
#include <share.h>
#include <cstdio>
#include <string_view>
#include "utility/temp_path.hpp"

namespace diagnostic_report
{
namespace
{

[[nodiscard]] unsigned long long current_process_creation_unix_time() noexcept
{
    FILETIME creation_time{};
    FILETIME exit_time{};
    FILETIME kernel_time{};
    FILETIME user_time{};
    if (!::GetProcessTimes(::GetCurrentProcess(), &creation_time, &exit_time, &kernel_time, &user_time))
        return 0;

    ULARGE_INTEGER ticks{};
    ticks.LowPart = creation_time.dwLowDateTime;
    ticks.HighPart = creation_time.dwHighDateTime;

    constexpr unsigned long long windows_to_unix_epoch_100ns = 116444736000000000ULL;
    if (ticks.QuadPart < windows_to_unix_epoch_100ns)
        return 0;

    return (ticks.QuadPart - windows_to_unix_epoch_100ns) / 10000000ULL;
}

std::wstring make_report_path() noexcept
{
    auto path = utility::temp_directory();
    if (path.empty())
        return {};

    const auto process_start_time = current_process_creation_unix_time();
    if (process_start_time == 0)
        return {};

    constexpr std::size_t max_timestamp_digits = 20;
    constexpr std::size_t max_pid_digits = 10;
    constexpr auto prefix = std::wstring_view{L"corehost_report_"};
    constexpr auto suffix = std::wstring_view{L".log"};
    constexpr std::size_t max_filename_chars =
        prefix.size() + max_timestamp_digits + 1 + max_pid_digits + suffix.size();

    const auto filename_offset = path.size();
    path.resize(filename_offset + max_filename_chars + 1);

    const int filename_chars =
        std::swprintf(path.data() + filename_offset, max_filename_chars + 1, L"corehost_report_%llu_%010u.log",
                      process_start_time, ::GetCurrentProcessId());
    if (filename_chars < 0 || static_cast<std::size_t>(filename_chars) > max_filename_chars)
        return {};

    path.resize(filename_offset + static_cast<std::size_t>(filename_chars));
    return path;
}

std::wstring write_connect_report(win32::wcstring_view reason, DWORD client_pid, DWORD process_group_id,
                                  DWORD show_window, DWORD startup_flags, bool window_visible, bool console_app,
                                  win32::wcstring_view command_line) noexcept
{
    auto path = make_report_path();
    if (path.empty())
        return {};

    auto *file = ::_wfsopen(path.c_str(), L"w, ccs=UTF-8", _SH_DENYNO);
    if (file == nullptr)
        return {};

    std::fwprintf(file, L"%.*ls\n", static_cast<int>(reason.size()), reason.data());
    std::fwprintf(file, L"ClientPid: %lu\n", client_pid);
    std::fwprintf(file, L"ProcessGroupId: %lu\n", process_group_id);
    std::fwprintf(file, L"CommandLine: %.*ls\n", static_cast<int>(command_line.size()), command_line.data());
    std::fwprintf(file, L"ShowWindow: %lu\n", show_window);
    std::fwprintf(file, L"StartupFlags: 0x%08lx\n", startup_flags);
    std::fwprintf(file, L"WindowVisible: %u\n", window_visible ? 1u : 0u);
    std::fwprintf(file, L"ConsoleApp: %u\n", console_app ? 1u : 0u);
    std::fclose(file);
    return path;
}

} // namespace

std::wstring write_elevated_terminal_blocked(win32::wcstring_view reason, DWORD client_pid, DWORD process_group_id,
                                             DWORD show_window, DWORD startup_flags, bool window_visible,
                                             bool console_app, win32::wcstring_view command_line) noexcept
{
    return write_connect_report(reason, client_pid, process_group_id, show_window, startup_flags, window_visible,
                                console_app, command_line);
}

std::wstring write_no_default_terminal(win32::wcstring_view reason, DWORD client_pid, DWORD process_group_id,
                                       DWORD show_window, DWORD startup_flags, bool window_visible, bool console_app,
                                       win32::wcstring_view command_line) noexcept
{
    return write_connect_report(reason, client_pid, process_group_id, show_window, startup_flags, window_visible,
                                console_app, command_line);
}

} // namespace diagnostic_report
