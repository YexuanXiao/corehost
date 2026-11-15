#include "console_api.hpp"

#include <cstdio>
#include <cstdlib>

int main()
{
    corehost::screen::screen_buffer buffer{8, 2};
    corehost::screen::console_state state{8, 2};
    corehost::consoleapi::console_api api{buffer, state};

    if (!api.write_text(u"AB"))
    {
        std::fprintf(stderr, "write_text failed\n");
        return EXIT_FAILURE;
    }
    if (buffer.at(0, 0).primary() != u'A' || state.cursor().x != 2)
    {
        std::fprintf(stderr, "write_text state failed\n");
        return EXIT_FAILURE;
    }

    corehost::interop::win32::char_info cells[2]{};
    if (api.read_cells(corehost::screen::rect{0, 0, 1, 0}, cells) != 2)
    {
        std::fprintf(stderr, "read_cells failed\n");
        return EXIT_FAILURE;
    }
    if (cells[0].char_data.unicode_char != u'A' || cells[1].char_data.unicode_char != u'B')
    {
        std::fprintf(stderr, "read_cells data failed\n");
        return EXIT_FAILURE;
    }

    api.fill_cells(corehost::screen::rect{2, 0, 4, 0}, u'X', 0x1F);
    if (buffer.at(2, 0).primary() != u'X' ||
        buffer.attr_at(corehost::screen::coord{4, 0}) != 0x1F)
    {
        std::fprintf(stderr, "fill_cells failed\n");
        return EXIT_FAILURE;
    }

    (void)buffer.write_scalar(0, 1, u'A', corehost::screen::cell_style{});
    api.scroll_cells(corehost::screen::rectangle{0, 1, 1, 1},
                     corehost::screen::rectangle{}, false, corehost::screen::coord{1, 1});
    if (buffer.at(1, 1).primary() != u'A')
    {
        std::fprintf(stderr, "scroll_cells failed\n");
        return EXIT_FAILURE;
    }

    corehost::coreio::io_msg message{};
    message.descriptor.InputSize = sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_SCREENBUFFERINFO_MSG);
    message.descriptor.OutputSize = sizeof(message.body);
    auto *header = reinterpret_cast<CONSOLE_MSG_HEADER *>(message.body);
    header->ApiNumber = ConsolepGetScreenBufferInfo;
    if (!api.handle(message) || message.complete.IoStatus.Status != 0 ||
        message.complete.IoStatus.Information != sizeof(CONSOLE_SCREENBUFFERINFO_MSG))
    {
        std::fprintf(stderr, "screen buffer info failed\n");
        return EXIT_FAILURE;
    }
    const auto *info = reinterpret_cast<const CONSOLE_SCREENBUFFERINFO_MSG *>(
        message.body + sizeof(CONSOLE_MSG_HEADER));
    if (info->Size.X != 8 || info->Size.Y != 2)
    {
        std::fprintf(stderr, "screen buffer info size failed\n");
        return EXIT_FAILURE;
    }

    corehost::coreio::io_msg cursor_message{};
    cursor_message.descriptor.InputSize = sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_GETCURSORINFO_MSG);
    cursor_message.descriptor.OutputSize = sizeof(cursor_message.body);
    auto *cursor_header = reinterpret_cast<CONSOLE_MSG_HEADER *>(cursor_message.body);
    cursor_header->ApiNumber = ConsolepGetCursorInfo;
    if (!api.handle(cursor_message) || cursor_message.complete.IoStatus.Status != 0)
    {
        std::fprintf(stderr, "cursor info failed\n");
        return EXIT_FAILURE;
    }
    const auto *cursor_info = reinterpret_cast<const CONSOLE_GETCURSORINFO_MSG *>(
        cursor_message.body + sizeof(CONSOLE_MSG_HEADER));
    if (!cursor_info->Visible || cursor_info->CursorSize == 0)
    {
        std::fprintf(stderr, "cursor info payload failed\n");
        return EXIT_FAILURE;
    }

    corehost::coreio::io_msg position_message{};
    position_message.descriptor.InputSize =
        sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_SETCURSORPOSITION_MSG);
    position_message.descriptor.OutputSize = sizeof(position_message.body);
    auto *position_header = reinterpret_cast<CONSOLE_MSG_HEADER *>(position_message.body);
    position_header->ApiNumber = ConsolepSetCursorPosition;
    auto *position = reinterpret_cast<CONSOLE_SETCURSORPOSITION_MSG *>(
        position_message.body + sizeof(CONSOLE_MSG_HEADER));
    position->CursorPosition = COORD{3, 1};
    if (!api.handle(position_message) || state.cursor().x != 3 || state.cursor().y != 1)
    {
        std::fprintf(stderr, "set cursor position failed\n");
        return EXIT_FAILURE;
    }

    corehost::coreio::io_msg cursor_info_message{};
    cursor_info_message.descriptor.InputSize =
        sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_SETCURSORINFO_MSG);
    cursor_info_message.descriptor.OutputSize = sizeof(cursor_info_message.body);
    auto *cursor_info_header = reinterpret_cast<CONSOLE_MSG_HEADER *>(cursor_info_message.body);
    cursor_info_header->ApiNumber = ConsolepSetCursorInfo;
    auto *cursor_set = reinterpret_cast<CONSOLE_SETCURSORINFO_MSG *>(
        cursor_info_message.body + sizeof(CONSOLE_MSG_HEADER));
    cursor_set->Visible = FALSE;
    if (!api.handle(cursor_info_message) || state.cursor().visible)
    {
        std::fprintf(stderr, "set cursor info failed\n");
        return EXIT_FAILURE;
    }

    corehost::coreio::io_msg write_message{};
    write_message.descriptor.InputSize = sizeof(CONSOLE_MSG_HEADER) +
                                         sizeof(CONSOLE_WRITECONSOLEOUTPUT_MSG) +
                                         sizeof(corehost::interop::win32::char_info);
    write_message.descriptor.OutputSize = sizeof(write_message.body);
    auto *write_header = reinterpret_cast<CONSOLE_MSG_HEADER *>(write_message.body);
    write_header->ApiNumber = ConsolepWriteConsoleOutput;
    auto *write_request = reinterpret_cast<CONSOLE_WRITECONSOLEOUTPUT_MSG *>(
        write_message.body + sizeof(CONSOLE_MSG_HEADER));
    write_request->CharRegion = SMALL_RECT{0, 0, 0, 0};
    write_request->Unicode = TRUE;
    auto *write_cell = reinterpret_cast<corehost::interop::win32::char_info *>(
        write_message.body + sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_WRITECONSOLEOUTPUT_MSG));
    write_cell->char_data.unicode_char = u'Z';
    write_cell->attributes = 0x07;
    if (!api.handle(write_message) || buffer.at(0, 0).primary() != u'Z')
    {
        std::fprintf(stderr, "write console output failed\n");
        return EXIT_FAILURE;
    }

    corehost::coreio::io_msg read_message{};
    read_message.descriptor.InputSize =
        sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_READCONSOLEOUTPUT_MSG);
    read_message.descriptor.OutputSize =
        sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_READCONSOLEOUTPUT_MSG) +
        sizeof(corehost::interop::win32::char_info);
    auto *read_header = reinterpret_cast<CONSOLE_MSG_HEADER *>(read_message.body);
    read_header->ApiNumber = ConsolepReadConsoleOutput;
    auto *read_request = reinterpret_cast<CONSOLE_READCONSOLEOUTPUT_MSG *>(
        read_message.body + sizeof(CONSOLE_MSG_HEADER));
    read_request->CharRegion = SMALL_RECT{0, 0, 0, 0};
    read_request->Unicode = TRUE;
    if (!api.handle(read_message) ||
        read_message.complete.IoStatus.Information != sizeof(corehost::interop::win32::char_info))
    {
        std::fprintf(stderr, "read console output failed\n");
        return EXIT_FAILURE;
    }
    const auto *read_cell = reinterpret_cast<const corehost::interop::win32::char_info *>(
        read_message.body + sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_READCONSOLEOUTPUT_MSG));
    if (read_cell->char_data.unicode_char != u'Z')
    {
        std::fprintf(stderr, "read console output data failed\n");
        return EXIT_FAILURE;
    }

    corehost::coreio::io_msg fill_message{};
    fill_message.descriptor.InputSize =
        sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_FILLCONSOLEOUTPUT_MSG);
    fill_message.descriptor.OutputSize = sizeof(fill_message.body);
    auto *fill_header = reinterpret_cast<CONSOLE_MSG_HEADER *>(fill_message.body);
    fill_header->ApiNumber = ConsolepFillConsoleOutput;
    auto *fill_request = reinterpret_cast<CONSOLE_FILLCONSOLEOUTPUT_MSG *>(
        fill_message.body + sizeof(CONSOLE_MSG_HEADER));
    fill_request->WriteCoord = COORD{1, 1};
    fill_request->ElementType = CONSOLE_REAL_UNICODE;
    fill_request->Element = u'F';
    fill_request->Length = 3;
    if (!api.handle(fill_message) || buffer.at(1, 1).primary() != u'F' ||
        buffer.at(3, 1).primary() != u'F' ||
        fill_message.complete.IoStatus.Information != 3)
    {
        std::fprintf(stderr, "fill console output failed\n");
        return EXIT_FAILURE;
    }

    constexpr char16_t string_data[]{u'H', u'I'};
    corehost::coreio::io_msg string_message{};
    string_message.descriptor.InputSize = sizeof(CONSOLE_MSG_HEADER) +
                                          sizeof(CONSOLE_WRITECONSOLEOUTPUTSTRING_MSG) +
                                          sizeof(string_data);
    string_message.descriptor.OutputSize = sizeof(string_message.body);
    auto *string_header = reinterpret_cast<CONSOLE_MSG_HEADER *>(string_message.body);
    string_header->ApiNumber = ConsolepWriteConsoleOutputString;
    auto *string_request = reinterpret_cast<CONSOLE_WRITECONSOLEOUTPUTSTRING_MSG *>(
        string_message.body + sizeof(CONSOLE_MSG_HEADER));
    string_request->WriteCoord = COORD{0, 0};
    string_request->StringType = CONSOLE_REAL_UNICODE;
    string_request->NumRecords = 2;
    std::memcpy(string_message.body + sizeof(CONSOLE_MSG_HEADER) +
                    sizeof(CONSOLE_WRITECONSOLEOUTPUTSTRING_MSG),
                string_data, sizeof(string_data));
    if (!api.handle(string_message) || buffer.at(0, 0).primary() != u'H' ||
        buffer.at(1, 0).primary() != u'I' || string_request->NumRecords != 2)
    {
        std::fprintf(stderr, "write console output string failed\n");
        return EXIT_FAILURE;
    }

    corehost::coreio::io_msg read_string_message{};
    read_string_message.descriptor.InputSize =
        sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_READCONSOLEOUTPUTSTRING_MSG);
    read_string_message.descriptor.OutputSize =
        sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_READCONSOLEOUTPUTSTRING_MSG) +
        2 * sizeof(char16_t);
    auto *read_string_header = reinterpret_cast<CONSOLE_MSG_HEADER *>(read_string_message.body);
    read_string_header->ApiNumber = ConsolepReadConsoleOutputString;
    auto *read_string_request = reinterpret_cast<CONSOLE_READCONSOLEOUTPUTSTRING_MSG *>(
        read_string_message.body + sizeof(CONSOLE_MSG_HEADER));
    read_string_request->ReadCoord = COORD{0, 0};
    read_string_request->StringType = CONSOLE_REAL_UNICODE;
    read_string_request->NumRecords = 2;
    if (!api.handle(read_string_message) || read_string_request->NumRecords != 2 ||
        read_string_message.complete.IoStatus.Information != 2 * sizeof(char16_t))
    {
        std::fprintf(stderr, "read console output string failed\n");
        return EXIT_FAILURE;
    }
    const auto *read_string_data = reinterpret_cast<const char16_t *>(
        read_string_message.body + sizeof(CONSOLE_MSG_HEADER) +
        sizeof(CONSOLE_READCONSOLEOUTPUTSTRING_MSG));
    if (read_string_data[0] != u'H' || read_string_data[1] != u'I')
    {
        std::fprintf(stderr, "read console output string data failed\n");
        return EXIT_FAILURE;
    }

    corehost::coreio::io_msg resize_message{};
    resize_message.descriptor.InputSize =
        sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_SETSCREENBUFFERSIZE_MSG);
    resize_message.descriptor.OutputSize = sizeof(resize_message.body);
    auto *resize_header = reinterpret_cast<CONSOLE_MSG_HEADER *>(resize_message.body);
    resize_header->ApiNumber = ConsolepSetScreenBufferSize;
    auto *resize_request = reinterpret_cast<CONSOLE_SETSCREENBUFFERSIZE_MSG *>(
        resize_message.body + sizeof(CONSOLE_MSG_HEADER));
    resize_request->Size = COORD{6, 3};
    if (!api.handle(resize_message) || buffer.buffer_size() != corehost::screen::coord{6, 3} ||
        state.width() != 6 || state.height() != 3)
    {
        std::fprintf(stderr, "set screen buffer size failed\n");
        return EXIT_FAILURE;
    }

    std::printf("libconsoleapi tests passed\n");
    return EXIT_SUCCESS;
}