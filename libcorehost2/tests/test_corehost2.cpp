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

    corehost::screen::screen_buffer write_server_buffer{8, 2};
    corehost::screen::console_state write_server_state{8, 2};
    corehost::consoleinput::input_queue write_server_input;
    corehost::corehost2::server_session write_server{write_server_buffer, write_server_state,
                                                    write_server_input};
    HANDLE write_server_event = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
    write_server.set_input_event(win32::handle_view{write_server_event});
    write_server.signal_input_event();
    if (::WaitForSingleObject(write_server_event, 0) != WAIT_OBJECT_0)
    {
        std::fprintf(stderr, "server input event signal failed\n");
        return EXIT_FAILURE;
    }
    write_server.reset_input_event_if_empty();
    if (::WaitForSingleObject(write_server_event, 0) != WAIT_TIMEOUT)
    {
        std::fprintf(stderr, "server input event reset failed\n");
        return EXIT_FAILURE;
    }
    ::CloseHandle(write_server_event);
    constexpr char16_t write_vt_text[]{u'A', u'\x1b', u'[', u'3', u'1', u'm', u'B'};
    corehost::coreio::io_msg write_vt_message{};
    write_vt_message.descriptor.InputSize =
        sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_WRITECONSOLE_MSG) + sizeof(write_vt_text);
    auto *write_vt_header = reinterpret_cast<CONSOLE_MSG_HEADER *>(write_vt_message.body);
    write_vt_header->ApiNumber = ConsolepWriteConsole;
    auto *write_vt_request = reinterpret_cast<CONSOLE_WRITECONSOLE_MSG *>(
        write_vt_message.body + sizeof(CONSOLE_MSG_HEADER));
    write_vt_request->Unicode = TRUE;
    std::memcpy(write_vt_request + 1, write_vt_text, sizeof(write_vt_text));
    if (!write_server.handle(write_vt_message) ||
        write_vt_request->NumBytes != sizeof(write_vt_text) ||
        write_server_buffer.at(0, 0).primary() != u'A' ||
        write_server_buffer.at(1, 0).primary() != u'B')
    {
        std::fprintf(stderr, "server WriteConsole VT failed\n");
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
    if (input_session.input().size() != 2 ||
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

    const char raw_x[]{"X"};
    server.push_raw(std::span<const char>{raw_x, 1});
    char raw_read_data[8]{};
    if (server.read_raw(std::span<char>{raw_read_data}) != 0)
    {
        std::fprintf(stderr, "raw read completed early\n");
        return EXIT_FAILURE;
    }
    const char raw_cr[]{"\r"};
    server.push_raw(std::span<const char>{raw_cr, 1});
    if (server.read_raw(std::span<char>{raw_read_data}) != 3 || raw_read_data[0] != 'X' ||
        raw_read_data[1] != '\r' || raw_read_data[2] != '\n')
    {
        std::fprintf(stderr, "raw read failed\n");
        return EXIT_FAILURE;
    }

    corehost::corehost2::session enter_session{8, 2};
    constexpr char16_t enter_input[]{u'A', u'\r'};
    enter_session.feed_input(enter_input);
    std::array<corehost::consoleinput::input_event, 8> enter_events{};
    const auto enter_count =
        enter_session.input().peek(std::span<corehost::consoleinput::input_event>{enter_events});
    if (enter_count != 4 || enter_events[2].key.character != u'\r')
    {
        std::fprintf(stderr, "enter input key failed\n");
        return EXIT_FAILURE;
    }

    corehost::corehost2::session window_session{8, 4};
    window_session.state().input_mode |= 0x0008;
    window_session.queue_window_size(8, 4);
    if (window_session.input().size() != 0)
    {
        std::fprintf(stderr, "initial window event queued\n");
        return EXIT_FAILURE;
    }
    window_session.queue_window_size(10, 5);
    corehost::consoleinput::input_event window_event{};
    if (window_session.input().peek(std::span<corehost::consoleinput::input_event>{&window_event, 1}) != 1 ||
        window_event.type != corehost::consoleinput::input_event_type::window ||
        window_event.window.width != 10 || window_event.window.height != 5)
    {
        std::fprintf(stderr, "window event failed\n");
        return EXIT_FAILURE;
    }

    corehost::screen::screen_buffer read_buffer{8, 2};
    corehost::screen::console_state read_state{8, 2};
    corehost::consoleinput::input_queue read_input;
    corehost::consoleapi::console_api read_api{read_buffer, read_state, &read_input};
    corehost::corehost2::session read_session{8, 2};
    constexpr char16_t read_text[]{u'A', u'B', u'\r'};
    read_session.feed_input(read_text);
    read_input = std::move(read_session.input());
    corehost::coreio::io_msg read_message{};
    read_message.descriptor.InputSize =
        sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_READCONSOLE_MSG);
    read_message.descriptor.OutputSize =
        sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_READCONSOLE_MSG) + 8;
    auto *read_header = reinterpret_cast<CONSOLE_MSG_HEADER *>(read_message.body);
    read_header->ApiNumber = ConsolepReadConsole;
    auto *read_request = reinterpret_cast<CONSOLE_READCONSOLE_MSG *>(
        read_message.body + sizeof(CONSOLE_MSG_HEADER));
    read_request->Unicode = TRUE;
    if (!read_api.handle(read_message) || read_request->NumBytes != 4 * sizeof(char16_t))
    {
        std::fprintf(stderr, "real ReadConsole path failed\n");
        return EXIT_FAILURE;
    }
    const auto *read_output = reinterpret_cast<const char16_t *>(
        read_message.body + sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_READCONSOLE_MSG));
    if (read_output[0] != u'A' || read_output[1] != u'B' || read_output[2] != u'\r' ||
        read_output[3] != u'\n')
    {
        std::fprintf(stderr, "real ReadConsole data failed\n");
        return EXIT_FAILURE;
    }

    constexpr char16_t history_text[]{u'\x1b', u'[', u'A', u'\r'};
    read_session.feed_input(history_text);
    read_input = std::move(read_session.input());
    corehost::coreio::io_msg history_message{};
    history_message.descriptor.InputSize =
        sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_READCONSOLE_MSG);
    history_message.descriptor.OutputSize =
        sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_READCONSOLE_MSG) + 8;
    auto *history_header = reinterpret_cast<CONSOLE_MSG_HEADER *>(history_message.body);
    history_header->ApiNumber = ConsolepReadConsole;
    auto *history_request = reinterpret_cast<CONSOLE_READCONSOLE_MSG *>(
        history_message.body + sizeof(CONSOLE_MSG_HEADER));
    history_request->Unicode = TRUE;
    if (!read_api.handle(history_message) || history_request->NumBytes != 4 * sizeof(char16_t))
    {
        std::fprintf(stderr, "real ReadConsole history failed bytes=%lu\n", history_request->NumBytes);
        return EXIT_FAILURE;
    }
    const auto *history_output = reinterpret_cast<const char16_t *>(
        history_message.body + sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_READCONSOLE_MSG));
    if (history_output[0] != u'A' || history_output[1] != u'B' ||
        history_output[2] != u'\r' || history_output[3] != u'\n')
    {
        std::fprintf(stderr, "real ReadConsole history data failed\n");
        return EXIT_FAILURE;
    }

    corehost::screen::screen_buffer edit_buffer{8, 2};
    corehost::screen::console_state edit_state{8, 2};
    corehost::consoleinput::input_queue edit_input;
    corehost::corehost2::session edit_session{8, 2};
    constexpr char16_t edit_text[]{u'A', u'B', u'\x08', u'C', u'\r'};
    edit_session.feed_input(edit_text);
    edit_input = std::move(edit_session.input());
    corehost::consoleapi::console_api edit_api{edit_buffer, edit_state, &edit_input};
    corehost::coreio::io_msg edit_message{};
    edit_message.descriptor.InputSize =
        sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_READCONSOLE_MSG);
    edit_message.descriptor.OutputSize =
        sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_READCONSOLE_MSG) + 8;
    auto *edit_header = reinterpret_cast<CONSOLE_MSG_HEADER *>(edit_message.body);
    edit_header->ApiNumber = ConsolepReadConsole;
    auto *edit_request = reinterpret_cast<CONSOLE_READCONSOLE_MSG *>(
        edit_message.body + sizeof(CONSOLE_MSG_HEADER));
    edit_request->Unicode = TRUE;
    if (!edit_api.handle(edit_message) || edit_request->NumBytes != 4 * sizeof(char16_t))
    {
        std::fprintf(stderr, "real ReadConsole edit failed\n");
        return EXIT_FAILURE;
    }
    const auto *edit_output = reinterpret_cast<const char16_t *>(
        edit_message.body + sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_READCONSOLE_MSG));
    if (edit_output[0] != u'A' || edit_output[1] != u'C' || edit_output[2] != u'\r' ||
        edit_output[3] != u'\n')
    {
        std::fprintf(stderr, "real ReadConsole edit data failed bytes=%lu: %04x %04x %04x %04x\\n",
                     edit_request->NumBytes, edit_output[0], edit_output[1], edit_output[2],
                     edit_output[3]);
        return EXIT_FAILURE;
    }

    corehost::screen::screen_buffer arrow_buffer{8, 2};
    corehost::screen::console_state arrow_state{8, 2};
    corehost::consoleinput::input_queue arrow_input;
    corehost::corehost2::session arrow_session{8, 2};
    constexpr char16_t arrow_text[]{u'A', u'B', u'\x1b', u'[', u'D', u'X', u'\r'};
    arrow_session.feed_input(arrow_text);
    arrow_input = std::move(arrow_session.input());
    corehost::consoleapi::console_api arrow_api{arrow_buffer, arrow_state, &arrow_input};
    corehost::coreio::io_msg arrow_message{};
    arrow_message.descriptor.InputSize =
        sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_READCONSOLE_MSG);
    arrow_message.descriptor.OutputSize =
        sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_READCONSOLE_MSG) + 8;
    auto *arrow_header = reinterpret_cast<CONSOLE_MSG_HEADER *>(arrow_message.body);
    arrow_header->ApiNumber = ConsolepReadConsole;
    auto *arrow_request = reinterpret_cast<CONSOLE_READCONSOLE_MSG *>(
        arrow_message.body + sizeof(CONSOLE_MSG_HEADER));
    arrow_request->Unicode = TRUE;
    if (!arrow_api.handle(arrow_message) || arrow_request->NumBytes != 5 * sizeof(char16_t))
    {
        std::fprintf(stderr, "real ReadConsole arrow failed bytes=%lu\\n", arrow_request->NumBytes);
        return EXIT_FAILURE;
    }
    const auto *arrow_output = reinterpret_cast<const char16_t *>(
        arrow_message.body + sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_READCONSOLE_MSG));
    if (arrow_output[0] != u'A' || arrow_output[1] != u'X' || arrow_output[2] != u'B' ||
        arrow_output[3] != u'\r')
    {
        std::fprintf(stderr, "real ReadConsole arrow data failed %04x %04x %04x %04x %04x\\n",
                     arrow_output[0], arrow_output[1], arrow_output[2], arrow_output[3],
                     arrow_output[4]);
        return EXIT_FAILURE;
    }

    corehost::screen::screen_buffer home_buffer{8, 2};
    corehost::screen::console_state home_state{8, 2};
    corehost::consoleinput::input_queue home_input;
    corehost::corehost2::session home_session{8, 2};
    constexpr char16_t home_text[]{u'A', u'B', u'\x1b', u'[', u'H', u'X', u'\r'};
    home_session.feed_input(home_text);
    home_input = std::move(home_session.input());
    corehost::consoleapi::console_api home_api{home_buffer, home_state, &home_input};
    corehost::coreio::io_msg home_message{};
    home_message.descriptor.InputSize =
        sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_READCONSOLE_MSG);
    home_message.descriptor.OutputSize =
        sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_READCONSOLE_MSG) + 8;
    auto *home_header = reinterpret_cast<CONSOLE_MSG_HEADER *>(home_message.body);
    home_header->ApiNumber = ConsolepReadConsole;
    auto *home_request = reinterpret_cast<CONSOLE_READCONSOLE_MSG *>(
        home_message.body + sizeof(CONSOLE_MSG_HEADER));
    home_request->Unicode = TRUE;
    if (!home_api.handle(home_message) || home_request->NumBytes != 5 * sizeof(char16_t))
    {
        std::fprintf(stderr, "real ReadConsole home failed bytes=%lu\n", home_request->NumBytes);
        return EXIT_FAILURE;
    }
    const auto *home_output = reinterpret_cast<const char16_t *>(
        home_message.body + sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_READCONSOLE_MSG));
    if (home_output[0] != u'X' || home_output[1] != u'A' || home_output[2] != u'B' ||
        home_output[3] != u'\r' || home_output[4] != u'\n')
    {
        std::fprintf(stderr, "real ReadConsole home data failed %04x %04x %04x\n",
                     home_output[0], home_output[1], home_output[2]);
        return EXIT_FAILURE;
    }

    corehost::screen::screen_buffer delete_buffer{8, 2};
    corehost::screen::console_state delete_state{8, 2};
    corehost::consoleinput::input_queue delete_input;
    corehost::corehost2::session delete_session{8, 2};
    constexpr char16_t delete_text[]{u'A', u'B', u'\x1b', u'[', u'D',
                                     u'\x1b', u'[', u'3', u'~', u'\r'};
    delete_session.feed_input(delete_text);
    delete_input = std::move(delete_session.input());
    corehost::consoleapi::console_api delete_api{delete_buffer, delete_state, &delete_input};
    corehost::coreio::io_msg delete_message{};
    delete_message.descriptor.InputSize =
        sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_READCONSOLE_MSG);
    delete_message.descriptor.OutputSize =
        sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_READCONSOLE_MSG) + 8;
    auto *delete_header = reinterpret_cast<CONSOLE_MSG_HEADER *>(delete_message.body);
    delete_header->ApiNumber = ConsolepReadConsole;
    auto *delete_request = reinterpret_cast<CONSOLE_READCONSOLE_MSG *>(
        delete_message.body + sizeof(CONSOLE_MSG_HEADER));
    delete_request->Unicode = TRUE;
    if (!delete_api.handle(delete_message) || delete_request->NumBytes != 3 * sizeof(char16_t))
    {
        std::fprintf(stderr, "real ReadConsole delete failed\n");
        return EXIT_FAILURE;
    }
    const auto *delete_output = reinterpret_cast<const char16_t *>(
        delete_message.body + sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_READCONSOLE_MSG));
    if (delete_output[0] != u'A' || delete_output[1] != u'\r' || delete_output[2] != u'\n')
    {
        std::fprintf(stderr, "real ReadConsole delete data failed\n");
        return EXIT_FAILURE;
    }

    corehost::screen::screen_buffer tab_buffer{8, 2};
    corehost::screen::console_state tab_state{8, 2};
    corehost::consoleinput::input_queue tab_input;
    corehost::corehost2::session tab_session{8, 2};
    constexpr char16_t tab_text[]{u'A', u'\t', u'B', u'\r'};
    tab_session.feed_input(tab_text);
    tab_input = std::move(tab_session.input());
    corehost::consoleapi::console_api tab_api{tab_buffer, tab_state, &tab_input};
    corehost::coreio::io_msg tab_message{};
    tab_message.descriptor.InputSize =
        sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_READCONSOLE_MSG);
    tab_message.descriptor.OutputSize =
        sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_READCONSOLE_MSG) + 8;
    auto *tab_header = reinterpret_cast<CONSOLE_MSG_HEADER *>(tab_message.body);
    tab_header->ApiNumber = ConsolepReadConsole;
    auto *tab_request = reinterpret_cast<CONSOLE_READCONSOLE_MSG *>(
        tab_message.body + sizeof(CONSOLE_MSG_HEADER));
    tab_request->Unicode = TRUE;
    if (!tab_api.handle(tab_message) || tab_request->NumBytes != 5 * sizeof(char16_t))
    {
        std::fprintf(stderr, "real ReadConsole tab failed\n");
        return EXIT_FAILURE;
    }
    const auto *tab_output = reinterpret_cast<const char16_t *>(
        tab_message.body + sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_READCONSOLE_MSG));
    if (tab_output[0] != u'A' || tab_output[1] != u'\t' || tab_output[2] != u'B' ||
        tab_output[3] != u'\r' || tab_output[4] != u'\n')
    {
        std::fprintf(stderr, "real ReadConsole tab data failed\n");
        return EXIT_FAILURE;
    }

    corehost::screen::screen_buffer input_buffer{8, 2};
    corehost::screen::console_state input_state{8, 2};
    corehost::consoleinput::input_queue input_queue;
    corehost::corehost2::session key_session{8, 2};
    constexpr char16_t key_text[]{u'X', u'\r'};
    key_session.feed_input(key_text);
    input_queue = std::move(key_session.input());
    corehost::consoleapi::console_api input_api{input_buffer, input_state, &input_queue};
    corehost::coreio::io_msg input_message{};
    input_message.descriptor.InputSize =
        sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_GETCONSOLEINPUT_MSG);
    input_message.descriptor.OutputSize =
        sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_GETCONSOLEINPUT_MSG) +
        4 * sizeof(INPUT_RECORD);
    auto *input_header = reinterpret_cast<CONSOLE_MSG_HEADER *>(input_message.body);
    input_header->ApiNumber = ConsolepGetConsoleInput;
    auto *input_request = reinterpret_cast<CONSOLE_GETCONSOLEINPUT_MSG *>(
        input_message.body + sizeof(CONSOLE_MSG_HEADER));
    input_request->Flags = CONSOLE_READ_NOWAIT;
    if (!input_api.handle(input_message) || input_request->NumRecords != 4)
    {
        std::fprintf(stderr, "real ReadConsoleInput path failed\n");
        return EXIT_FAILURE;
    }
    // The client receives CONSOLE_GETCONSOLEINPUT_MSG followed by the
    // INPUT_RECORD array. Write.Size must include the reply struct; only
    // Information is the record-data byte count.
    if (input_message.complete.IoStatus.Information != 4 * sizeof(INPUT_RECORD) ||
        input_message.complete.Write.Size !=
            sizeof(CONSOLE_GETCONSOLEINPUT_MSG) + 4 * sizeof(INPUT_RECORD) ||
        input_message.complete.Write.Data != input_message.body + sizeof(CONSOLE_MSG_HEADER))
    {
        std::fprintf(stderr, "ReadConsoleInput completion layout failed\n");
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

    // Win32Input must stay structured even when the application selected
    // ENABLE_VIRTUAL_TERMINAL_INPUT. The raw CSI must not be re-decoded into
    // individual 'E', '[', '6', ... key events.
    corehost::corehost2::session win32_input_session{8, 2};
    win32_input_session.feed_input(
        std::u16string_view{u"\x1b[65;30;65;1;16;1_\x1b[65;30;65;0;16;1_"}, true);
    if (win32_input_session.input().size() != 2)
    {
        std::fprintf(stderr, "Win32Input raw_passthrough produced wrong event count\n");
        return EXIT_FAILURE;
    }
    std::array<corehost::consoleinput::input_event, 2> win32_input_events{};
    const auto win32_count = win32_input_session.input().peek(win32_input_events);
    if (win32_count != 2 || win32_input_events[0].key.character != u'A' ||
        win32_input_events[0].key.control_state != 0x10 || !win32_input_events[0].key.key_down ||
        win32_input_events[1].key.key_down)
    {
        std::fprintf(stderr, "Win32Input raw_passthrough lost VK/Uc/ControlKeyState\n");
        return EXIT_FAILURE;
    }

    std::printf("libcorehost2 tests passed\n");
    return EXIT_SUCCESS;
}