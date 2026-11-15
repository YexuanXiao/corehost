#include "conpty.hpp"
#include "server_dispatcher.hpp"
#include "server_session.hpp"
#include "server_loop.hpp"
#include "session.hpp"

#include <cstdio>
#include <cstdlib>

int main()
{
    corehost::corehost2::session session{8, 2};
    constexpr char32_t input[]{U'A', U'B'};
    session.feed(input);

    if (session.screen().at(0, 0).primary() != u'A' ||
        session.screen().at(1, 0).primary() != u'B')
    {
        std::fprintf(stderr, "session text failed\n");
        return EXIT_FAILURE;
    }
    if (session.state().cursor().x != 2)
    {
        std::fprintf(stderr, "session cursor failed\n");
        return EXIT_FAILURE;
    }

    HANDLE input_read = nullptr;
    HANDLE input_write = nullptr;
    if (!::CreatePipe(&input_read, &input_write, nullptr, 0))
    {
        std::fprintf(stderr, "create input pipe failed\n");
        return EXIT_FAILURE;
    }
    ::CloseHandle(input_write);

    HANDLE output = ::CreateFileW(L"NUL", GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
    if (output == INVALID_HANDLE_VALUE)
    {
        std::fprintf(stderr, "open NUL failed\n");
        return EXIT_FAILURE;
    }

    corehost::corehost2::conpty_entry(win32::handle_view{}, win32::handle_view{}, {}, {},
                                      win32::handle_view{input_read}, win32::handle_view{output}, {},
                                      8, 2, false, corehost::screen::text_measurement::console);

    ::CloseHandle(output);
    ::CloseHandle(input_read);

    constexpr char32_t key_input[]{U'\x1b', U'O', U'A'};
    session.feed(key_input);
    if (session.input().size() != 1)
    {
        std::fprintf(stderr, "session key queue failed\n");
        return EXIT_FAILURE;
    }

    corehost::screen::screen_buffer server_buffer{8, 2};
    corehost::screen::console_state server_state{8, 2};
    corehost::consoleinput::input_queue server_input;
    corehost::corehost2::server_session server{server_buffer, server_state, server_input};
    corehost::coreio::io_msg message{};
    message.descriptor.InputSize = sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_SCREENBUFFERINFO_MSG);
    message.descriptor.OutputSize = sizeof(message.body);
    auto *header = reinterpret_cast<CONSOLE_MSG_HEADER *>(message.body);
    header->ApiNumber = ConsolepGetScreenBufferInfo;
    server.handle(message);
    if (message.complete.IoStatus.Status != 0)
    {
        std::fprintf(stderr, "server session failed\n");
        return EXIT_FAILURE;
    }

    struct fake_transport
    {
        int reads{};
        corehost::coreio::read_io_result read(CD_IO_COMPLETE *, corehost::coreio::io_msg &message) noexcept
        {
            if (++reads > 1)
            {
                return corehost::coreio::read_io_result::disconnected;
            }
            message.descriptor.InputSize =
                sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_SCREENBUFFERINFO_MSG);
            message.descriptor.OutputSize = sizeof(message.body);
            auto *header = reinterpret_cast<CONSOLE_MSG_HEADER *>(message.body);
            header->ApiNumber = ConsolepGetScreenBufferInfo;
            return corehost::coreio::read_io_result::got_message;
        }
        void wait() noexcept {}
    };

    fake_transport transport;
    corehost::coreio::run_server(transport, server);

    corehost::corehost2::session input_session{4, 2};
    constexpr char32_t text_input[]{U'A'};
    input_session.feed_input(text_input);
    if (input_session.input().size() != 1 ||
        input_session.screen().at(0, 0).primary() != u' ')
    {
        std::fprintf(stderr, "session feed_input failed\n");
        return EXIT_FAILURE;
    }

    corehost::corehost2::server_dispatcher dispatcher{win32::handle_view{}, win32::handle_view{}, win32::handle_view{}, server};
    corehost::coreio::io_msg close_message{};
    close_message.descriptor.Function = CONSOLE_IO_CLOSE_OBJECT;
    if (!dispatcher.handle(close_message) || close_message.complete.IoStatus.Status != 0)
    {
        std::fprintf(stderr, "server dispatcher close failed\n");
        return EXIT_FAILURE;
    }

    const char raw_text[]{"Hi"};
    server.write_raw(std::span<const char>{raw_text, 2});
    if (server_buffer.at(0, 0).primary() != u'H' ||
        server_buffer.at(1, 0).primary() != u'i')
    {
        std::fprintf(stderr, "raw write failed\n");
        return EXIT_FAILURE;
    }

    corehost::consoleinput::key_event raw_read_key;
    raw_read_key.character = u'X';
    server_input.push(
        corehost::consoleinput::input_event{corehost::consoleinput::input_event_type::key, raw_read_key});
    char raw_read_data[1]{};
    if (server.read_raw(std::span<char>{raw_read_data, 1}) != 1 || raw_read_data[0] != 'X')
    {
        std::fprintf(stderr, "raw read failed\n");
        return EXIT_FAILURE;
    }

    corehost::corehost2::session console_session{4, 2,
                                                 corehost::screen::text_measurement::console};
    console_session.feed(input);
    if (console_session.screen().at(0, 0).primary() != u'A')
    {
        std::fprintf(stderr, "console measurement session failed\n");
        return EXIT_FAILURE;
    }

    std::printf("libcorehost2 tests passed\n");
    return EXIT_SUCCESS;
}