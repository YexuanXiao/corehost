#include "conpty.hpp"
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

    std::printf("libcorehost2 tests passed\n");
    return EXIT_SUCCESS;
}