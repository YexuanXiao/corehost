#pragma once

// Common bench utilities shared by the runner, worker modes, and scenarios.
// This header intentionally contains only small value types and thin Win32
// wrappers so higher-level headers can describe their own control flow.

#include "win32/handle.hpp"
#include "win32/event.hpp"

#include <windows.h>

#include <algorithm>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace bench
{

struct scenario_result
{
    std::string host;
    std::string name;
    size_t input_bytes = 0;
    size_t output_bytes = 0;
    double elapsed_ms = 0;
};

// One measured host run. The main process prints this directly or compares two
// host_result values by matching scenarios in order.
struct host_result
{
    std::string label;
    std::vector<scenario_result> scenarios;
};

// High-resolution wall-clock helpers used around one complete scenario action.
[[nodiscard]] inline int64_t perf_counter()
{
    LARGE_INTEGER value{};
    ::QueryPerformanceCounter(&value);
    return value.QuadPart;
}

[[nodiscard]] inline std::wstring unique_event_name()
{
    return L"Local\\corehost-conpty-bench-" + std::to_wstring(::GetCurrentProcessId()) + L"-" +
           std::to_wstring(perf_counter());
}

[[nodiscard]] inline int64_t perf_frequency()
{
    LARGE_INTEGER value{};
    ::QueryPerformanceFrequency(&value);
    return value.QuadPart;
}

[[nodiscard]] inline double elapsed_ms(int64_t begin, int64_t end)
{
    return static_cast<double>(end - begin) * 1000.0 / static_cast<double>(perf_frequency());
}

[[noreturn]] inline void print_and_abort(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    std::vfprintf(stderr, format, args);
    va_end(args);
    std::exit(1);
}

// UTF-16 command-line/path text to UTF-8 result labels and worker markers.
[[nodiscard]] inline std::string narrow(std::wstring_view value)
{
    if (value.empty())
        return {};

    const int required =
        ::WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (required <= 0)
    {
        print_and_abort("WideCharToMultiByte(size) failed: %lu\n", ::GetLastError());
    }

    std::string result(static_cast<size_t>(required), '\0');
    const int written = ::WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(),
                                              required, nullptr, nullptr);
    if (written != required)
    {
        print_and_abort("WideCharToMultiByte(data) failed: %lu\n", ::GetLastError());
    }
    return result;
}

// UTF-8 labels/markers to UTF-16 command-line arguments.
[[nodiscard]] inline std::wstring widen(std::string_view value)
{
    if (value.empty())
        return {};

    const int required = ::MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0);
    if (required <= 0)
    {
        print_and_abort("MultiByteToWideChar(size) failed: %lu\n", ::GetLastError());
    }

    std::wstring result(static_cast<size_t>(required), L'\0');
    const int written =
        ::MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(), required);
    if (written != required)
    {
        print_and_abort("MultiByteToWideChar(data) failed: %lu\n", ::GetLastError());
    }
    return result;
}

// Write a full terminal input payload to the PTY input pipe. Short writes are
// retried; failure means the host or child side closed the input path.
inline void write_all(HANDLE pipe, std::string_view bytes)
{
    while (!bytes.empty())
    {
        DWORD written = 0;
        const auto chunk = static_cast<DWORD>(std::min<size_t>(bytes.size(), 64 * 1024));
        if (!::WriteFile(pipe, bytes.data(), chunk, &written, nullptr))
        {
            print_and_abort("WriteFile(pty input) failed: %lu\n", ::GetLastError());
        }
        if (written == 0)
        {
            print_and_abort("WriteFile(pty input) wrote zero bytes\n");
        }
        bytes.remove_prefix(written);
    }
}

// Quote one CreateProcess command-line argument. Callers build complete command
// lines from already separated executable/argument strings.
[[nodiscard]] inline std::wstring quote(std::wstring_view value)
{
    std::wstring result;
    result.reserve(value.size() + 2);
    result.push_back(L'"');
    result.append(value);
    result.push_back(L'"');
    return result;
}

// Path to the currently running conpty_bench executable. Worker scenarios use
// this to start the same binary in helper modes.
[[nodiscard]] inline std::wstring current_exe_path()
{
    std::wstring path;
    path.resize_and_overwrite(32768, [](wchar_t *buffer, size_t capacity) noexcept {
        return static_cast<size_t>(::GetModuleFileNameW(nullptr, buffer, static_cast<DWORD>(capacity)));
    });
    return path;
}

// Human-readable host label used in RESULT rows.
[[nodiscard]] inline std::string label_from_path(std::wstring_view path)
{
    return narrow(std::filesystem::path{path}.filename().wstring());
}

// Validate user-provided host paths before creating worker directories.
inline void validate_host_path(const wchar_t *path)
{
    const DWORD attributes = ::GetFileAttributesW(path);
    if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_DIRECTORY))
    {
        print_and_abort("invalid host path failed: %lu\n",
                        attributes == INVALID_FILE_ATTRIBUTES ? ::GetLastError() : ERROR_DIRECTORY);
    }
}

} // namespace bench
