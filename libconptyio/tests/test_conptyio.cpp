#include "terminal_reader.hpp"
#include "terminal_reader.hpp"
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
    HANDLE pipe_read = nullptr;
    HANDLE pipe_write = nullptr;
    if (!::CreatePipe(&pipe_read, &pipe_write, nullptr, 0))
    {
        std::fprintf(stderr, "create pipe failed\n");
        return EXIT_FAILURE;
    }

    corehost::conptyio::terminal_reader reader{win32::handle_view{pipe_read}};
    const char text[] = {static_cast<char>(0xE4), static_cast<char>(0xB8), static_cast<char>(0xAD)};
    DWORD written = 0;
    ::WriteFile(pipe_write, text, 3, &written, nullptr);
    (void)reader.read_batch();
    if (reader.codepoints().size() != 1 || reader.codepoints()[0] != U'\u4E2D')
    {
        std::fprintf(stderr, "utf8 decode failed\n");
        return EXIT_FAILURE;
    }

    ::CloseHandle(pipe_write);
    ::CloseHandle(pipe_read);

    std::printf("libconptyio tests passed\n");
    return EXIT_SUCCESS;
}