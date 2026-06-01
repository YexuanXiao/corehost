#pragma once

// Worker side for cjk-terminal-input. This code runs inside the ConPTY child
// process, announces readiness, then consumes stdin until the marker arrives.

#include "common.hpp"

namespace bench
{

// --consume-input <marker> [ready-marker]
inline int consume_stdin_until_marker(const wchar_t *marker, const wchar_t *ready_marker)
{
    ::SetConsoleCP(CP_UTF8);
    ::SetConsoleOutputCP(CP_UTF8);

    const HANDLE input = ::GetStdHandle(STD_INPUT_HANDLE);
    DWORD mode = 0;
    if (::GetConsoleMode(input, &mode))
        ::SetConsoleMode(input, ENABLE_PROCESSED_INPUT);

    const auto marker_bytes = narrow(marker);
    const auto ready_bytes = narrow(ready_marker);
    if (!ready_bytes.empty())
    {
        if (std::fwrite(ready_bytes.data(), 1, ready_bytes.size(), stdout) != ready_bytes.size() ||
            std::fwrite("\r\n", 1, 2, stdout) != 2 || std::fflush(stdout) != 0)
        {
            print_and_abort("write input ready marker failed\n");
        }
    }

    std::string tail;
    tail.reserve(marker_bytes.size() + 4096);

    size_t total = 0;
    char buffer[64 * 1024];
    for (;;)
    {
        DWORD read = 0;
        if (!::ReadFile(input, buffer, sizeof(buffer), &read, nullptr))
        {
            print_and_abort("ReadFile(console input) failed: %lu\n", ::GetLastError());
        }
        if (read == 0)
        {
            print_and_abort("ReadFile(console input) read zero bytes\n");
        }

        total += read;
        tail.append(buffer, read);
        if (tail.find(marker_bytes) != std::string::npos)
            break;
        if (tail.size() > marker_bytes.size() + 4096)
            tail.erase(0, tail.size() - marker_bytes.size() - 4096);
    }

    std::printf("\r\n%s %zu\r\n", marker_bytes.c_str(), total);
    if (std::fflush(stdout) != 0)
    {
        print_and_abort("fflush(input result) failed\n");
    }
    return 0;
}

} // namespace bench
