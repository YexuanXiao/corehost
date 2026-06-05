#pragma once

#include <windows.h>
#include <winternl.h>
#include <psapi.h>
#include <cstddef>
#include <cstring>
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
            return 0uz;
        return static_cast<size_t>(length);
    });
    return path;
}

[[nodiscard]] inline std::wstring query_process_command_line(win32::handle_view process) noexcept
{
    constexpr ULONG ProcessCommandLineInformation = 60;
    constexpr auto max_command_line_bytes = static_cast<std::size_t>(USHRT_MAX - (USHRT_MAX % sizeof(wchar_t)));
    constexpr auto output_buffer_chars = (sizeof(UNICODE_STRING) + max_command_line_bytes) / sizeof(wchar_t);

    std::wstring command_line;
    command_line.resize_and_overwrite(output_buffer_chars, [&](wchar_t *buffer, size_t capacity) noexcept {
        const auto status = ::NtQueryInformationProcess(
            process.get(), static_cast<PROCESSINFOCLASS>(ProcessCommandLineInformation), buffer,
            static_cast<ULONG>(capacity * sizeof(wchar_t)), nullptr);
        if (status < 0)
            return 0uz;

        const auto &command_line_info = *reinterpret_cast<const UNICODE_STRING *>(buffer);
        if (command_line_info.Length % sizeof(wchar_t) != 0 || command_line_info.Buffer == nullptr)
            return 0uz;

        const auto *buffer_begin = reinterpret_cast<const std::byte *>(buffer);
        const auto *buffer_end = buffer_begin + capacity * sizeof(wchar_t);
        const auto *text_begin = reinterpret_cast<const std::byte *>(command_line_info.Buffer);
        const auto *text_end = text_begin + command_line_info.Length;
        if (text_begin < buffer_begin || text_begin > buffer_end || text_end < text_begin || text_end > buffer_end)
            return 0uz;

        const auto command_line_bytes = command_line_info.Length;
        std::memmove(buffer, command_line_info.Buffer, command_line_bytes);
        return static_cast<std::size_t>(command_line_bytes / sizeof(wchar_t));
    });
    return command_line;
}

} // namespace win32
