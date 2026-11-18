#include "signal.hpp"
#include "terminal_reader.hpp"
#include "vt_input_thread.hpp"
#include "signal_thread.hpp"
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
    writer.flush();
    if (!writer.empty())
    {
        std::fprintf(stderr, "flush failed\n");
        ::CloseHandle(raw);
        return EXIT_FAILURE;
    }

    ::CloseHandle(raw);

    HANDLE closed_read = nullptr;
    HANDLE closed_write = nullptr;
    if (!::CreatePipe(&closed_read, &closed_write, nullptr, 0))
    {
        std::fprintf(stderr, "create closed pipe failed\n");
        return EXIT_FAILURE;
    }
    corehost::conptyio::terminal_writer closed_writer{win32::handle_view{closed_write}};
    ::CloseHandle(closed_read);
    closed_writer.buffer().append_text(u8"closed");
    closed_writer.flush();
    if (::WaitForSingleObject(closed_writer.close_requested().get(), 0) != WAIT_OBJECT_0)
    {
        std::fprintf(stderr, "writer did not report a closed output pipe\n");
        return EXIT_FAILURE;
    }
    ::CloseHandle(closed_write);

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

    const char emoji[] = {static_cast<char>(0xF0), static_cast<char>(0x9F), static_cast<char>(0x98),
                          static_cast<char>(0x80)};
    ::WriteFile(pipe_write, emoji, 2, &written, nullptr);
    (void)reader.read_batch();
    if (!reader.codepoints().empty())
    {
        std::fprintf(stderr, "incomplete utf8 decoded early\n");
        return EXIT_FAILURE;
    }
    ::WriteFile(pipe_write, emoji + 2, 2, &written, nullptr);
    (void)reader.read_batch();
    if (reader.codepoints().size() != 2 || reader.codepoints()[0] != 0xD83D || reader.codepoints()[1] != 0xDE00)
    {
        std::fprintf(stderr, "split utf8 decode failed\n");
        return EXIT_FAILURE;
    }

    ::CloseHandle(pipe_write);
    ::CloseHandle(pipe_read);

    struct recording_sink final
    {
        std::string bytes;
        bool eof{};

        void on_input_bytes(std::span<const std::byte> value)
        {
            bytes.append(reinterpret_cast<const char *>(value.data()), value.size());
        }

        void on_input_eof()
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
    struct recording_signal_sink final
    {
        std::uint16_t id{};
        std::vector<std::byte> payload;
        bool eof{};

        void on_signal(std::uint16_t value, std::span<const std::byte> bytes)
        {
            id = value;
            payload.assign(bytes.begin(), bytes.end());
        }

        void on_signal_eof()
        {
            eof = true;
        }
    };

    HANDLE signal_thread_read = nullptr;
    HANDLE signal_thread_write = nullptr;
    if (!::CreatePipe(&signal_thread_read, &signal_thread_write, nullptr, 0))
    {
        std::fprintf(stderr, "create signal thread pipe failed\n");
        return EXIT_FAILURE;
    }
    recording_signal_sink signal_sink;
    corehost::conptyio::pty_signal_thread signal_thread{win32::handle_view{signal_thread_read}, signal_sink};
    signal_thread.start();
    const std::uint16_t signal_id = 8;
    const std::uint32_t signal_resize = (4u << 16) | 12u;
    DWORD signal_written = 0;
    const auto *id_bytes = reinterpret_cast<const char *>(&signal_id);
    ::WriteFile(signal_thread_write, id_bytes, 1, &signal_written, nullptr);
    ::Sleep(1);
    ::WriteFile(signal_thread_write, id_bytes + 1, 1, &signal_written, nullptr);
    const auto *payload_bytes = reinterpret_cast<const char *>(&signal_resize);
    ::WriteFile(signal_thread_write, payload_bytes, 2, &signal_written, nullptr);
    ::Sleep(1);
    ::WriteFile(signal_thread_write, payload_bytes + 2, 2, &signal_written, nullptr);
    ::CloseHandle(signal_thread_write);
    signal_thread.join();
    if (signal_sink.id != 8 || signal_sink.payload.size() != sizeof(signal_resize) || !signal_sink.eof)
    {
        std::fprintf(stderr, "signal thread read failed\n");
        return EXIT_FAILURE;
    }
    ::CloseHandle(signal_thread_read);

    HANDLE signal_stop_read = nullptr;
    HANDLE signal_stop_write = nullptr;
    if (!::CreatePipe(&signal_stop_read, &signal_stop_write, nullptr, 0))
    {
        std::fprintf(stderr, "create signal stop pipe failed\n");
        return EXIT_FAILURE;
    }
    recording_signal_sink signal_stop_sink;
    corehost::conptyio::pty_signal_thread signal_stop_thread{win32::handle_view{signal_stop_read}, signal_stop_sink};
    signal_stop_thread.start();
    signal_stop_thread.stop_and_join();
    ::CloseHandle(signal_stop_write);
    ::CloseHandle(signal_stop_read);

    HANDLE bad_read = nullptr;
    HANDLE bad_write = nullptr;
    if (!::CreatePipe(&bad_read, &bad_write, nullptr, 0))
    {
        std::fprintf(stderr, "create bad signal pipe failed\n");
        return EXIT_FAILURE;
    }
    recording_signal_sink bad_sink;
    corehost::conptyio::pty_signal_thread bad_thread{win32::handle_view{bad_read}, bad_sink};
    bad_thread.start();
    const std::uint16_t bad_id = 99;
    ::WriteFile(bad_write, &bad_id, sizeof(bad_id), &signal_written, nullptr);
    ::CloseHandle(bad_write);
    bad_thread.join();
    if (bad_sink.id != 0 || bad_sink.eof)
    {
        std::fprintf(stderr, "invalid signal did not stop cleanly\n");
        return EXIT_FAILURE;
    }
    ::CloseHandle(bad_read);

    corehost::screen::screen_buffer signal_buffer{8, 2};
    corehost::screen::console_state signal_state{8, 2};
    corehost::conptyio::pseudo_window signal_window;
    std::uint32_t resize = (3u << 16) | 10u;
    corehost::conptyio::apply_signal(
        8, std::span<const std::byte>{reinterpret_cast<const std::byte *>(&resize), sizeof(resize)}, signal_buffer,
        signal_state, signal_window);
    if (signal_buffer.buffer_size() != corehost::screen::coord{10, 3} || signal_state.width() != 10)
    {
        std::fprintf(stderr, "signal resize failed\n");
        return EXIT_FAILURE;
    }

    const std::uint16_t clear = 0;
    corehost::conptyio::apply_signal(
        2, std::span<const std::byte>{reinterpret_cast<const std::byte *>(&clear), sizeof(clear)}, signal_buffer,
        signal_state, signal_window);
    if (signal_state.cursor().x != 0 || signal_state.cursor().y != 0)
    {
        std::fprintf(stderr, "signal clear failed\n");
        return EXIT_FAILURE;
    }

    signal_window.start();
    if (signal_window.handle() == nullptr)
    {
        std::fprintf(stderr, "pseudo window create failed\n");
        return EXIT_FAILURE;
    }
    const std::uint16_t visible = 1;
    corehost::conptyio::apply_signal(
        1, std::span<const std::byte>{reinterpret_cast<const std::byte *>(&visible), sizeof(visible)}, signal_buffer,
        signal_state, signal_window);

    std::printf("libcorepty tests passed\n");
    return EXIT_SUCCESS;
}
