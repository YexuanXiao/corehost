#include "terminal_writer.hpp"

#include <windows.h>

#include <cstdio>
#include <cstdlib>

int main()
{
    HANDLE raw = ::CreateFileW(L"NUL", GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
    if (raw == INVALID_HANDLE_VALUE)
    {
        std::fprintf(stderr, "open NUL failed\n");
        return EXIT_FAILURE;
    }

    corehost::conptyio::terminal_writer writer{win32::handle_view{raw}};
    writer.buffer().append_text(u8"hello");
    if (!writer.flush() || !writer.empty())
    {
        std::fprintf(stderr, "flush failed\n");
        ::CloseHandle(raw);
        return EXIT_FAILURE;
    }

    ::CloseHandle(raw);
    std::printf("libconptyio tests passed\n");
    return EXIT_SUCCESS;
}