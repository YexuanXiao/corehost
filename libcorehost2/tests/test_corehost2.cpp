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
    constexpr char16_t input[]{u'A', u'B'};
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

    constexpr char16_t key_input[]{u'\x1b', u'O', u'A'};
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
    constexpr char16_t text_input[]{u'A'};
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

    server_state.output_code_page = 65001;
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

    constexpr char16_t mouse_mode[]{u'\x1b', u'[', u'?', u'1', u'0', u'0', u'0', u'h'};
    session.feed_input(mouse_mode);
    const auto mode_changes = session.state().take_private_modes();
    if (mode_changes.size() != 1 || mode_changes[0].first != 1000 || !mode_changes[0].second)
    {
        std::fprintf(stderr, "private mode forwarding failed\n");
        return EXIT_FAILURE;
    }

    corehost::corehost2::session mouse_session{8, 2};
    constexpr char16_t mouse_sequence[]{u'\x1b', u'[', u'<', u'0', u';', u'2', u';', u'2', u'M'};
    mouse_session.feed_input(mouse_sequence);
    if (!mouse_session.input().empty())
    {
        std::fprintf(stderr, "mouse accepted without tracking mode\n");
        return EXIT_FAILURE;
    }
    mouse_session.feed_input(mouse_mode);
    mouse_session.feed_input(mouse_sequence);
    if (mouse_session.input().size() != 1)
    {
        std::fprintf(stderr, "mouse rejected with tracking mode\n");
        return EXIT_FAILURE;
    }

    corehost::corehost2::session ambiguous_session{4, 2,
                                                   corehost::screen::text_measurement::graphemes, true};
    constexpr char16_t ambiguous_text[]{u'\u00A1'};
    ambiguous_session.feed(ambiguous_text);
    if (ambiguous_session.state().cursor().x != 2)
    {
        std::fprintf(stderr, "ambiguous width failed\n");
        return EXIT_FAILURE;
    }

    const char utf8_text[]{static_cast<char>(0xE4), static_cast<char>(0xB8), static_cast<char>(0xAD)};
    server.write_raw(std::span<const char>{utf8_text, sizeof(utf8_text)});
    if (server_buffer.at(2, 0).primary() != u'\u4E2D')
    {
        std::fprintf(stderr, "raw utf8 write failed\n");
        return EXIT_FAILURE;
    }

    corehost::corehost2::session split_session{8, 2};
    split_session.feed(std::u16string_view{u"\x1b[", 2});
    split_session.feed(std::u16string_view{u"2J", 2});
    split_session.feed(std::u16string_view{u"A", 1});
    if (split_session.screen().at(0, 0).primary() != u'A')
    {
        std::fprintf(stderr, "split escape sequence failed\n");
        return EXIT_FAILURE;
    }

    corehost::corehost2::session image_session{8, 2};
    image_session.feed(std::u16string_view{u"\x1b_Ga=T,i=7,s=4,v=4,c=2,r=1;AAAA\x1b\\"});
    if (image_session.screen().images().size() != 2 ||
        !image_session.screen().at(0, 0).has_image())
    {
        std::fprintf(stderr, "image placement failed\n");
        return EXIT_FAILURE;
    }

    corehost::corehost2::session sixel_session{8, 2};
    sixel_session.feed(
        std::u16string_view{u"\x1bPq\"1;1;16;16#0~~\x1b\\"});
    if (sixel_session.screen().images().size() != 2 ||
        !sixel_session.screen().at(0, 0).has_image())
    {
        std::fprintf(stderr, "sixel placement failed\n");
        return EXIT_FAILURE;
    }

    std::printf("libcorehost2 tests passed\n");
    return EXIT_SUCCESS;
}