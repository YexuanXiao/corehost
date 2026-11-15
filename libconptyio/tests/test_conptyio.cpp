#include "signal.hpp"
#include "signal_reader.hpp"
#include "signal_reader.hpp"
#include "terminal_reader.hpp"
#include "vt_input_thread.hpp"
#include "pseudo_window.hpp"
#include "terminal_writer.hpp"

#include <windows.h>

#include <string>
#include <vector>
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

    struct recording_sink final : corehost::conptyio::vt_input_sink
    {
        std::string bytes;
        bool eof{};

        void on_input_bytes(std::span<const std::byte> value) override
        {
            bytes.append(reinterpret_cast<const char *>(value.data()), value.size());
        }

        void on_input_eof() override
        {
            eof = true;
        }
    };

    HANDLE input_read = nullptr;
    HANDLE input_write = nullptr;
    if (!::CreatePipe(&input_read, &input_write, nullptr, 0))
    {
        std::fprintf(stderr, "create input pipe failed\n");
        return EXIT_FAILURE;
    }

    recording_sink input_sink;
    corehost::conptyio::vt_input_thread input_thread{win32::handle_view{input_read}, input_sink};
    input_thread.start();
    DWORD input_written = 0;
    ::WriteFile(input_write, "abc", 3, &input_written, nullptr);
    ::CloseHandle(input_write);
    input_thread.join();
    if (input_sink.bytes != "abc" || !input_sink.eof)
    {
        std::fprintf(stderr, "input thread EOF failed\n");
        return EXIT_FAILURE;
    }
    ::CloseHandle(input_read);

    HANDLE stop_read = nullptr;
    HANDLE stop_write = nullptr;
    if (!::CreatePipe(&stop_read, &stop_write, nullptr, 0))
    {
        std::fprintf(stderr, "create stop pipe failed\n");
        return EXIT_FAILURE;
    }
    recording_sink stop_sink;
    corehost::conptyio::vt_input_thread stop_thread{win32::handle_view{stop_read}, stop_sink};
    stop_thread.start();
    stop_thread.stop_and_join();
    ::CloseHandle(stop_write);
    ::CloseHandle(stop_read);
    corehost::screen::screen_buffer signal_buffer{8, 2};
    corehost::screen::console_state signal_state{8, 2};
    std::uint32_t resize = (3u << 16) | 10u;
    corehost::conptyio::apply_signal(
        8, std::span<const std::byte>{reinterpret_cast<const std::byte *>(&resize), sizeof(resize)},
        signal_buffer, signal_state);
    if (signal_buffer.buffer_size() != corehost::screen::coord{10, 3} || signal_state.width() != 10)
    {
        std::fprintf(stderr, "signal resize failed\n");
        return EXIT_FAILURE;
    }

    const std::uint16_t clear = 0;
    corehost::conptyio::apply_signal(
        2, std::span<const std::byte>{reinterpret_cast<const std::byte *>(&clear), sizeof(clear)},
        signal_buffer, signal_state);
    if (signal_state.cursor().x != 0 || signal_state.cursor().y != 0)
    {
        std::fprintf(stderr, "signal clear failed\n");
        return EXIT_FAILURE;
    }

    HANDLE signal_read = nullptr;
    HANDLE signal_write = nullptr;
    if (!::CreatePipe(&signal_read, &signal_write, nullptr, 0))
    {
        std::fprintf(stderr, "create signal pipe failed\n");
        return EXIT_FAILURE;
    }
    const std::uint16_t message[] = {2, 0};
    DWORD message_written = 0;
    ::WriteFile(signal_write, message, sizeof(message), &message_written, nullptr);
    ::CloseHandle(signal_write);

    corehost::conptyio::signal_reader signal_reader{win32::handle_view{signal_read}};
    if (!signal_reader.read_one() || signal_reader.id() != 2 ||
        signal_reader.payload().size() != 2)
    {
        std::fprintf(stderr, "signal reader failed\n");
        return EXIT_FAILURE;
    }
    ::CloseHandle(signal_read);

    corehost::conptyio::pseudo_window signal_window;
    if (!signal_window.create())
    {
        std::fprintf(stderr, "pseudo window create failed\n");
        return EXIT_FAILURE;
    }
    const std::uint16_t visible = 1;
    corehost::conptyio::apply_signal(
        1, std::span<const std::byte>{reinterpret_cast<const std::byte *>(&visible), sizeof(visible)},
        signal_buffer, signal_state, &signal_window);

    std::printf("libconptyio tests passed\n");
    return EXIT_SUCCESS;
}