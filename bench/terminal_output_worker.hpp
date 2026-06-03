#pragma once

// Shared worker helpers for output-throughput scenarios. The worker announces a
// ready marker, waits for one input trigger, then writes complete repeated lines.

#include "common.hpp"

#include <fcntl.h>
#include <io.h>

namespace bench
{

inline void configure_binary_vt_stdout()
{
    if (_setmode(_fileno(stdout), _O_BINARY) == -1)
    {
        print_and_abort("_setmode(stdout, binary) failed\n");
    }
    ::SetConsoleOutputCP(CP_UTF8);
    DWORD mode = 0;
    const HANDLE output = ::GetStdHandle(STD_OUTPUT_HANDLE);
    if (::GetConsoleMode(output, &mode))
        ::SetConsoleMode(output, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
}

inline void wait_for_output_trigger(const wchar_t *ready_marker, const wchar_t *trigger_event_name)
{
    if (!ready_marker || ready_marker[0] == L'\0')
        return;

    const auto ready_bytes = narrow(ready_marker);
    if (std::fwrite(ready_bytes.data(), 1, ready_bytes.size(), stdout) != ready_bytes.size() ||
        std::fwrite("\r\n", 1, 2, stdout) != 2 || std::fflush(stdout) != 0)
    {
        print_and_abort("write output ready marker failed\n");
    }

    if (trigger_event_name && trigger_event_name[0] != L'\0')
    {
        win32::handle trigger{::OpenEventW(SYNCHRONIZE, FALSE, trigger_event_name)};
        if (!trigger.valid())
            print_and_abort("OpenEvent(output trigger) failed: %lu\n", ::GetLastError());
        const DWORD wait = ::WaitForSingleObject(trigger.get(), INFINITE);
        if (wait != WAIT_OBJECT_0)
            print_and_abort("WaitForSingleObject(output trigger) failed: %lu\n", wait);
        return;
    }

    const HANDLE input = ::GetStdHandle(STD_INPUT_HANDLE);
    DWORD mode = 0;
    if (::GetConsoleMode(input, &mode))
        ::SetConsoleMode(input, 0);

    char trigger = 0;
    DWORD read = 0;
    if (!::ReadFile(input, &trigger, sizeof(trigger), &read, nullptr))
    {
        print_and_abort("ReadFile(output trigger) failed: %lu\n", ::GetLastError());
    }
    if (read == 0)
    {
        print_and_abort("ReadFile(output trigger) read zero bytes\n");
    }
}

inline int emit_repeated_output_line(std::string_view line, size_t target_bytes, const wchar_t *marker,
                                     const wchar_t *ready_marker, const wchar_t *trigger_event_name)
{
    configure_binary_vt_stdout();
    wait_for_output_trigger(ready_marker, trigger_event_name);

    std::string block;
    while (block.size() + line.size() <= 64 * 1024)
        block.append(line);

    size_t written = 0;
    while (written < target_bytes)
    {
        if (std::fwrite(block.data(), 1, block.size(), stdout) != block.size())
        {
            print_and_abort("fwrite(output block) failed\n");
        }
        written += block.size();
    }

    if (marker && marker[0] != L'\0')
    {
        const auto marker_bytes = narrow(marker);
        if (std::fwrite("\r\n", 1, 2, stdout) != 2 ||
            std::fwrite(marker_bytes.data(), 1, marker_bytes.size(), stdout) != marker_bytes.size() ||
            std::fwrite("\r\n", 1, 2, stdout) != 2)
        {
            print_and_abort("fwrite(output marker) failed\n");
        }
    }

    if (std::fflush(stdout) != 0)
    {
        print_and_abort("fflush(output) failed\n");
    }
    return 0;
}

inline int emit_repeated_output_line_unbuffered(std::string_view line, size_t target_bytes, const wchar_t *marker,
                                                const wchar_t *ready_marker, const wchar_t *trigger_event_name)
{
    configure_binary_vt_stdout();
    wait_for_output_trigger(ready_marker, trigger_event_name);

    const HANDLE output = ::GetStdHandle(STD_OUTPUT_HANDLE);

    size_t written = 0;
    while (written < target_bytes)
    {
        DWORD line_written = 0;
        if (!::WriteFile(output, line.data(), static_cast<DWORD>(line.size()), &line_written, nullptr))
        {
            print_and_abort("WriteFile(output line) failed: %lu\n", ::GetLastError());
        }
        if (line_written != line.size())
        {
            print_and_abort("WriteFile(output line) wrote %lu of %zu bytes\n", line_written, line.size());
        }
        written += line.size();
    }

    if (marker && marker[0] != L'\0')
    {
        const auto marker_bytes = narrow(marker);
        std::string marker_line;
        marker_line.reserve(marker_bytes.size() + 4);
        marker_line.append("\r\n", 2);
        marker_line.append(marker_bytes);
        marker_line.append("\r\n", 2);

        DWORD marker_written = 0;
        if (!::WriteFile(output, marker_line.data(), static_cast<DWORD>(marker_line.size()), &marker_written, nullptr))
        {
            print_and_abort("WriteFile(output marker) failed: %lu\n", ::GetLastError());
        }
        if (marker_written != marker_line.size())
        {
            print_and_abort("WriteFile(output marker) wrote %lu of %zu bytes\n", marker_written, marker_line.size());
        }
    }
    return 0;
}

} // namespace bench
