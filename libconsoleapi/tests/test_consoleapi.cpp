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

    std::printf("libconsoleapi tests passed\n");
    return EXIT_SUCCESS;
}