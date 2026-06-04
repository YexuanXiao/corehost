#include "utility/log.hpp"

#ifndef COREHOST_DISABLE_LOG

#include <windows.h>

#include "shell/shell.hpp"
#include "utility/temp_path.hpp"
#include "win32/string.hpp"

#include <algorithm>
#include <array>
#include <cstdarg>
#include <cstdio>
#include <ctime>
#include <cwchar>
#include <cwctype>
#include <share.h>
#include <string>
#include <string_view>
#include <vector>

namespace corehost::log
{
namespace
{

FILE *init_log_file() noexcept
{
    std::wstring exe_dir = shell::get_module_dir_path();
    auto lower_exe_dir = exe_dir;
    std::ranges::transform(lower_exe_dir, lower_exe_dir.begin(),
                           [](wchar_t ch) { return static_cast<wchar_t>(std::towlower(ch)); });
    if (lower_exe_dir.find(L"\\system32") != std::wstring::npos)
        exe_dir = utility::temp_directory();

    const auto now = std::time(nullptr);
    if (now == static_cast<std::time_t>(-1))
        return nullptr;

    if (exe_dir.empty())
        return nullptr;

    exe_dir.append(L"logs");

    if (!::CreateDirectoryW(exe_dir.c_str(), nullptr) && ::GetLastError() != ERROR_ALREADY_EXISTS)
        return nullptr;

    exe_dir.push_back(L'\\');

    constexpr std::size_t max_timestamp_digits = 20;
    constexpr std::size_t max_pid_digits = 10;
    constexpr auto prefix = std::wstring_view{L"corehost_"};
    constexpr auto suffix = std::wstring_view{L".log"};
    constexpr std::size_t max_filename_chars =
        prefix.size() + max_timestamp_digits + 1 + max_pid_digits + suffix.size();

    const auto filename_offset = exe_dir.size();
    exe_dir.resize(filename_offset + max_filename_chars + 1);

    const int filename_chars =
        std::swprintf(exe_dir.data() + filename_offset, max_filename_chars + 1, L"corehost_%llu_%010u.log",
                      static_cast<unsigned long long>(now), ::GetCurrentProcessId());
    if (filename_chars < 0 || static_cast<std::size_t>(filename_chars) > max_filename_chars)
        return nullptr;

    exe_dir.resize(filename_offset + static_cast<std::size_t>(filename_chars));
    auto *file = ::_wfsopen(exe_dir.c_str(), L"a", _SH_DENYNO);
    if (file == nullptr)
    {
        ::MessageBoxW(nullptr, exe_dir.c_str(), L"CoreHost Open Log File Error", MB_OK | MB_ICONERROR);
        std::abort();
    }
    return file;
}

FILE *g_log_file = nullptr;

void write_hex_digits(wchar_t *out, const unsigned char *bytes, std::size_t count) noexcept
{
    constexpr std::wstring_view digits = L"0123456789ABCDEF";
    for (std::size_t i = 0; i < count; ++i)
    {
        const auto value = bytes[i];
        out[i * 3] = digits[value >> 4];
        out[i * 3 + 1] = digits[value & 0x0F];
        out[i * 3 + 2] = L' ';
    }
    if (count != 0)
        out[count * 3 - 1] = L'\0';
    else
        out[0] = L'\0';
}
} // namespace

void initialize_log() noexcept
{
    assert(g_log_file == nullptr);
    g_log_file = init_log_file();
}

void core_log(const wchar_t *fmt, ...) noexcept
{
    if (g_log_file == nullptr)
        return;

    wchar_t buf[1024];

    std::time_t now = std::time(nullptr);
    std::tm tm_buf{};
    localtime_s(&tm_buf, &now);

    int off = std::swprintf(buf, std::size(buf), L"[%02d:%02d:%02d] ", tm_buf.tm_hour, tm_buf.tm_min, tm_buf.tm_sec);
    if (off < 0)
        return;

    va_list va;
    va_start(va, fmt);
    std::vswprintf(buf + off, std::size(buf) - static_cast<std::size_t>(off), fmt, va);
    va_end(va);

    std::fputws(buf, g_log_file);
    std::fflush(g_log_file);
}

void core_log_hex(const char *function_name, const char *tag, const void *data, std::size_t size) noexcept
{
    auto *bytes = static_cast<const unsigned char *>(data);
    if (bytes == nullptr && size != 0)
    {
        core_log(L"%-35hs [hex] %hs: null data, len=%zu\n", function_name, tag, size);
        return;
    }

    constexpr std::size_t bytes_per_line = 32;
    std::array<wchar_t, bytes_per_line * 3> hex{};

    if (size == 0)
    {
        core_log(L"%-35hs [hex] %hs: len=0\n", function_name, tag);
        return;
    }

    for (std::size_t offset = 0; offset < size; offset += bytes_per_line)
    {
        const auto count = std::min(bytes_per_line, size - offset);
        write_hex_digits(hex.data(), bytes + offset, count);
        core_log(L"%-35hs [hex] %hs +%04zu/%zu: %ls\n", function_name, tag, offset, size, hex.data());
    }
}

} // namespace corehost::log

#else

namespace corehost::log
{

void initialize_log() noexcept
{
}

} // namespace corehost::log

#endif
