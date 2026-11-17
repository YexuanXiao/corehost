#include "console_api.hpp"

#include <cstdio>
#include <utility>
#include <vector>
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
    if (buffer.at(2, 0).primary() != u'X' || buffer.attr_at(corehost::screen::coord{4, 0}) != 0x1F)
    {
        std::fprintf(stderr, "fill_cells failed\n");
        return EXIT_FAILURE;
    }

    (void)buffer.write_scalar(0, 1, u'A', corehost::screen::cell_style{});
    api.scroll_cells(corehost::screen::rectangle{0, 1, 1, 1}, corehost::screen::rectangle{}, false,
                     corehost::screen::coord{1, 1});
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
    const auto *info =
        reinterpret_cast<const CONSOLE_SCREENBUFFERINFO_MSG *>(message.body + sizeof(CONSOLE_MSG_HEADER));
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
    const auto *cursor_info =
        reinterpret_cast<const CONSOLE_GETCURSORINFO_MSG *>(cursor_message.body + sizeof(CONSOLE_MSG_HEADER));
    if (!cursor_info->Visible || cursor_info->CursorSize == 0)
    {
        std::fprintf(stderr, "cursor info payload failed\n");
        return EXIT_FAILURE;
    }

    corehost::coreio::io_msg position_message{};
    position_message.descriptor.InputSize = sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_SETCURSORPOSITION_MSG);
    position_message.descriptor.OutputSize = sizeof(position_message.body);
    auto *position_header = reinterpret_cast<CONSOLE_MSG_HEADER *>(position_message.body);
    position_header->ApiNumber = ConsolepSetCursorPosition;
    auto *position =
        reinterpret_cast<CONSOLE_SETCURSORPOSITION_MSG *>(position_message.body + sizeof(CONSOLE_MSG_HEADER));
    position->CursorPosition = COORD{3, 1};
    if (!api.handle(position_message) || state.cursor().x != 3 || state.cursor().y != 1)
    {
        std::fprintf(stderr, "set cursor position failed\n");
        return EXIT_FAILURE;
    }

    corehost::coreio::io_msg cursor_info_message{};
    cursor_info_message.descriptor.InputSize = sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_SETCURSORINFO_MSG);
    cursor_info_message.descriptor.OutputSize = sizeof(cursor_info_message.body);
    auto *cursor_info_header = reinterpret_cast<CONSOLE_MSG_HEADER *>(cursor_info_message.body);
    cursor_info_header->ApiNumber = ConsolepSetCursorInfo;
    auto *cursor_set =
        reinterpret_cast<CONSOLE_SETCURSORINFO_MSG *>(cursor_info_message.body + sizeof(CONSOLE_MSG_HEADER));
    cursor_set->CursorSize = 50;
    cursor_set->Visible = FALSE;
    if (!api.handle(cursor_info_message) || state.cursor().visible || state.cursor().size != 50)
    {
        std::fprintf(stderr, "set cursor info failed\n");
        return EXIT_FAILURE;
    }

    corehost::coreio::io_msg write_message{};
    write_message.descriptor.InputSize = sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_WRITECONSOLEOUTPUT_MSG) +
                                         sizeof(corehost::interop::win32::char_info);
    write_message.descriptor.OutputSize = sizeof(write_message.body);
    auto *write_header = reinterpret_cast<CONSOLE_MSG_HEADER *>(write_message.body);
    write_header->ApiNumber = ConsolepWriteConsoleOutput;
    auto *write_request =
        reinterpret_cast<CONSOLE_WRITECONSOLEOUTPUT_MSG *>(write_message.body + sizeof(CONSOLE_MSG_HEADER));
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
    read_message.descriptor.InputSize = sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_READCONSOLEOUTPUT_MSG);
    read_message.descriptor.OutputSize = sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_READCONSOLEOUTPUT_MSG) +
                                         sizeof(corehost::interop::win32::char_info);
    auto *read_header = reinterpret_cast<CONSOLE_MSG_HEADER *>(read_message.body);
    read_header->ApiNumber = ConsolepReadConsoleOutput;
    auto *read_request =
        reinterpret_cast<CONSOLE_READCONSOLEOUTPUT_MSG *>(read_message.body + sizeof(CONSOLE_MSG_HEADER));
    read_request->CharRegion = SMALL_RECT{0, 0, 0, 0};
    read_request->Unicode = TRUE;
    if (!api.handle(read_message) ||
        read_message.complete.IoStatus.Information != sizeof(corehost::interop::win32::char_info) ||
        read_message.complete.Write.Size !=
            sizeof(CONSOLE_READCONSOLEOUTPUT_MSG) + sizeof(corehost::interop::win32::char_info))
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
    fill_message.descriptor.InputSize = sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_FILLCONSOLEOUTPUT_MSG);
    fill_message.descriptor.OutputSize = sizeof(fill_message.body);
    auto *fill_header = reinterpret_cast<CONSOLE_MSG_HEADER *>(fill_message.body);
    fill_header->ApiNumber = ConsolepFillConsoleOutput;
    auto *fill_request =
        reinterpret_cast<CONSOLE_FILLCONSOLEOUTPUT_MSG *>(fill_message.body + sizeof(CONSOLE_MSG_HEADER));
    fill_request->WriteCoord = COORD{1, 1};
    fill_request->ElementType = CONSOLE_REAL_UNICODE;
    fill_request->Element = u'F';
    fill_request->Length = 3;
    if (!api.handle(fill_message) || buffer.at(1, 1).primary() != u'F' || buffer.at(3, 1).primary() != u'F' ||
        fill_message.complete.IoStatus.Information != 3)
    {
        std::fprintf(stderr, "fill console output failed\n");
        return EXIT_FAILURE;
    }

    constexpr char16_t string_data[]{u'H', u'I'};
    corehost::coreio::io_msg string_message{};
    string_message.descriptor.InputSize =
        sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_WRITECONSOLEOUTPUTSTRING_MSG) + sizeof(string_data);
    string_message.descriptor.OutputSize = sizeof(string_message.body);
    auto *string_header = reinterpret_cast<CONSOLE_MSG_HEADER *>(string_message.body);
    string_header->ApiNumber = ConsolepWriteConsoleOutputString;
    auto *string_request =
        reinterpret_cast<CONSOLE_WRITECONSOLEOUTPUTSTRING_MSG *>(string_message.body + sizeof(CONSOLE_MSG_HEADER));
    string_request->WriteCoord = COORD{0, 0};
    string_request->StringType = CONSOLE_REAL_UNICODE;
    string_request->NumRecords = 0;
    std::memcpy(string_message.body + sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_WRITECONSOLEOUTPUTSTRING_MSG),
                string_data, sizeof(string_data));
    if (!api.handle(string_message) || buffer.at(0, 0).primary() != u'H' || buffer.at(1, 0).primary() != u'I' ||
        string_request->NumRecords != 2)
    {
        std::fprintf(stderr, "write console output string failed\n");
        return EXIT_FAILURE;
    }

    corehost::coreio::io_msg read_string_message{};
    read_string_message.descriptor.InputSize = sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_READCONSOLEOUTPUTSTRING_MSG);
    read_string_message.descriptor.OutputSize =
        sizeof(CONSOLE_READCONSOLEOUTPUTSTRING_MSG) + 2 * sizeof(char16_t);
    auto *read_string_header = reinterpret_cast<CONSOLE_MSG_HEADER *>(read_string_message.body);
    read_string_header->ApiNumber = ConsolepReadConsoleOutputString;
    auto *read_string_request =
        reinterpret_cast<CONSOLE_READCONSOLEOUTPUTSTRING_MSG *>(read_string_message.body + sizeof(CONSOLE_MSG_HEADER));
    read_string_request->ReadCoord = COORD{0, 0};
    read_string_request->StringType = CONSOLE_REAL_UNICODE;
    read_string_request->NumRecords = 0;
    if (!api.handle(read_string_message) || read_string_request->NumRecords != 2 ||
        read_string_message.complete.IoStatus.Information != 2 * sizeof(char16_t) ||
        read_string_message.complete.Write.Size !=
            sizeof(CONSOLE_READCONSOLEOUTPUTSTRING_MSG) + 2 * sizeof(char16_t))
    {
        std::fprintf(stderr, "read console output string failed\n");
        return EXIT_FAILURE;
    }
    const auto *read_string_data = reinterpret_cast<const char16_t *>(
        read_string_message.body + sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_READCONSOLEOUTPUTSTRING_MSG));
    if (read_string_data[0] != u'H' || read_string_data[1] != u'I')
    {
        std::fprintf(stderr, "read console output string data failed\n");
        return EXIT_FAILURE;
    }

    corehost::coreio::io_msg resize_message{};
    resize_message.descriptor.InputSize = sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_SETSCREENBUFFERSIZE_MSG);
    resize_message.descriptor.OutputSize = sizeof(resize_message.body);
    auto *resize_header = reinterpret_cast<CONSOLE_MSG_HEADER *>(resize_message.body);
    resize_header->ApiNumber = ConsolepSetScreenBufferSize;
    auto *resize_request =
        reinterpret_cast<CONSOLE_SETSCREENBUFFERSIZE_MSG *>(resize_message.body + sizeof(CONSOLE_MSG_HEADER));
    resize_request->Size = COORD{6, 3};
    if (!api.handle(resize_message) || buffer.buffer_size() != corehost::screen::coord{6, 3} || state.width() != 6 ||
        state.height() != 3)
    {
        std::fprintf(stderr, "set screen buffer size failed\n");
        return EXIT_FAILURE;
    }

    corehost::consoleinput::input_queue input_queue;
    input_queue.push(corehost::consoleinput::input_event{});
    corehost::consoleapi::console_api input_api{buffer, state, &input_queue};
    corehost::coreio::io_msg event_message{};
    event_message.descriptor.InputSize = sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_GETNUMBEROFINPUTEVENTS_MSG);
    event_message.descriptor.OutputSize = sizeof(event_message.body);
    auto *event_header = reinterpret_cast<CONSOLE_MSG_HEADER *>(event_message.body);
    event_header->ApiNumber = ConsolepGetNumberOfInputEvents;
    if (!input_api.handle(event_message) || input_queue.record_count() != 1)
    {
        std::fprintf(stderr, "input event count failed\n");
        return EXIT_FAILURE;
    }
    const auto *event_info =
        reinterpret_cast<const CONSOLE_GETNUMBEROFINPUTEVENTS_MSG *>(event_message.body + sizeof(CONSOLE_MSG_HEADER));
    if (event_info->ReadyEvents != 1)
    {
        std::fprintf(stderr, "input event count wrong\n");
        return EXIT_FAILURE;
    }

    corehost::coreio::io_msg read_input_message{};
    read_input_message.descriptor.InputSize = sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_GETCONSOLEINPUT_MSG);
    read_input_message.descriptor.OutputSize =
        sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_GETCONSOLEINPUT_MSG) + sizeof(INPUT_RECORD);
    auto *read_input_header = reinterpret_cast<CONSOLE_MSG_HEADER *>(read_input_message.body);
    read_input_header->ApiNumber = ConsolepGetConsoleInput;
    auto *read_input_request =
        reinterpret_cast<CONSOLE_GETCONSOLEINPUT_MSG *>(read_input_message.body + sizeof(CONSOLE_MSG_HEADER));
    read_input_request->Flags = 0;
    read_input_request->Unicode = TRUE;
    if (!input_api.handle(read_input_message) || read_input_request->NumRecords != 1)
    {
        std::fprintf(stderr, "read console input failed\n");
        return EXIT_FAILURE;
    }
    const auto *read_input_record = reinterpret_cast<const INPUT_RECORD *>(
        read_input_message.body + sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_GETCONSOLEINPUT_MSG));
    if (read_input_record->EventType != KEY_EVENT)
    {
        std::fprintf(stderr, "read console input type failed\n");
        return EXIT_FAILURE;
    }

    corehost::coreio::io_msg code_page_message{};
    code_page_message.descriptor.InputSize = sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_SETCP_MSG);
    code_page_message.descriptor.OutputSize = sizeof(code_page_message.body);
    auto *code_page_header = reinterpret_cast<CONSOLE_MSG_HEADER *>(code_page_message.body);
    code_page_header->ApiNumber = ConsolepSetCP;
    auto *code_page_request =
        reinterpret_cast<CONSOLE_SETCP_MSG *>(code_page_message.body + sizeof(CONSOLE_MSG_HEADER));
    code_page_request->CodePage = 1252;
    code_page_request->Output = FALSE;
    if (!api.handle(code_page_message) || state.input_code_page != 1252)
    {
        std::fprintf(stderr, "set code page failed\n");
        return EXIT_FAILURE;
    }

    corehost::coreio::io_msg get_code_page_message{};
    get_code_page_message.descriptor.InputSize = sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_GETCP_MSG);
    get_code_page_message.descriptor.OutputSize = sizeof(get_code_page_message.body);
    auto *get_code_page_header = reinterpret_cast<CONSOLE_MSG_HEADER *>(get_code_page_message.body);
    get_code_page_header->ApiNumber = ConsolepGetCP;
    auto *get_code_page_request =
        reinterpret_cast<CONSOLE_GETCP_MSG *>(get_code_page_message.body + sizeof(CONSOLE_MSG_HEADER));
    get_code_page_request->Output = FALSE;
    if (!api.handle(get_code_page_message) || get_code_page_request->CodePage != 1252)
    {
        std::fprintf(stderr, "get code page failed\n");
        return EXIT_FAILURE;
    }

    constexpr char16_t title_text[]{u'C', u'H'};
    corehost::coreio::io_msg title_message{};
    title_message.descriptor.InputSize = sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_SETTITLE_MSG) + sizeof(title_text);
    title_message.descriptor.OutputSize = sizeof(title_message.body);
    auto *title_header = reinterpret_cast<CONSOLE_MSG_HEADER *>(title_message.body);
    title_header->ApiNumber = ConsolepSetTitle;
    auto *title_request = reinterpret_cast<CONSOLE_SETTITLE_MSG *>(title_message.body + sizeof(CONSOLE_MSG_HEADER));
    title_request->Unicode = TRUE;
    std::memcpy(title_message.body + sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_SETTITLE_MSG), title_text,
                sizeof(title_text));
    if (!api.handle(title_message) || state.title != u"CH")
    {
        std::fprintf(stderr, "set title failed\n");
        return EXIT_FAILURE;
    }

    corehost::coreio::io_msg get_title_message{};
    get_title_message.descriptor.InputSize = sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_GETTITLE_MSG);
    get_title_message.descriptor.OutputSize =
        sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_GETTITLE_MSG) + 2 * sizeof(char16_t);
    auto *get_title_header = reinterpret_cast<CONSOLE_MSG_HEADER *>(get_title_message.body);
    get_title_header->ApiNumber = ConsolepGetTitle;
    auto *get_title_request =
        reinterpret_cast<CONSOLE_GETTITLE_MSG *>(get_title_message.body + sizeof(CONSOLE_MSG_HEADER));
    get_title_request->Unicode = TRUE;
    get_title_request->Original = FALSE;
    if (!api.handle(get_title_message) || get_title_request->TitleLength != 2)
    {
        std::fprintf(stderr, "get title failed\n");
        return EXIT_FAILURE;
    }
    const auto *title_data = reinterpret_cast<const char16_t *>(get_title_message.body + sizeof(CONSOLE_MSG_HEADER) +
                                                                sizeof(CONSOLE_GETTITLE_MSG));
    if (title_data[0] != u'C' || title_data[1] != u'H')
    {
        std::fprintf(stderr, "get title data failed\n");
        return EXIT_FAILURE;
    }

    corehost::coreio::io_msg selection_message{};
    selection_message.descriptor.InputSize = sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_GETSELECTIONINFO_MSG);
    selection_message.descriptor.OutputSize = sizeof(selection_message.body);
    auto *selection_header = reinterpret_cast<CONSOLE_MSG_HEADER *>(selection_message.body);
    selection_header->ApiNumber = ConsolepGetSelectionInfo;
    if (!api.handle(selection_message))
    {
        std::fprintf(stderr, "get selection info failed\n");
        return EXIT_FAILURE;
    }
    const auto *selection_info =
        reinterpret_cast<const CONSOLE_GETSELECTIONINFO_MSG *>(selection_message.body + sizeof(CONSOLE_MSG_HEADER));
    if (selection_info->SelectionInfo.dwFlags != 0)
    {
        std::fprintf(stderr, "selection flags unexpected\n");
        return EXIT_FAILURE;
    }

    INPUT_RECORD input_record{};
    input_record.EventType = KEY_EVENT;
    input_record.Event.KeyEvent.bKeyDown = TRUE;
    input_record.Event.KeyEvent.wRepeatCount = 1;
    input_record.Event.KeyEvent.wVirtualKeyCode = 0x41;
    input_record.Event.KeyEvent.uChar.UnicodeChar = u'A';
    corehost::coreio::io_msg write_input_message{};
    write_input_message.descriptor.InputSize =
        sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_WRITECONSOLEINPUT_MSG) + sizeof(input_record);
    write_input_message.descriptor.OutputSize = sizeof(write_input_message.body);
    auto *write_input_header = reinterpret_cast<CONSOLE_MSG_HEADER *>(write_input_message.body);
    write_input_header->ApiNumber = ConsolepWriteConsoleInput;
    auto *write_input_request =
        reinterpret_cast<CONSOLE_WRITECONSOLEINPUT_MSG *>(write_input_message.body + sizeof(CONSOLE_MSG_HEADER));
    write_input_request->NumRecords = 1;
    write_input_request->Unicode = TRUE;
    write_input_request->Append = TRUE;
    auto *write_input_record = reinterpret_cast<INPUT_RECORD *>(write_input_message.body + sizeof(CONSOLE_MSG_HEADER) +
                                                                sizeof(CONSOLE_WRITECONSOLEINPUT_MSG));
    *write_input_record = input_record;
    if (!input_api.handle(write_input_message) || write_input_request->NumRecords != 1 || input_queue.record_count() != 1)
    {
        std::fprintf(stderr, "write console input failed\n");
        return EXIT_FAILURE;
    }

    corehost::screen::screen_buffer scroll_buffer{4, 2};
    corehost::screen::console_state scroll_state{4, 2};
    corehost::consoleapi::console_api scroll_api{scroll_buffer, scroll_state};
    (void)scroll_buffer.write_scalar(0, 0, u'A', corehost::screen::cell_style{});
    corehost::coreio::io_msg scroll_message{};
    scroll_message.descriptor.InputSize = sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_SCROLLSCREENBUFFER_MSG);
    scroll_message.descriptor.OutputSize = sizeof(scroll_message.body);
    auto *scroll_header = reinterpret_cast<CONSOLE_MSG_HEADER *>(scroll_message.body);
    scroll_header->ApiNumber = ConsolepScrollScreenBuffer;
    auto *scroll_request =
        reinterpret_cast<CONSOLE_SCROLLSCREENBUFFER_MSG *>(scroll_message.body + sizeof(CONSOLE_MSG_HEADER));
    scroll_request->ScrollRectangle = SMALL_RECT{0, 0, 0, 0};
    scroll_request->ClipRectangle = SMALL_RECT{0, 0, 3, 1};
    scroll_request->Clip = FALSE;
    scroll_request->Unicode = TRUE;
    scroll_request->DestinationOrigin = COORD{1, 0};
    scroll_request->Fill.Char.UnicodeChar = u'X';
    scroll_request->Fill.Attributes = 0x07;
    if (!scroll_api.handle(scroll_message) || scroll_buffer.at(1, 0).primary() != u'A' ||
        scroll_buffer.at(0, 0).primary() != u'X')
    {
        std::fprintf(stderr, "scroll console buffer failed\n");
        return EXIT_FAILURE;
    }

    corehost::coreio::io_msg lang_message{};
    lang_message.descriptor.InputSize = sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_LANGID_MSG);
    lang_message.descriptor.OutputSize = sizeof(lang_message.body);
    auto *lang_header = reinterpret_cast<CONSOLE_MSG_HEADER *>(lang_message.body);
    lang_header->ApiNumber = ConsolepGetLangId;
    if (!api.handle(lang_message))
    {
        std::fprintf(stderr, "get language id failed\n");
        return EXIT_FAILURE;
    }
    const auto *lang_info =
        reinterpret_cast<const CONSOLE_LANGID_MSG *>(lang_message.body + sizeof(CONSOLE_MSG_HEADER));
    const auto acp = ::GetACP();
    if (acp == 932 || acp == 936 || acp == 949 || acp == 950)
    {
        if (lang_info->LangId == 0)
        {
            std::fprintf(stderr, "get language id data failed\n");
            return EXIT_FAILURE;
        }
    }
    else if (lang_message.complete.IoStatus.Status != corehost::coreio::error::status_not_supported)
    {
        std::fprintf(stderr, "get language id should be unsupported\n");
        return EXIT_FAILURE;
    }

    corehost::screen::screen_buffer write_buffer{4, 2};
    corehost::screen::console_state write_state{4, 2};
    corehost::consoleapi::console_api write_api{write_buffer, write_state};
    constexpr char16_t write_text[]{u'O', u'K'};
    corehost::coreio::io_msg write_console_message{};
    write_console_message.descriptor.InputSize =
        sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_WRITECONSOLE_MSG) + sizeof(write_text);
    write_console_message.descriptor.OutputSize = sizeof(write_console_message.body);
    auto *write_console_header = reinterpret_cast<CONSOLE_MSG_HEADER *>(write_console_message.body);
    write_console_header->ApiNumber = ConsolepWriteConsole;
    auto *write_console_request =
        reinterpret_cast<CONSOLE_WRITECONSOLE_MSG *>(write_console_message.body + sizeof(CONSOLE_MSG_HEADER));
    write_console_request->Unicode = TRUE;
    std::memcpy(write_console_message.body + sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_WRITECONSOLE_MSG), write_text,
                sizeof(write_text));
    if (!write_api.handle(write_console_message) || write_buffer.at(0, 0).primary() != u'O' ||
        write_buffer.at(1, 0).primary() != u'K' || write_state.cursor().x != 2)
    {
        std::fprintf(stderr, "write console failed\n");
        return EXIT_FAILURE;
    }

    corehost::screen::screen_buffer window_buffer{8, 2};
    corehost::screen::console_state window_state{8, 2};
    corehost::consoleapi::console_api window_api{window_buffer, window_state};
    corehost::coreio::io_msg window_message{};
    window_message.descriptor.InputSize = sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_SETWINDOWINFO_MSG);
    window_message.descriptor.OutputSize = sizeof(window_message.body);
    auto *window_header = reinterpret_cast<CONSOLE_MSG_HEADER *>(window_message.body);
    window_header->ApiNumber = ConsolepSetWindowInfo;
    auto *window_request =
        reinterpret_cast<CONSOLE_SETWINDOWINFO_MSG *>(window_message.body + sizeof(CONSOLE_MSG_HEADER));
    window_request->Absolute = TRUE;
    window_request->Window = SMALL_RECT{0, 0, 3, 1};
    if (!window_api.handle(window_message) || window_buffer.viewport_size() != corehost::screen::coord{4, 2})
    {
        std::fprintf(stderr, "set window info failed\n");
        return EXIT_FAILURE;
    }

    corehost::coreio::io_msg attribute_message{};
    attribute_message.descriptor.InputSize = sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_SETTEXTATTRIBUTE_MSG);
    attribute_message.descriptor.OutputSize = sizeof(attribute_message.body);
    auto *attribute_header = reinterpret_cast<CONSOLE_MSG_HEADER *>(attribute_message.body);
    attribute_header->ApiNumber = ConsolepSetTextAttribute;
    auto *attribute_request =
        reinterpret_cast<CONSOLE_SETTEXTATTRIBUTE_MSG *>(attribute_message.body + sizeof(CONSOLE_MSG_HEADER));
    attribute_request->Attributes = 0x1F;
    if (!api.handle(attribute_message) || state.default_attributes != 0x1F)
    {
        std::fprintf(stderr, "set text attribute failed\n");
        return EXIT_FAILURE;
    }

    corehost::coreio::io_msg read_console_message{};
    read_console_message.descriptor.InputSize = sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_READCONSOLE_MSG);
    read_console_message.descriptor.OutputSize =
        sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_READCONSOLE_MSG) + sizeof(char16_t);
    auto *read_console_header = reinterpret_cast<CONSOLE_MSG_HEADER *>(read_console_message.body);
    read_console_header->ApiNumber = ConsolepReadConsole;
    auto *read_console_request =
        reinterpret_cast<CONSOLE_READCONSOLE_MSG *>(read_console_message.body + sizeof(CONSOLE_MSG_HEADER));
    read_console_request->Unicode = TRUE;
    input_queue.push(corehost::consoleinput::input_event{corehost::consoleinput::input_event_type::key,
                                                         corehost::consoleinput::key_event{u'\r'}});
    if (!input_api.handle(read_console_message) || read_console_request->NumBytes != 3 * sizeof(char16_t))
    {
        std::fprintf(stderr, "read console failed\n");
        return EXIT_FAILURE;
    }
    const auto *read_console_data = reinterpret_cast<const char16_t *>(
        read_console_message.body + sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_READCONSOLE_MSG));
    if (read_console_data[0] != u'A')
    {
        std::fprintf(stderr, "read console data failed\n");
        return EXIT_FAILURE;
    }

    corehost::coreio::io_msg font_message{};
    font_message.descriptor.InputSize = sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_GETFONTSIZE_MSG);
    font_message.descriptor.OutputSize = sizeof(font_message.body);
    auto *font_header = reinterpret_cast<CONSOLE_MSG_HEADER *>(font_message.body);
    font_header->ApiNumber = ConsolepGetFontSize;
    auto *font_request = reinterpret_cast<CONSOLE_GETFONTSIZE_MSG *>(font_message.body + sizeof(CONSOLE_MSG_HEADER));
    font_request->FontIndex = 0;
    if (!api.handle(font_message) || font_request->FontSize.X != 8 || font_request->FontSize.Y != 12)
    {
        std::fprintf(stderr, "get font size failed\n");
        return EXIT_FAILURE;
    }

    corehost::coreio::io_msg current_font_message{};
    current_font_message.descriptor.InputSize = sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_CURRENTFONT_MSG);
    current_font_message.descriptor.OutputSize = sizeof(current_font_message.body);
    auto *current_font_header = reinterpret_cast<CONSOLE_MSG_HEADER *>(current_font_message.body);
    current_font_header->ApiNumber = ConsolepGetCurrentFont;
    if (!api.handle(current_font_message))
    {
        std::fprintf(stderr, "get current font failed\n");
        return EXIT_FAILURE;
    }
    const auto *current_font =
        reinterpret_cast<const CONSOLE_CURRENTFONT_MSG *>(current_font_message.body + sizeof(CONSOLE_MSG_HEADER));
    if (current_font->FontSize.X != 8 || current_font->FaceName[0] != u'C')
    {
        std::fprintf(stderr, "get current font data failed\n");
        return EXIT_FAILURE;
    }

    corehost::screen::screen_buffer info_buffer{8, 2};
    corehost::screen::console_state info_state{8, 2};
    corehost::consoleapi::console_api info_api{info_buffer, info_state};
    corehost::coreio::io_msg info_message{};
    info_message.descriptor.InputSize = sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_SCREENBUFFERINFO_MSG);
    info_message.descriptor.OutputSize = sizeof(info_message.body);
    auto *info_header = reinterpret_cast<CONSOLE_MSG_HEADER *>(info_message.body);
    info_header->ApiNumber = ConsolepSetScreenBufferInfo;
    auto *info_request =
        reinterpret_cast<CONSOLE_SCREENBUFFERINFO_MSG *>(info_message.body + sizeof(CONSOLE_MSG_HEADER));
    info_request->Size = COORD{6, 3};
    info_request->CursorPosition = COORD{1, 2};
    info_request->ScrollPosition = COORD{0, 0};
    info_request->Attributes = 0x1F;
    info_request->CurrentWindowSize = COORD{4, 2};
    info_request->MaximumWindowSize = COORD{6, 3};
    info_request->PopupAttributes = 0x07;
    if (!info_api.handle(info_message) || info_buffer.buffer_size() != corehost::screen::coord{6, 3} ||
        info_state.cursor().x != 0 || info_state.cursor().y != 0 || info_state.default_attributes != 0x1F ||
        info_buffer.viewport_size() != corehost::screen::coord{4, 2})
    {
        std::fprintf(stderr, "set screen buffer info failed\n");
        return EXIT_FAILURE;
    }

    corehost::coreio::io_msg mouse_input_message{};
    mouse_input_message.descriptor.InputSize = sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_GETMOUSEINFO_MSG);
    mouse_input_message.descriptor.OutputSize = sizeof(mouse_input_message.body);
    auto *mouse_header = reinterpret_cast<CONSOLE_MSG_HEADER *>(mouse_input_message.body);
    mouse_header->ApiNumber = ConsolepGetMouseInfo;
    if (!api.handle(mouse_input_message))
    {
        std::fprintf(stderr, "get mouse info failed\n");
        return EXIT_FAILURE;
    }
    const auto *mouse_info =
        reinterpret_cast<const CONSOLE_GETMOUSEINFO_MSG *>(mouse_input_message.body + sizeof(CONSOLE_MSG_HEADER));
    if (mouse_info->NumButtons != 5)
    {
        std::fprintf(stderr, "get mouse info data failed\n");
        return EXIT_FAILURE;
    }

    corehost::coreio::io_msg set_current_font_message{};
    set_current_font_message.descriptor.InputSize = sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_CURRENTFONT_MSG);
    set_current_font_message.descriptor.OutputSize = sizeof(set_current_font_message.body);
    auto *set_current_font_header = reinterpret_cast<CONSOLE_MSG_HEADER *>(set_current_font_message.body);
    set_current_font_header->ApiNumber = ConsolepSetCurrentFont;
    auto *set_current_font_request =
        reinterpret_cast<CONSOLE_CURRENTFONT_MSG *>(set_current_font_message.body + sizeof(CONSOLE_MSG_HEADER));
    set_current_font_request->FontIndex = 0;
    set_current_font_request->FontSize = COORD{10, 20};
    set_current_font_request->FontFamily = 54;
    set_current_font_request->FontWeight = 700;
    set_current_font_request->FaceName[0] = u'X';
    set_current_font_request->FaceName[1] = 0;
    if (!api.handle(set_current_font_message) || state.font_size.x != 10 || state.font_size.y != 20 ||
        state.face_name != u"X")
    {
        std::fprintf(stderr, "set current font failed\n");
        return EXIT_FAILURE;
    }

    api.set_output_object(1);
    corehost::coreio::io_msg set_mode_message{};
    set_mode_message.descriptor.InputSize = sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_MODE_MSG);
    set_mode_message.descriptor.OutputSize = sizeof(set_mode_message.body);
    auto *set_mode_header = reinterpret_cast<CONSOLE_MSG_HEADER *>(set_mode_message.body);
    set_mode_header->ApiNumber = ConsolepSetMode;
    set_mode_message.descriptor.Object = 1;
    auto *set_mode_request = reinterpret_cast<CONSOLE_MODE_MSG *>(set_mode_message.body + sizeof(CONSOLE_MSG_HEADER));
    set_mode_request->Mode = 0x7;
    if (!api.handle(set_mode_message) || state.output_mode != 0x7)
    {
        std::fprintf(stderr, "set console mode failed\n");
        return EXIT_FAILURE;
    }

    corehost::coreio::io_msg get_mode_message{};
    get_mode_message.descriptor.InputSize = sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_MODE_MSG);
    get_mode_message.descriptor.OutputSize = sizeof(get_mode_message.body);
    auto *get_mode_header = reinterpret_cast<CONSOLE_MSG_HEADER *>(get_mode_message.body);
    get_mode_header->ApiNumber = ConsolepGetMode;
    get_mode_message.descriptor.Object = 1;
    if (!api.handle(get_mode_message) ||
        reinterpret_cast<const CONSOLE_MODE_MSG *>(get_mode_message.body + sizeof(CONSOLE_MSG_HEADER))->Mode != 0x7)
    {
        std::fprintf(stderr, "get console mode failed\n");
        return EXIT_FAILURE;
    }

    corehost::coreio::io_msg set_display_message{};
    set_display_message.descriptor.InputSize = sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_SETDISPLAYMODE_MSG);
    set_display_message.descriptor.OutputSize = sizeof(set_display_message.body);
    auto *set_display_header = reinterpret_cast<CONSOLE_MSG_HEADER *>(set_display_message.body);
    set_display_header->ApiNumber = ConsolepSetDisplayMode;
    auto *set_display_request =
        reinterpret_cast<CONSOLE_SETDISPLAYMODE_MSG *>(set_display_message.body + sizeof(CONSOLE_MSG_HEADER));
    set_display_request->dwFlags = 1;
    if (!api.handle(set_display_message) || state.display_mode != 1 ||
        set_display_request->ScreenBufferDimensions.X != 6)
    {
        std::fprintf(stderr, "set display mode failed\n");
        return EXIT_FAILURE;
    }

    corehost::coreio::io_msg get_display_message{};
    get_display_message.descriptor.InputSize = sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_GETDISPLAYMODE_MSG);
    get_display_message.descriptor.OutputSize = sizeof(get_display_message.body);
    auto *get_display_header = reinterpret_cast<CONSOLE_MSG_HEADER *>(get_display_message.body);
    get_display_header->ApiNumber = ConsolepGetDisplayMode;
    if (!api.handle(get_display_message) ||
        reinterpret_cast<const CONSOLE_GETDISPLAYMODE_MSG *>(get_display_message.body + sizeof(CONSOLE_MSG_HEADER))
                ->ModeFlags != 1)
    {
        std::fprintf(stderr, "get display mode failed\n");
        return EXIT_FAILURE;
    }

    corehost::coreio::process_registry processes;
    processes.add(42);
    processes.add(43);
    corehost::consoleapi::console_api process_api{buffer, state, nullptr, 0, &processes};
    corehost::coreio::io_msg process_message{};
    process_message.descriptor.InputSize = sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_GETCONSOLEPROCESSLIST_MSG);
    process_message.descriptor.OutputSize =
        sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_GETCONSOLEPROCESSLIST_MSG) + 2 * sizeof(std::uint32_t);
    auto *process_header = reinterpret_cast<CONSOLE_MSG_HEADER *>(process_message.body);
    process_header->ApiNumber = ConsolepGetConsoleProcessList;
    if (!process_api.handle(process_message))
    {
        std::fprintf(stderr, "get process list failed\n");
        return EXIT_FAILURE;
    }
    const auto *process_info =
        reinterpret_cast<const CONSOLE_GETCONSOLEPROCESSLIST_MSG *>(process_message.body + sizeof(CONSOLE_MSG_HEADER));
    const auto *process_data = reinterpret_cast<const std::uint32_t *>(
        process_message.body + sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_GETCONSOLEPROCESSLIST_MSG));
    if (process_info->dwProcessCount != 2 || process_data[0] != 43 || process_data[1] != 42)
    {
        std::fprintf(stderr, "get process list data failed\n");
        return EXIT_FAILURE;
    }

    input_queue.push(corehost::consoleinput::input_event{});
    corehost::coreio::io_msg flush_message{};
    flush_message.descriptor.InputSize = sizeof(CONSOLE_MSG_HEADER);
    flush_message.descriptor.OutputSize = sizeof(flush_message.body);
    auto *flush_header = reinterpret_cast<CONSOLE_MSG_HEADER *>(flush_message.body);
    flush_header->ApiNumber = ConsolepFlushInputBuffer;
    if (!input_api.handle(flush_message) || !input_queue.empty())
    {
        std::fprintf(stderr, "flush input buffer failed\n");
        return EXIT_FAILURE;
    }

    corehost::coreio::io_msg set_history_message{};
    set_history_message.descriptor.InputSize = sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_HISTORY_MSG);
    set_history_message.descriptor.OutputSize = sizeof(set_history_message.body);
    auto *set_history_header = reinterpret_cast<CONSOLE_MSG_HEADER *>(set_history_message.body);
    set_history_header->ApiNumber = ConsolepSetHistory;
    auto *set_history_request =
        reinterpret_cast<CONSOLE_HISTORY_MSG *>(set_history_message.body + sizeof(CONSOLE_MSG_HEADER));
    set_history_request->HistoryBufferSize = 100;
    set_history_request->NumberOfHistoryBuffers = 8;
    set_history_request->dwFlags = 1;
    if (!api.handle(set_history_message) || state.history_buffer_size != 100)
    {
        std::fprintf(stderr, "set history failed\n");
        return EXIT_FAILURE;
    }

    corehost::coreio::io_msg get_history_message{};
    get_history_message.descriptor.InputSize = sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_HISTORY_MSG);
    get_history_message.descriptor.OutputSize = sizeof(get_history_message.body);
    auto *get_history_header = reinterpret_cast<CONSOLE_MSG_HEADER *>(get_history_message.body);
    get_history_header->ApiNumber = ConsolepGetHistory;
    if (!api.handle(get_history_message))
    {
        std::fprintf(stderr, "get history failed\n");
        return EXIT_FAILURE;
    }
    const auto *history_info =
        reinterpret_cast<const CONSOLE_HISTORY_MSG *>(get_history_message.body + sizeof(CONSOLE_MSG_HEADER));
    if (history_info->HistoryBufferSize != 100 || history_info->NumberOfHistoryBuffers != 8)
    {
        std::fprintf(stderr, "get history data failed\n");
        return EXIT_FAILURE;
    }

    constexpr char16_t newline_text[]{u'A', u'\r', u'\n', u'B'};
    corehost::coreio::io_msg newline_message{};
    newline_message.descriptor.InputSize =
        sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_WRITECONSOLE_MSG) + sizeof(newline_text);
    newline_message.descriptor.OutputSize = sizeof(newline_message.body);
    auto *newline_header = reinterpret_cast<CONSOLE_MSG_HEADER *>(newline_message.body);
    newline_header->ApiNumber = ConsolepWriteConsole;
    auto *newline_request =
        reinterpret_cast<CONSOLE_WRITECONSOLE_MSG *>(newline_message.body + sizeof(CONSOLE_MSG_HEADER));
    newline_request->Unicode = TRUE;
    std::memcpy(newline_message.body + sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_WRITECONSOLE_MSG), newline_text,
                sizeof(newline_text));
    if (!write_api.handle(newline_message) || write_buffer.at(0, 1).primary() != u'B')
    {
        std::fprintf(stderr, "write console newline failed\n");
        return EXIT_FAILURE;
    }

    const char ascii_data[]{'P', 'Q'};
    corehost::coreio::io_msg ascii_message{};
    ascii_message.descriptor.InputSize =
        sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_WRITECONSOLEOUTPUTSTRING_MSG) + sizeof(ascii_data);
    ascii_message.descriptor.OutputSize = sizeof(ascii_message.body);
    auto *ascii_header = reinterpret_cast<CONSOLE_MSG_HEADER *>(ascii_message.body);
    ascii_header->ApiNumber = ConsolepWriteConsoleOutputString;
    auto *ascii_request =
        reinterpret_cast<CONSOLE_WRITECONSOLEOUTPUTSTRING_MSG *>(ascii_message.body + sizeof(CONSOLE_MSG_HEADER));
    ascii_request->WriteCoord = COORD{0, 1};
    ascii_request->StringType = CONSOLE_ASCII;
    ascii_request->NumRecords = 2;
    std::memcpy(ascii_message.body + sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_WRITECONSOLEOUTPUTSTRING_MSG),
                ascii_data, sizeof(ascii_data));
    if (!api.handle(ascii_message) || buffer.at(0, 1).primary() != u'P' || buffer.at(1, 1).primary() != u'Q')
    {
        std::fprintf(stderr, "ascii output string failed\n");
        return EXIT_FAILURE;
    }

    corehost::consoleapi::console_api active_api{buffer, state};
    active_api.set_alternate_object(0x77);
    corehost::coreio::io_msg active_message{};
    active_message.descriptor.InputSize = sizeof(CONSOLE_MSG_HEADER);
    active_message.descriptor.OutputSize = sizeof(active_message.body);
    active_message.descriptor.Object = 0x77;
    auto *active_header = reinterpret_cast<CONSOLE_MSG_HEADER *>(active_message.body);
    active_header->ApiNumber = ConsolepSetActiveScreenBuffer;
    if (!active_api.handle(active_message) || !buffer.alternate_active())
    {
        std::fprintf(stderr, "set active screen buffer failed\n");
        return EXIT_FAILURE;
    }
    active_message.descriptor.Object = 0;
    if (!active_api.handle(active_message) || buffer.alternate_active())
    {
        std::fprintf(stderr, "reset active screen buffer failed\n");
        return EXIT_FAILURE;
    }

    corehost::consoleinput::input_queue line_queue;
    corehost::consoleinput::key_event line_key;
    line_key.character = u'A';
    line_queue.push(corehost::consoleinput::input_event{corehost::consoleinput::input_event_type::key, line_key});
    line_key.character = u'\r';
    line_queue.push(corehost::consoleinput::input_event{corehost::consoleinput::input_event_type::key, line_key});
    line_key.character = u'B';
    line_queue.push(corehost::consoleinput::input_event{corehost::consoleinput::input_event_type::key, line_key});
    corehost::consoleapi::console_api line_api{buffer, state, &line_queue};
    corehost::coreio::io_msg line_message{};
    line_message.descriptor.InputSize = sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_READCONSOLE_MSG);
    line_message.descriptor.OutputSize =
        sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_READCONSOLE_MSG) + sizeof(char16_t);
    auto *line_header = reinterpret_cast<CONSOLE_MSG_HEADER *>(line_message.body);
    line_header->ApiNumber = ConsolepReadConsole;
    auto *line_request = reinterpret_cast<CONSOLE_READCONSOLE_MSG *>(line_message.body + sizeof(CONSOLE_MSG_HEADER));
    line_request->Unicode = TRUE;
    if (!line_api.handle(line_message) || line_queue.record_count() != 1)
    {
        std::fprintf(stderr, "read console unread restore failed\n");
        return EXIT_FAILURE;
    }
    corehost::consoleinput::input_event remaining{};
    if (line_queue.read(std::span<corehost::consoleinput::input_event>{&remaining, 1}) != 1 ||
        remaining.key.character != u'B')
    {
        std::fprintf(stderr, "read console remaining order failed\n");
        return EXIT_FAILURE;
    }

    corehost::consoleinput::input_queue line_edit_queue;
    auto push_edit = [&](char16_t character) {
        corehost::consoleinput::key_event key;
        key.character = character;
        line_edit_queue.push(corehost::consoleinput::input_event{corehost::consoleinput::input_event_type::key, key});
    };
    push_edit(u'A');
    push_edit(u'B');
    {
        corehost::consoleinput::key_event key;
        key.key = corehost::consoleinput::virtual_key::left;
        line_edit_queue.push(corehost::consoleinput::input_event{corehost::consoleinput::input_event_type::key, key});
        key = {};
        key.key = corehost::consoleinput::virtual_key::insert;
        line_edit_queue.push(corehost::consoleinput::input_event{corehost::consoleinput::input_event_type::key, key});
    }
    push_edit(u'C');
    push_edit(u'\r');
    corehost::consoleapi::console_api line_edit_api{buffer, state, &line_edit_queue};
    corehost::coreio::io_msg line_edit_msg{};
    line_edit_msg.descriptor.InputSize = sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_READCONSOLE_MSG);
    line_edit_msg.descriptor.OutputSize =
        sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_READCONSOLE_MSG) + 8 * sizeof(char16_t);
    auto *line_edit_header = reinterpret_cast<CONSOLE_MSG_HEADER *>(line_edit_msg.body);
    line_edit_header->ApiNumber = ConsolepReadConsole;
    auto *line_edit_request = reinterpret_cast<CONSOLE_READCONSOLE_MSG *>(line_edit_msg.body + sizeof(CONSOLE_MSG_HEADER));
    line_edit_request->Unicode = TRUE;
    const auto *line_edit_output = reinterpret_cast<const char16_t *>(line_edit_msg.body + sizeof(CONSOLE_MSG_HEADER) +
                                                                  sizeof(CONSOLE_READCONSOLE_MSG));
    if (!line_edit_api.handle(line_edit_msg) || line_edit_request->NumBytes != 4 * sizeof(char16_t) ||
        line_edit_output[0] != u'A' || line_edit_output[1] != u'C' || line_edit_output[2] != u'\r' || line_edit_output[3] != u'\n')
    {
        std::fprintf(stderr, "read console insert mode failed\n");
        return EXIT_FAILURE;
    }

    corehost::screen::screen_buffer echo_buffer{4, 2};
    corehost::screen::console_state echo_state{4, 2};
    corehost::consoleinput::input_queue echo_queue;
    corehost::consoleinput::key_event echo_key;
    echo_key.character = u'E';
    echo_queue.push(corehost::consoleinput::input_event{corehost::consoleinput::input_event_type::key, echo_key});
    corehost::consoleapi::console_api echo_api{echo_buffer, echo_state, &echo_queue};
    corehost::coreio::io_msg echo_message{};
    echo_message.descriptor.InputSize = sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_READCONSOLE_MSG);
    echo_message.descriptor.OutputSize =
        sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_READCONSOLE_MSG) + sizeof(char16_t);
    auto *echo_header = reinterpret_cast<CONSOLE_MSG_HEADER *>(echo_message.body);
    echo_header->ApiNumber = ConsolepReadConsole;
    auto *echo_request = reinterpret_cast<CONSOLE_READCONSOLE_MSG *>(echo_message.body + sizeof(CONSOLE_MSG_HEADER));
    echo_request->Unicode = TRUE;
    if (echo_api.handle(echo_message) || echo_buffer.at(0, 0).primary() != u'E' || echo_state.cursor().x != 1)
    {
        std::fprintf(stderr, "read console echo failed\n");
        return EXIT_FAILURE;
    }

    corehost::screen::screen_buffer cluster_buffer{8, 2};
    corehost::screen::console_state cluster_state{8, 2};
    cluster_state.measurement = corehost::screen::text_measurement::graphemes;
    corehost::consoleinput::input_queue cluster_queue;
    cluster_queue.push(corehost::consoleinput::input_event{
        corehost::consoleinput::input_event_type::key, corehost::consoleinput::key_event{u'e'}});
    cluster_queue.push(corehost::consoleinput::input_event{
        corehost::consoleinput::input_event_type::key, corehost::consoleinput::key_event{u'\u0301'}});
    corehost::consoleapi::console_api cluster_api{cluster_buffer, cluster_state, &cluster_queue};
    corehost::coreio::io_msg cluster_message{};
    cluster_message.descriptor.InputSize = sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_READCONSOLE_MSG);
    cluster_message.descriptor.OutputSize =
        sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_READCONSOLE_MSG) + 8 * sizeof(char16_t);
    auto *cluster_header = reinterpret_cast<CONSOLE_MSG_HEADER *>(cluster_message.body);
    cluster_header->ApiNumber = ConsolepReadConsole;
    auto *cluster_request =
        reinterpret_cast<CONSOLE_READCONSOLE_MSG *>(cluster_message.body + sizeof(CONSOLE_MSG_HEADER));
    cluster_request->Unicode = TRUE;
    if (cluster_api.handle(cluster_message) || cluster_buffer.glyph_at(0, 0) != u"e\u0301" ||
        cluster_state.cursor().x != 1)
    {
        std::fprintf(stderr, "combining echo failed\n");
        return EXIT_FAILURE;
    }

    corehost::screen::screen_buffer wide_buffer{8, 2};
    corehost::screen::console_state wide_state{8, 2};
    wide_state.measurement = corehost::screen::text_measurement::wcswidth;
    corehost::consoleinput::input_queue wide_queue;
    wide_queue.push(corehost::consoleinput::input_event{
        corehost::consoleinput::input_event_type::key, corehost::consoleinput::key_event{u'\u4E2D'}});
    corehost::consoleapi::console_api wide_api{wide_buffer, wide_state, &wide_queue};
    corehost::coreio::io_msg wide_message{};
    wide_message.descriptor.InputSize = sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_READCONSOLE_MSG);
    wide_message.descriptor.OutputSize =
        sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_READCONSOLE_MSG) + 8 * sizeof(char16_t);
    auto *wide_header = reinterpret_cast<CONSOLE_MSG_HEADER *>(wide_message.body);
    wide_header->ApiNumber = ConsolepReadConsole;
    auto *wide_request = reinterpret_cast<CONSOLE_READCONSOLE_MSG *>(wide_message.body + sizeof(CONSOLE_MSG_HEADER));
    wide_request->Unicode = TRUE;
    if (wide_api.handle(wide_message) || wide_buffer.glyph_at(0, 0) != u"\u4E2D" || wide_state.cursor().x != 2)
    {
        std::fprintf(stderr, "wide echo failed\n");
        return EXIT_FAILURE;
    }

    corehost::screen::screen_buffer wrap_off_buffer{4, 2};
    corehost::screen::console_state wrap_off_state{4, 2};
    wrap_off_state.output_mode = 0;
    corehost::consoleapi::console_api wrap_off_api{wrap_off_buffer, wrap_off_state};
    if (wrap_off_api.write_text(u"ABCDE") || wrap_off_buffer.at(0, 0).primary() != u'A' ||
        wrap_off_buffer.at(3, 0).primary() != u'D' || wrap_off_buffer.at(0, 1).primary() != u' ')
    {
        std::fprintf(stderr, "wrap disabled failed\n");
        return EXIT_FAILURE;
    }

    corehost::screen::screen_buffer wrap_on_buffer{4, 2};
    corehost::screen::console_state wrap_on_state{4, 2};
    wrap_on_state.output_mode = 0x0001;
    corehost::consoleapi::console_api wrap_on_api{wrap_on_buffer, wrap_on_state};
    const auto wrap_on_result = wrap_on_api.write_text(u"ABCDE");
    if (!wrap_on_result || wrap_on_buffer.at(0, 1).primary() != u'E')
    {
        std::fprintf(stderr, "wrap enabled failed: result=%d cell=%04x cursor=%zu,%zu\n",
                     static_cast<int>(wrap_on_result), static_cast<unsigned>(wrap_on_buffer.at(0, 1).primary()),
                     wrap_on_state.cursor().x, wrap_on_state.cursor().y);
        return EXIT_FAILURE;
    }

    corehost::screen::screen_buffer zero_buffer{8, 2};
    corehost::screen::console_state zero_state{8, 2};
    zero_state.measurement = corehost::screen::text_measurement::wcswidth;
    corehost::consoleinput::input_queue zero_queue;
    zero_queue.push(corehost::consoleinput::input_event{
        corehost::consoleinput::input_event_type::key, corehost::consoleinput::key_event{u'\u0301'}});
    corehost::consoleapi::console_api zero_api{zero_buffer, zero_state, &zero_queue};
    corehost::coreio::io_msg zero_message{};
    zero_message.descriptor.InputSize = sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_READCONSOLE_MSG);
    zero_message.descriptor.OutputSize =
        sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_READCONSOLE_MSG) + 8 * sizeof(char16_t);
    auto *zero_header = reinterpret_cast<CONSOLE_MSG_HEADER *>(zero_message.body);
    zero_header->ApiNumber = ConsolepReadConsole;
    auto *zero_request = reinterpret_cast<CONSOLE_READCONSOLE_MSG *>(zero_message.body + sizeof(CONSOLE_MSG_HEADER));
    zero_request->Unicode = TRUE;
    if (zero_api.handle(zero_message) || zero_buffer.glyph_at(0, 0) != u" " || zero_state.cursor().x != 0)
    {
        std::fprintf(stderr, "zero width echo failed\n");
        return EXIT_FAILURE;
    }

    corehost::coreio::io_msg font_size_message{};
    font_size_message.descriptor.InputSize = sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_GETFONTSIZE_MSG);
    font_size_message.descriptor.OutputSize = sizeof(font_size_message.body);
    auto *font_size_header = reinterpret_cast<CONSOLE_MSG_HEADER *>(font_size_message.body);
    font_size_header->ApiNumber = ConsolepGetFontSize;
    auto *font_size_request =
        reinterpret_cast<CONSOLE_GETFONTSIZE_MSG *>(font_size_message.body + sizeof(CONSOLE_MSG_HEADER));
    font_size_request->FontIndex = 0;
    if (!api.handle(font_size_message) || font_size_request->FontSize.X != 10 || font_size_request->FontSize.Y != 20)
    {
        std::fprintf(stderr, "get font size failed\n");
        return EXIT_FAILURE;
    }

    corehost::screen::screen_buffer history_buffer{4, 2};
    corehost::screen::console_state history_state{4, 2};
    corehost::consoleapi::console_api history_api{history_buffer, history_state};
    history_state.add_command(u"cmd");
    corehost::coreio::io_msg history_length_message{};
    history_length_message.descriptor.InputSize =
        sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_GETCOMMANDHISTORYLENGTH_MSG);
    history_length_message.descriptor.OutputSize = sizeof(history_length_message.body);
    auto *history_length_header = reinterpret_cast<CONSOLE_MSG_HEADER *>(history_length_message.body);
    history_length_header->ApiNumber = ConsolepGetCommandHistoryLength;
    auto *history_length_request = reinterpret_cast<CONSOLE_GETCOMMANDHISTORYLENGTH_MSG *>(history_length_message.body +
                                                                                           sizeof(CONSOLE_MSG_HEADER));
    history_length_request->Unicode = TRUE;
    if (!history_api.handle(history_length_message) || history_length_request->CommandHistoryLength != 8)
    {
        std::fprintf(stderr, "command history length failed\n");
        return EXIT_FAILURE;
    }

    corehost::coreio::io_msg history_message{};
    history_message.descriptor.InputSize = sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_GETCOMMANDHISTORY_MSG);
    history_message.descriptor.OutputSize =
        sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_GETCOMMANDHISTORY_MSG) + 4 * sizeof(char16_t);
    auto *history_header = reinterpret_cast<CONSOLE_MSG_HEADER *>(history_message.body);
    history_header->ApiNumber = ConsolepGetCommandHistory;
    auto *history_request =
        reinterpret_cast<CONSOLE_GETCOMMANDHISTORY_MSG *>(history_message.body + sizeof(CONSOLE_MSG_HEADER));
    history_request->Unicode = TRUE;
    if (!history_api.handle(history_message))
    {
        std::fprintf(stderr, "get command history failed\n");
        return EXIT_FAILURE;
    }
    const auto *history_text = reinterpret_cast<const char16_t *>(history_message.body + sizeof(CONSOLE_MSG_HEADER) +
                                                                  sizeof(CONSOLE_GETCOMMANDHISTORY_MSG));
    if (history_text[0] != u'c' || history_text[2] != u'd' || history_text[3] != 0)
    {
        std::fprintf(stderr, "get command history data failed\n");
        return EXIT_FAILURE;
    }

    constexpr char16_t alias_source[]{u'x'};
    constexpr char16_t alias_target[]{u'y'};
    constexpr char16_t alias_exe[]{u'c', u'm', u'd'};
    corehost::coreio::io_msg add_alias_message{};
    add_alias_message.descriptor.InputSize = sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_ADDALIAS_MSG) +
                                             (sizeof(alias_source) + sizeof(alias_target) + sizeof(alias_exe));
    add_alias_message.descriptor.OutputSize = sizeof(add_alias_message.body);
    auto *add_alias_header = reinterpret_cast<CONSOLE_MSG_HEADER *>(add_alias_message.body);
    add_alias_header->ApiNumber = ConsolepAddAlias;
    auto *add_alias_request =
        reinterpret_cast<CONSOLE_ADDALIAS_MSG *>(add_alias_message.body + sizeof(CONSOLE_MSG_HEADER));
    add_alias_request->SourceLength = 1;
    add_alias_request->TargetLength = 1;
    add_alias_request->ExeLength = 3;
    add_alias_request->Unicode = TRUE;
    auto *alias_data = reinterpret_cast<char16_t *>(add_alias_request + 1);
    std::memcpy(alias_data, alias_exe, sizeof(alias_exe));
    std::memcpy(alias_data + 3, alias_source, sizeof(alias_source));
    std::memcpy(alias_data + 4, alias_target, sizeof(alias_target));
    if (!api.handle(add_alias_message) || state.aliases.size() != 1)
    {
        std::fprintf(stderr, "add alias failed\n");
        return EXIT_FAILURE;
    }

    corehost::coreio::io_msg get_alias_message{};
    get_alias_message.descriptor.InputSize =
        sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_GETALIAS_MSG) + sizeof(alias_source) + sizeof(alias_exe);
    get_alias_message.descriptor.OutputSize = sizeof(get_alias_message.body);
    auto *get_alias_header = reinterpret_cast<CONSOLE_MSG_HEADER *>(get_alias_message.body);
    get_alias_header->ApiNumber = ConsolepGetAlias;
    auto *get_alias_request =
        reinterpret_cast<CONSOLE_GETALIAS_MSG *>(get_alias_message.body + sizeof(CONSOLE_MSG_HEADER));
    get_alias_request->SourceLength = 1;
    get_alias_request->ExeLength = 3;
    get_alias_request->Unicode = TRUE;
    auto *get_alias_data = reinterpret_cast<char16_t *>(get_alias_request + 1);
    std::memcpy(get_alias_data, alias_exe, sizeof(alias_exe));
    std::memcpy(get_alias_data + 3, alias_source, sizeof(alias_source));
    if (!api.handle(get_alias_message) || get_alias_request->TargetLength != sizeof(char16_t) * 2 ||
        get_alias_data[0] != u'y')
    {
        std::fprintf(stderr, "get alias failed\n");
        return EXIT_FAILURE;
    }

    corehost::coreio::io_msg aliases_length_message{};
    aliases_length_message.descriptor.InputSize = sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_GETALIASESLENGTH_MSG);
    aliases_length_message.descriptor.OutputSize = sizeof(aliases_length_message.body);
    auto *aliases_length_header = reinterpret_cast<CONSOLE_MSG_HEADER *>(aliases_length_message.body);
    aliases_length_header->ApiNumber = ConsolepGetAliasesLength;
    auto *aliases_length_request =
        reinterpret_cast<CONSOLE_GETALIASESLENGTH_MSG *>(aliases_length_message.body + sizeof(CONSOLE_MSG_HEADER));
    aliases_length_request->Unicode = TRUE;
    if (!api.handle(aliases_length_message) || aliases_length_request->AliasesLength != 8)
    {
        std::fprintf(stderr, "aliases length failed\n");
        return EXIT_FAILURE;
    }

    corehost::coreio::io_msg aliases_message{};
    aliases_message.descriptor.InputSize = sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_GETALIASES_MSG);
    aliases_message.descriptor.OutputSize = sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_GETALIASES_MSG) + 8;
    auto *aliases_header = reinterpret_cast<CONSOLE_MSG_HEADER *>(aliases_message.body);
    aliases_header->ApiNumber = ConsolepGetAliases;
    auto *aliases_request =
        reinterpret_cast<CONSOLE_GETALIASES_MSG *>(aliases_message.body + sizeof(CONSOLE_MSG_HEADER));
    aliases_request->Unicode = TRUE;
    if (!api.handle(aliases_message) || aliases_request->AliasesBufferLength != 8)
    {
        std::fprintf(stderr, "get aliases failed\n");
        return EXIT_FAILURE;
    }
    const auto *aliases_text = reinterpret_cast<const char16_t *>(aliases_message.body + sizeof(CONSOLE_MSG_HEADER) +
                                                                  sizeof(CONSOLE_GETALIASES_MSG));
    if (aliases_text[0] != u'x' || aliases_text[1] != u'=' || aliases_text[2] != u'y' || aliases_text[3] != 0)
    {
        std::fprintf(stderr, "get aliases data failed\n");
        return EXIT_FAILURE;
    }

    add_alias_request->SourceLength = 1;
    add_alias_request->TargetLength = 1;
    add_alias_request->ExeLength = 3;
    add_alias_message.descriptor.InputSize = sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_ADDALIAS_MSG) +
                                             sizeof(alias_exe) + sizeof(alias_source) + sizeof(alias_target);
    std::memcpy(alias_data, alias_exe, sizeof(alias_exe));
    std::memcpy(alias_data + 3, u"z", sizeof(char16_t));
    std::memcpy(alias_data + 4, u"w", sizeof(char16_t));
    if (!api.handle(add_alias_message) || state.aliases.size() != 2)
    {
        std::fprintf(stderr, "add second alias failed\n");
        return EXIT_FAILURE;
    }

    corehost::coreio::io_msg exes_message{};
    exes_message.descriptor.InputSize = sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_GETALIASEXES_MSG);
    exes_message.descriptor.OutputSize = sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_GETALIASEXES_MSG) + 8;
    auto *exes_header = reinterpret_cast<CONSOLE_MSG_HEADER *>(exes_message.body);
    exes_header->ApiNumber = ConsolepGetAliasExes;
    auto *exes_request = reinterpret_cast<CONSOLE_GETALIASEXES_MSG *>(exes_message.body + sizeof(CONSOLE_MSG_HEADER));
    exes_request->Unicode = TRUE;
    if (!api.handle(exes_message) || exes_request->AliasExesBufferLength != 8)
    {
        std::fprintf(stderr, "get alias exes failed\n");
        return EXIT_FAILURE;
    }
    const auto *exes_text = reinterpret_cast<const char16_t *>(exes_message.body + sizeof(CONSOLE_MSG_HEADER) +
                                                               sizeof(CONSOLE_GETALIASEXES_MSG));
    if (exes_text[0] != u'c' || exes_text[1] != u'm' || exes_text[2] != u'd' || exes_text[3] != 0)
    {
        std::fprintf(stderr, "get alias exes data failed\n");
        return EXIT_FAILURE;
    }

    corehost::coreio::io_msg console_window_message{};
    console_window_message.descriptor.InputSize = sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_GETCONSOLEWINDOW_MSG64);
    console_window_message.descriptor.OutputSize = sizeof(console_window_message.body);
    auto *console_window_header = reinterpret_cast<CONSOLE_MSG_HEADER *>(console_window_message.body);
    console_window_header->ApiNumber = ConsolepGetConsoleWindow;
    if (!api.handle(console_window_message) || reinterpret_cast<const CONSOLE_GETCONSOLEWINDOW_MSG64 *>(
                                                   console_window_message.body + sizeof(CONSOLE_MSG_HEADER))
                                                       ->hwnd != 0)
    {
        std::fprintf(stderr, "get console window failed\n");
        return EXIT_FAILURE;
    }

    corehost::consoleapi::console_api window_handle_api{buffer, state};
    window_handle_api.set_window_handle(0x55);
    if (!window_handle_api.handle(console_window_message) ||
        reinterpret_cast<const CONSOLE_GETCONSOLEWINDOW_MSG64 *>(console_window_message.body +
                                                                 sizeof(CONSOLE_MSG_HEADER))
                ->hwnd != reinterpret_cast<HWND>(0x55))
    {
        std::fprintf(stderr, "get console window handle failed\n");
        return EXIT_FAILURE;
    }

    corehost::consoleinput::input_queue mouse_queue;
    corehost::consoleinput::input_event mouse_event{corehost::consoleinput::input_event_type::mouse};
    mouse_event.mouse = corehost::consoleinput::mouse_event{4, 2, 1, 0, 0};
    mouse_queue.push(mouse_event);
    corehost::consoleapi::console_api mouse_api{buffer, state, &mouse_queue};
    corehost::coreio::io_msg mouse_record_message{};
    mouse_record_message.descriptor.InputSize = sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_GETCONSOLEINPUT_MSG);
    mouse_record_message.descriptor.OutputSize =
        sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_GETCONSOLEINPUT_MSG) + sizeof(INPUT_RECORD);
    auto *mouse_record_header = reinterpret_cast<CONSOLE_MSG_HEADER *>(mouse_record_message.body);
    mouse_record_header->ApiNumber = ConsolepGetConsoleInput;
    if (!mouse_api.handle(mouse_record_message))
    {
        std::fprintf(stderr, "read mouse input failed\n");
        return EXIT_FAILURE;
    }
    const auto *mouse_record = reinterpret_cast<const INPUT_RECORD *>(
        mouse_record_message.body + sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_GETCONSOLEINPUT_MSG));
    if (mouse_record->EventType != MOUSE_EVENT || mouse_record->Event.MouseEvent.dwMousePosition.X != 4 ||
        mouse_record->Event.MouseEvent.dwButtonState != 1)
    {
        std::fprintf(stderr, "read mouse input data failed\n");
        return EXIT_FAILURE;
    }

    corehost::consoleapi::console_api mode_api{buffer, state, nullptr, 0x55};
    set_mode_message.descriptor.Object = 0x55;
    set_mode_request->Mode = 0x0200 | 0x0080 | 0x0020;
    if (!mode_api.handle(set_mode_message) || (state.input_mode & 0x0200) == 0 ||
        (state.input_private_mode & 0x0020) == 0)
    {
        std::fprintf(stderr, "set input mode private failed\n");
        return EXIT_FAILURE;
    }
    get_mode_message.descriptor.Object = 0x55;
    if (!mode_api.handle(get_mode_message) ||
        (reinterpret_cast<const CONSOLE_MODE_MSG *>(get_mode_message.body + sizeof(CONSOLE_MSG_HEADER))->Mode &
         0x0080) == 0)
    {
        std::fprintf(stderr, "get input mode extended failed\n");
        return EXIT_FAILURE;
    }
    set_mode_message.descriptor.Object = 1;
    set_mode_request->Mode = 0x100;
    if (!api.handle(set_mode_message) || set_mode_message.complete.IoStatus.Status == 0)
    {
        std::fprintf(stderr, "invalid output mode accepted\n");
        return EXIT_FAILURE;
    }

    corehost::consoleinput::input_queue initial_queue;
    initial_queue.push(corehost::consoleinput::input_event{corehost::consoleinput::input_event_type::key,
                                                           corehost::consoleinput::key_event{u'A'}});
    corehost::screen::screen_buffer initial_buffer{4, 2};
    corehost::screen::console_state initial_state{4, 2};
    corehost::consoleapi::console_api initial_api{initial_buffer, initial_state, &initial_queue};
    corehost::coreio::io_msg initial_message{};
    initial_message.descriptor.InputSize =
        sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_READCONSOLE_MSG) + sizeof(char16_t);
    initial_message.descriptor.OutputSize =
        sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_READCONSOLE_MSG) + 2 * sizeof(char16_t);
    auto *initial_header = reinterpret_cast<CONSOLE_MSG_HEADER *>(initial_message.body);
    initial_header->ApiNumber = ConsolepReadConsole;
    auto *initial_request =
        reinterpret_cast<CONSOLE_READCONSOLE_MSG *>(initial_message.body + sizeof(CONSOLE_MSG_HEADER));
    initial_request->Unicode = TRUE;
    initial_request->InitialNumBytes = sizeof(char16_t);
    *reinterpret_cast<char16_t *>(initial_request + 1) = u'Z';
    initial_queue.push(corehost::consoleinput::input_event{corehost::consoleinput::input_event_type::key,
                                                           corehost::consoleinput::key_event{u'\r'}});
    if (!initial_api.handle(initial_message) || initial_request->NumBytes != 4 * sizeof(char16_t))
    {
        std::fprintf(stderr, "read console initial data failed\n");
        return EXIT_FAILURE;
    }
    const auto *initial_output = reinterpret_cast<const char16_t *>(initial_request + 1);
    if (initial_output[0] != u'Z' || initial_output[1] != u'A')
    {
        std::fprintf(stderr, "read console initial data content failed\n");
        return EXIT_FAILURE;
    }

    corehost::consoleinput::input_queue wakeup_queue;
    wakeup_queue.push(corehost::consoleinput::input_event{corehost::consoleinput::input_event_type::key,
                                                          corehost::consoleinput::key_event{u'\x01'}});
    corehost::screen::screen_buffer wakeup_buffer{4, 2};
    corehost::screen::console_state wakeup_state{4, 2};
    corehost::consoleapi::console_api wakeup_api{wakeup_buffer, wakeup_state, &wakeup_queue};
    corehost::coreio::io_msg wakeup_message{};
    wakeup_message.descriptor.InputSize = sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_READCONSOLE_MSG);
    wakeup_message.descriptor.OutputSize =
        sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_READCONSOLE_MSG) + sizeof(char16_t);
    auto *wakeup_header = reinterpret_cast<CONSOLE_MSG_HEADER *>(wakeup_message.body);
    wakeup_header->ApiNumber = ConsolepReadConsole;
    auto *wakeup_request =
        reinterpret_cast<CONSOLE_READCONSOLE_MSG *>(wakeup_message.body + sizeof(CONSOLE_MSG_HEADER));
    wakeup_request->Unicode = TRUE;
    wakeup_request->CtrlWakeupMask = 1;
    if (!wakeup_api.handle(wakeup_message) || wakeup_request->NumBytes != 3 * sizeof(char16_t))
    {
        std::fprintf(stderr, "read console ctrl wakeup failed\n");
        return EXIT_FAILURE;
    }
    const auto *wakeup_output = reinterpret_cast<const char16_t *>(wakeup_request + 1);
    if (wakeup_output[0] != u'\x01')
    {
        std::fprintf(stderr, "read console ctrl wakeup data failed\n");
        return EXIT_FAILURE;
    }

    const char title_ascii[]{'H', 'i'};
    corehost::coreio::io_msg set_title_message{};
    set_title_message.descriptor.InputSize =
        sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_SETTITLE_MSG) + sizeof(title_ascii);
    set_title_message.descriptor.OutputSize = sizeof(set_title_message.body);
    auto *set_title_header = reinterpret_cast<CONSOLE_MSG_HEADER *>(set_title_message.body);
    set_title_header->ApiNumber = ConsolepSetTitle;
    auto *set_title_request =
        reinterpret_cast<CONSOLE_SETTITLE_MSG *>(set_title_message.body + sizeof(CONSOLE_MSG_HEADER));
    set_title_request->Unicode = FALSE;
    std::memcpy(set_title_request + 1, title_ascii, sizeof(title_ascii));
    if (!api.handle(set_title_message) || state.title != u"Hi")
    {
        std::fprintf(stderr, "set ascii title failed\n");
        return EXIT_FAILURE;
    }

    corehost::coreio::io_msg ascii_title_message{};
    ascii_title_message.descriptor.InputSize = sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_GETTITLE_MSG);
    ascii_title_message.descriptor.OutputSize = sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_GETTITLE_MSG) + 2;
    auto *ascii_title_header = reinterpret_cast<CONSOLE_MSG_HEADER *>(ascii_title_message.body);
    ascii_title_header->ApiNumber = ConsolepGetTitle;
    auto *ascii_title_request =
        reinterpret_cast<CONSOLE_GETTITLE_MSG *>(ascii_title_message.body + sizeof(CONSOLE_MSG_HEADER));
    ascii_title_request->Unicode = FALSE;
    if (!api.handle(ascii_title_message) || ascii_title_request->TitleLength != 2)
    {
        std::fprintf(stderr, "get ascii title failed\n");
        return EXIT_FAILURE;
    }
    const auto *title_output = reinterpret_cast<const char *>(ascii_title_request + 1);
    if (title_output[0] != 'H' || title_output[1] != 'i')
    {
        std::fprintf(stderr, "get ascii title data failed\n");
        return EXIT_FAILURE;
    }

    add_alias_request->TargetLength = 0;
    add_alias_message.descriptor.InputSize =
        sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_ADDALIAS_MSG) + sizeof(alias_exe) + sizeof(alias_source);
    std::memcpy(alias_data, alias_exe, sizeof(alias_exe));
    std::memcpy(alias_data + 3, alias_source, sizeof(alias_source));
    if (!api.handle(add_alias_message) || state.aliases.size() != 1)
    {
        std::fprintf(stderr, "delete alias failed\n");
        return EXIT_FAILURE;
    }
    std::memcpy(alias_data, alias_exe, sizeof(alias_exe));
    std::memcpy(alias_data + 3, u"z", sizeof(char16_t));
    if (!api.handle(add_alias_message) || !state.aliases.empty())
    {
        std::fprintf(stderr, "delete second alias failed\n");
        return EXIT_FAILURE;
    }

    corehost::consoleinput::input_queue write_mouse_queue;
    corehost::consoleapi::console_api write_mouse_api{buffer, state, &write_mouse_queue};
    corehost::coreio::io_msg write_mouse_message{};
    write_mouse_message.descriptor.InputSize =
        sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_WRITECONSOLEINPUT_MSG) + sizeof(INPUT_RECORD);
    write_mouse_message.descriptor.OutputSize = sizeof(write_mouse_message.body);
    auto *write_mouse_header = reinterpret_cast<CONSOLE_MSG_HEADER *>(write_mouse_message.body);
    write_mouse_header->ApiNumber = ConsolepWriteConsoleInput;
    auto *write_mouse_request =
        reinterpret_cast<CONSOLE_WRITECONSOLEINPUT_MSG *>(write_mouse_message.body + sizeof(CONSOLE_MSG_HEADER));
    write_mouse_request->Append = TRUE;
    write_mouse_request->NumRecords = 1;
    auto *write_mouse_record = reinterpret_cast<INPUT_RECORD *>(write_mouse_request + 1);
    write_mouse_record->EventType = MOUSE_EVENT;
    write_mouse_record->Event.MouseEvent.dwMousePosition = COORD{4, 2};
    write_mouse_record->Event.MouseEvent.dwButtonState = 1;
    if (!write_mouse_api.handle(write_mouse_message) || write_mouse_queue.record_count() != 1)
    {
        std::fprintf(stderr, "write mouse input failed\n");
        return EXIT_FAILURE;
    }
    corehost::consoleinput::input_event written_mouse{};
    (void)write_mouse_queue.read(std::span<corehost::consoleinput::input_event>{&written_mouse, 1});
    if (written_mouse.type != corehost::consoleinput::input_event_type::mouse || written_mouse.mouse.x != 4)
    {
        std::fprintf(stderr, "write mouse input data failed\n");
        return EXIT_FAILURE;
    }

    add_alias_request->TargetLength = 1;
    add_alias_message.descriptor.InputSize = sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_ADDALIAS_MSG) +
                                             sizeof(alias_exe) + sizeof(alias_source) + sizeof(alias_target);
    std::memcpy(alias_data, alias_exe, sizeof(alias_exe));
    std::memcpy(alias_data + 3, alias_source, sizeof(alias_source));
    std::memcpy(alias_data + 4, alias_target, sizeof(alias_target));
    if (!api.handle(add_alias_message))
    {
        std::fprintf(stderr, "restore alias failed\n");
        return EXIT_FAILURE;
    }

    corehost::coreio::io_msg filtered_length_message{};
    filtered_length_message.descriptor.InputSize =
        sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_GETALIASESLENGTH_MSG) + sizeof(alias_exe);
    filtered_length_message.descriptor.OutputSize = sizeof(filtered_length_message.body);
    auto *filtered_header = reinterpret_cast<CONSOLE_MSG_HEADER *>(filtered_length_message.body);
    filtered_header->ApiNumber = ConsolepGetAliasesLength;
    auto *filtered_request =
        reinterpret_cast<CONSOLE_GETALIASESLENGTH_MSG *>(filtered_length_message.body + sizeof(CONSOLE_MSG_HEADER));
    filtered_request->Unicode = TRUE;
    std::memcpy(filtered_request + 1, alias_exe, sizeof(alias_exe));
    if (!api.handle(filtered_length_message) || filtered_request->AliasesLength != 8)
    {
        std::fprintf(stderr, "aliases length exe filter failed\n");
        return EXIT_FAILURE;
    }
    *reinterpret_cast<char16_t *>(filtered_request + 1) = u'X';
    if (!api.handle(filtered_length_message) || filtered_request->AliasesLength != 0)
    {
        std::fprintf(stderr, "aliases length unknown exe failed\n");
        return EXIT_FAILURE;
    }

    corehost::consoleinput::input_queue pending_queue;
    corehost::screen::screen_buffer pending_buffer{4, 2};
    corehost::screen::console_state pending_state{4, 2};
    corehost::consoleapi::console_api pending_api{pending_buffer, pending_state, &pending_queue};
    corehost::coreio::io_msg pending_message{};
    pending_message.descriptor.InputSize = sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_READCONSOLE_MSG);
    pending_message.descriptor.OutputSize =
        sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_READCONSOLE_MSG) + sizeof(char16_t);
    auto *pending_header = reinterpret_cast<CONSOLE_MSG_HEADER *>(pending_message.body);
    pending_header->ApiNumber = ConsolepReadConsole;
    auto *pending_request =
        reinterpret_cast<CONSOLE_READCONSOLE_MSG *>(pending_message.body + sizeof(CONSOLE_MSG_HEADER));
    pending_request->Unicode = TRUE;
    if (pending_api.handle(pending_message))
    {
        std::fprintf(stderr, "read console did not suspend\n");
        return EXIT_FAILURE;
    }
    pending_queue.push(corehost::consoleinput::input_event{corehost::consoleinput::input_event_type::key,
                                                           corehost::consoleinput::key_event{u'A'}});
    pending_queue.push(corehost::consoleinput::input_event{corehost::consoleinput::input_event_type::key,
                                                           corehost::consoleinput::key_event{u'\r'}});
    if (!pending_api.resume_read(pending_message) || pending_request->NumBytes != 3 * sizeof(char16_t))
    {
        std::fprintf(stderr, "read console resume failed\n");
        return EXIT_FAILURE;
    }

    corehost::coreio::io_msg ctrl_message{};
    ctrl_message.descriptor.InputSize = sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_CTRLEVENT_MSG);
    ctrl_message.descriptor.OutputSize = sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_CTRLEVENT_MSG);
    auto *ctrl_header = reinterpret_cast<CONSOLE_MSG_HEADER *>(ctrl_message.body);
    ctrl_header->ApiNumber = ConsolepGenerateCtrlEvent;
    auto *ctrl_request = reinterpret_cast<CONSOLE_CTRLEVENT_MSG *>(ctrl_message.body + sizeof(CONSOLE_MSG_HEADER));
    ctrl_request->CtrlEvent = CTRL_C_EVENT;
    if (!api.handle(ctrl_message) || ctrl_message.complete.IoStatus.Status != 0)
    {
        std::fprintf(stderr, "generate ctrl event failed\n");
        return EXIT_FAILURE;
    }
    ctrl_request->CtrlEvent = 9;
    if (!api.handle(ctrl_message) ||
        ctrl_message.complete.IoStatus.Status != corehost::coreio::error::status_invalid_parameter)
    {
        std::fprintf(stderr, "invalid ctrl event accepted\n");
        return EXIT_FAILURE;
    }

    corehost::consoleinput::input_queue flush_queue;
    corehost::screen::screen_buffer flush_buffer{4, 2};
    corehost::screen::console_state flush_state{4, 2};
    corehost::consoleapi::console_api flush_api{flush_buffer, flush_state, &flush_queue};
    corehost::coreio::io_msg cancel_message{};
    cancel_message.descriptor.InputSize = sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_READCONSOLE_MSG);
    cancel_message.descriptor.OutputSize =
        sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_READCONSOLE_MSG) + sizeof(char16_t);
    auto *cancel_header = reinterpret_cast<CONSOLE_MSG_HEADER *>(cancel_message.body);
    cancel_header->ApiNumber = ConsolepReadConsole;
    auto *cancel_read = reinterpret_cast<CONSOLE_READCONSOLE_MSG *>(cancel_message.body + sizeof(CONSOLE_MSG_HEADER));
    cancel_read->Unicode = TRUE;
    if (flush_api.handle(cancel_message))
    {
        std::fprintf(stderr, "flush pending read did not suspend\n");
        return EXIT_FAILURE;
    }
    corehost::coreio::io_msg flush_request{};
    flush_request.descriptor.InputSize = sizeof(CONSOLE_MSG_HEADER);
    auto *flush_request_header = reinterpret_cast<CONSOLE_MSG_HEADER *>(flush_request.body);
    flush_request_header->ApiNumber = ConsolepFlushInputBuffer;
    if (!flush_api.handle(flush_request) || !flush_api.take_cancel_pending())
    {
        std::fprintf(stderr, "flush did not cancel pending read\n");
        return EXIT_FAILURE;
    }

    corehost::consoleinput::input_queue input_wait_queue;
    corehost::screen::screen_buffer input_wait_buffer{4, 2};
    corehost::screen::console_state input_wait_state{4, 2};
    corehost::consoleapi::console_api input_wait_api{input_wait_buffer, input_wait_state, &input_wait_queue};
    corehost::coreio::io_msg input_wait_message{};
    input_wait_message.descriptor.InputSize = sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_GETCONSOLEINPUT_MSG);
    input_wait_message.descriptor.OutputSize =
        sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_GETCONSOLEINPUT_MSG) + sizeof(INPUT_RECORD);
    auto *input_wait_header = reinterpret_cast<CONSOLE_MSG_HEADER *>(input_wait_message.body);
    input_wait_header->ApiNumber = ConsolepGetConsoleInput;
    auto *input_wait_request =
        reinterpret_cast<CONSOLE_GETCONSOLEINPUT_MSG *>(input_wait_message.body + sizeof(CONSOLE_MSG_HEADER));
    input_wait_request->Flags = 0;
    if (input_wait_api.handle(input_wait_message) ||
        input_wait_api.take_pending_read_kind() != corehost::consoleapi::console_api::pending_read_kind::console_input)
    {
        std::fprintf(stderr, "GetConsoleInput did not suspend\n");
        return EXIT_FAILURE;
    }
    input_wait_queue.push(corehost::consoleinput::input_event{corehost::consoleinput::input_event_type::key,
                                                              corehost::consoleinput::key_event{u'A'}});
    if (!input_wait_api.resume_console_input(input_wait_message) || input_wait_request->NumRecords != 1)
    {
        std::fprintf(stderr, "GetConsoleInput resume failed\n");
        return EXIT_FAILURE;
    }

    corehost::consoleinput::input_queue peek_queue;
    peek_queue.push(corehost::consoleinput::input_event{corehost::consoleinput::input_event_type::key,
                                                        corehost::consoleinput::key_event{u'A'}});
    corehost::screen::screen_buffer peek_buffer{4, 2};
    corehost::screen::console_state peek_state{4, 2};
    corehost::consoleapi::console_api peek_api{peek_buffer, peek_state, &peek_queue};
    corehost::coreio::io_msg peek_message{};
    peek_message.descriptor.InputSize = sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_GETCONSOLEINPUT_MSG);
    peek_message.descriptor.OutputSize =
        sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_GETCONSOLEINPUT_MSG) + sizeof(INPUT_RECORD);
    auto *peek_header = reinterpret_cast<CONSOLE_MSG_HEADER *>(peek_message.body);
    peek_header->ApiNumber = ConsolepGetConsoleInput;
    auto *peek_request =
        reinterpret_cast<CONSOLE_GETCONSOLEINPUT_MSG *>(peek_message.body + sizeof(CONSOLE_MSG_HEADER));
    peek_request->Flags = CONSOLE_READ_NOREMOVE;
    if (!peek_api.handle(peek_message) || peek_request->NumRecords != 1 || peek_queue.record_count() != 1)
    {
        std::fprintf(stderr, "GetConsoleInput NOREMOVE failed\n");
        return EXIT_FAILURE;
    }

    corehost::consoleinput::input_queue edit_queue;
    corehost::screen::screen_buffer edit_buffer{20, 4};
    corehost::screen::console_state edit_state{20, 4};
    corehost::consoleapi::console_api edit_api{edit_buffer, edit_state, &edit_queue};
    edit_queue.push(corehost::consoleinput::input_event{corehost::consoleinput::input_event_type::key,
                                                        corehost::consoleinput::key_event{u'A'}});
    edit_queue.push(corehost::consoleinput::input_event{corehost::consoleinput::input_event_type::key,
                                                        corehost::consoleinput::key_event{u'B'}});
    corehost::consoleinput::key_event left_key;
    left_key.character = 0;
    left_key.key = corehost::consoleinput::virtual_key::left;
    edit_queue.push(corehost::consoleinput::input_event{corehost::consoleinput::input_event_type::key, left_key});
    edit_queue.push(corehost::consoleinput::input_event{corehost::consoleinput::input_event_type::key,
                                                        corehost::consoleinput::key_event{u'X'}});
    edit_queue.push(corehost::consoleinput::input_event{corehost::consoleinput::input_event_type::key,
                                                        corehost::consoleinput::key_event{u'\r'}});
    corehost::coreio::io_msg edit_message{};
    edit_message.descriptor.InputSize = sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_READCONSOLE_MSG);
    edit_message.descriptor.OutputSize =
        sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_READCONSOLE_MSG) + 16 * sizeof(char16_t);
    auto *edit_header = reinterpret_cast<CONSOLE_MSG_HEADER *>(edit_message.body);
    edit_header->ApiNumber = ConsolepReadConsole;
    auto *edit_request = reinterpret_cast<CONSOLE_READCONSOLE_MSG *>(edit_message.body + sizeof(CONSOLE_MSG_HEADER));
    edit_request->Unicode = TRUE;
    if (!edit_api.handle(edit_message))
    {
        std::fprintf(stderr, "line edit read failed\n");
        return EXIT_FAILURE;
    }
    const auto *edit_output = reinterpret_cast<const char16_t *>(edit_message.body + sizeof(CONSOLE_MSG_HEADER) +
                                                                 sizeof(CONSOLE_READCONSOLE_MSG));
    if (edit_request->NumBytes != 5 * sizeof(char16_t) || edit_output[0] != u'A' || edit_output[1] != u'X' ||
        edit_output[2] != u'B')
    {
        std::fprintf(stderr, "line editing failed\n");
        return EXIT_FAILURE;
    }

    edit_queue.push(corehost::consoleinput::input_event{corehost::consoleinput::input_event_type::key, left_key});
    corehost::consoleinput::key_event up_key;
    up_key.character = 0;
    up_key.key = corehost::consoleinput::virtual_key::up;
    edit_queue.push(corehost::consoleinput::input_event{corehost::consoleinput::input_event_type::key, up_key});
    edit_queue.push(corehost::consoleinput::input_event{corehost::consoleinput::input_event_type::key,
                                                        corehost::consoleinput::key_event{u'\r'}});
    if (!edit_api.handle(edit_message) || edit_request->NumBytes != 5 * sizeof(char16_t) || edit_output[0] != u'A' ||
        edit_output[1] != u'X' || edit_output[2] != u'B')
    {
        std::fprintf(stderr, "history recall failed\n");
        return EXIT_FAILURE;
    }

    corehost::consoleinput::input_queue prepend_queue;
    prepend_queue.push(corehost::consoleinput::input_event{corehost::consoleinput::input_event_type::key,
                                                           corehost::consoleinput::key_event{u'C'}});
    corehost::screen::screen_buffer prepend_buffer{4, 2};
    corehost::screen::console_state prepend_state{4, 2};
    corehost::consoleapi::console_api prepend_api{prepend_buffer, prepend_state, &prepend_queue};
    corehost::coreio::io_msg prepend_message{};
    prepend_message.descriptor.InputSize =
        sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_WRITECONSOLEINPUT_MSG) + 2 * sizeof(INPUT_RECORD);
    prepend_message.descriptor.OutputSize = sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_WRITECONSOLEINPUT_MSG);
    auto *prepend_header = reinterpret_cast<CONSOLE_MSG_HEADER *>(prepend_message.body);
    prepend_header->ApiNumber = ConsolepWriteConsoleInput;
    auto *prepend_request =
        reinterpret_cast<CONSOLE_WRITECONSOLEINPUT_MSG *>(prepend_message.body + sizeof(CONSOLE_MSG_HEADER));
    prepend_request->NumRecords = 2;
    auto *prepend_records = reinterpret_cast<INPUT_RECORD *>(prepend_message.body + sizeof(CONSOLE_MSG_HEADER) +
                                                             sizeof(CONSOLE_WRITECONSOLEINPUT_MSG));
    prepend_records[0].EventType = KEY_EVENT;
    prepend_records[0].Event.KeyEvent.bKeyDown = TRUE;
    prepend_records[0].Event.KeyEvent.uChar.UnicodeChar = u'A';
    prepend_records[1].EventType = KEY_EVENT;
    prepend_records[1].Event.KeyEvent.bKeyDown = TRUE;
    prepend_records[1].Event.KeyEvent.uChar.UnicodeChar = u'B';
    if (!prepend_api.handle(prepend_message))
    {
        std::fprintf(stderr, "WriteConsoleInput prepend failed\n");
        return EXIT_FAILURE;
    }
    std::array<corehost::consoleinput::input_event, 3> prepend_events{};
    const auto prepend_count = prepend_queue.read(std::span<corehost::consoleinput::input_event>{prepend_events});
    if (prepend_count != 3 || prepend_events[0].key.character != u'A' || prepend_events[1].key.character != u'B' ||
        prepend_events[2].key.character != u'C')
    {
        std::fprintf(stderr, "WriteConsoleInput prepend order failed\n");
        return EXIT_FAILURE;
    }

    corehost::consoleinput::input_queue partial_queue;
    corehost::screen::screen_buffer partial_buffer{8, 2};
    corehost::screen::console_state partial_state{8, 2};
    corehost::consoleapi::console_api partial_api{partial_buffer, partial_state, &partial_queue};
    partial_queue.push(corehost::consoleinput::input_event{corehost::consoleinput::input_event_type::key,
                                                           corehost::consoleinput::key_event{u'A'}});
    corehost::coreio::io_msg partial_message{};
    partial_message.descriptor.InputSize = sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_READCONSOLE_MSG);
    partial_message.descriptor.OutputSize =
        sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_READCONSOLE_MSG) + 8 * sizeof(char16_t);
    auto *partial_header = reinterpret_cast<CONSOLE_MSG_HEADER *>(partial_message.body);
    partial_header->ApiNumber = ConsolepReadConsole;
    auto *partial_request =
        reinterpret_cast<CONSOLE_READCONSOLE_MSG *>(partial_message.body + sizeof(CONSOLE_MSG_HEADER));
    partial_request->Unicode = TRUE;
    if (partial_api.handle(partial_message) ||
        partial_api.take_pending_read_kind() != corehost::consoleapi::console_api::pending_read_kind::console_read)
    {
        std::fprintf(stderr, "partial line did not suspend\n");
        return EXIT_FAILURE;
    }
    partial_queue.push(corehost::consoleinput::input_event{corehost::consoleinput::input_event_type::key,
                                                           corehost::consoleinput::key_event{u'\r'}});
    if (!partial_api.resume_read(partial_message) || partial_request->NumBytes != 3 * sizeof(char16_t))
    {
        std::fprintf(stderr, "partial line resume failed\n");
        return EXIT_FAILURE;
    }
    const auto *partial_data = reinterpret_cast<const char16_t *>(partial_message.body + sizeof(CONSOLE_MSG_HEADER) +
                                                                  sizeof(CONSOLE_READCONSOLE_MSG));
    if (partial_data[0] != u'A' || partial_data[1] != u'\r')
    {
        std::fprintf(stderr, "partial line data failed\n");
        return EXIT_FAILURE;
    }

    corehost::consoleinput::input_queue ansi_queue;
    ansi_queue.push(corehost::consoleinput::input_event{corehost::consoleinput::input_event_type::key,
                                                        corehost::consoleinput::key_event{u'A'}});
    ansi_queue.push(corehost::consoleinput::input_event{corehost::consoleinput::input_event_type::key,
                                                        corehost::consoleinput::key_event{u'\r'}});
    corehost::screen::screen_buffer ansi_buffer{8, 2};
    corehost::screen::console_state ansi_state{8, 2};
    corehost::consoleapi::console_api ansi_api{ansi_buffer, ansi_state, &ansi_queue};
    corehost::coreio::io_msg ansi_message{};
    ansi_message.descriptor.InputSize = sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_READCONSOLE_MSG);
    ansi_message.descriptor.OutputSize = sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_READCONSOLE_MSG) + 8;
    auto *ansi_header = reinterpret_cast<CONSOLE_MSG_HEADER *>(ansi_message.body);
    ansi_header->ApiNumber = ConsolepReadConsole;
    auto *ansi_request = reinterpret_cast<CONSOLE_READCONSOLE_MSG *>(ansi_message.body + sizeof(CONSOLE_MSG_HEADER));
    ansi_request->Unicode = FALSE;
    if (!ansi_api.handle(ansi_message) || ansi_request->NumBytes != 3)
    {
        std::fprintf(stderr, "ansi read console failed bytes=%lu unicode=%d\\n", ansi_request->NumBytes,
                     ansi_request->Unicode);
        return EXIT_FAILURE;
    }
    const auto *ansi_data = reinterpret_cast<const char *>(ansi_message.body + sizeof(CONSOLE_MSG_HEADER) +
                                                           sizeof(CONSOLE_READCONSOLE_MSG));
    if (ansi_data[0] != 'A' || ansi_data[1] != '\r' || ansi_data[2] != '\n')
    {
        std::fprintf(stderr, "ansi read console data failed\n");
        return EXIT_FAILURE;
    }

    corehost::screen::screen_buffer ansi_write_buffer{8, 2};
    corehost::screen::console_state ansi_write_state{8, 2};
    ansi_write_state.input_code_page = 65001;
    corehost::consoleapi::console_api ansi_write_api{ansi_write_buffer, ansi_write_state};
    corehost::coreio::io_msg ansi_write_message{};
    ansi_write_message.descriptor.InputSize = sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_WRITECONSOLE_MSG) + 2;
    ansi_write_message.descriptor.OutputSize = sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_WRITECONSOLE_MSG);
    auto *ansi_write_header = reinterpret_cast<CONSOLE_MSG_HEADER *>(ansi_write_message.body);
    ansi_write_header->ApiNumber = ConsolepWriteConsole;
    auto *ansi_write_request =
        reinterpret_cast<CONSOLE_WRITECONSOLE_MSG *>(ansi_write_message.body + sizeof(CONSOLE_MSG_HEADER));
    ansi_write_request->Unicode = FALSE;
    auto *ansi_write_data = reinterpret_cast<char *>(ansi_write_message.body + sizeof(CONSOLE_MSG_HEADER) +
                                                     sizeof(CONSOLE_WRITECONSOLE_MSG));
    ansi_write_data[0] = 'O';
    ansi_write_data[1] = 'K';
    if (!ansi_write_api.handle(ansi_write_message) || ansi_write_buffer.at(0, 0).primary() != u'O' ||
        ansi_write_buffer.at(1, 0).primary() != u'K')
    {
        std::fprintf(stderr, "ansi write console failed\n");
        return EXIT_FAILURE;
    }

    (void)ansi_write_buffer.write_scalar(0, 1, u'A', corehost::screen::cell_style{});
    (void)ansi_write_buffer.write_scalar(1, 1, u'B', corehost::screen::cell_style{});
    corehost::coreio::io_msg ansi_read_message{};
    ansi_read_message.descriptor.InputSize = sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_READCONSOLEOUTPUTSTRING_MSG);
    ansi_read_message.descriptor.OutputSize = sizeof(CONSOLE_READCONSOLEOUTPUTSTRING_MSG) + 4;
    auto *ansi_read_header = reinterpret_cast<CONSOLE_MSG_HEADER *>(ansi_read_message.body);
    ansi_read_header->ApiNumber = ConsolepReadConsoleOutputString;
    auto *ansi_read_request =
        reinterpret_cast<CONSOLE_READCONSOLEOUTPUTSTRING_MSG *>(ansi_read_message.body + sizeof(CONSOLE_MSG_HEADER));
    ansi_read_request->ReadCoord = COORD{0, 1};
    ansi_read_request->NumRecords = 2;
    ansi_read_request->StringType = CONSOLE_ASCII;
    if (!ansi_write_api.handle(ansi_read_message) || ansi_read_request->NumRecords != 2)
    {
        std::fprintf(stderr, "ansi read output string failed\n");
        return EXIT_FAILURE;
    }
    const auto *ansi_read_data = reinterpret_cast<const char *>(ansi_read_message.body + sizeof(CONSOLE_MSG_HEADER) +
                                                                sizeof(CONSOLE_READCONSOLEOUTPUTSTRING_MSG));
    if (ansi_read_data[0] != 'A' || ansi_read_data[1] != 'B')
    {
        std::fprintf(stderr, "ansi read output data failed\n");
        return EXIT_FAILURE;
    }

    corehost::consoleinput::input_queue ansi_input_queue;
    corehost::consoleapi::console_api ansi_input_api{ansi_write_buffer, ansi_write_state, &ansi_input_queue};
    corehost::coreio::io_msg ansi_input_message{};
    ansi_input_message.descriptor.InputSize =
        sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_WRITECONSOLEINPUT_MSG) + sizeof(INPUT_RECORD);
    ansi_input_message.descriptor.OutputSize = sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_WRITECONSOLEINPUT_MSG);
    auto *ansi_input_header = reinterpret_cast<CONSOLE_MSG_HEADER *>(ansi_input_message.body);
    ansi_input_header->ApiNumber = ConsolepWriteConsoleInput;
    auto *ansi_input_request =
        reinterpret_cast<CONSOLE_WRITECONSOLEINPUT_MSG *>(ansi_input_message.body + sizeof(CONSOLE_MSG_HEADER));
    ansi_input_request->NumRecords = 1;
    ansi_input_request->Unicode = FALSE;
    auto *ansi_input_record = reinterpret_cast<INPUT_RECORD *>(ansi_input_message.body + sizeof(CONSOLE_MSG_HEADER) +
                                                               sizeof(CONSOLE_WRITECONSOLEINPUT_MSG));
    ansi_input_record->EventType = KEY_EVENT;
    ansi_input_record->Event.KeyEvent.bKeyDown = TRUE;
    ansi_input_record->Event.KeyEvent.uChar.AsciiChar = 'Z';
    if (!ansi_input_api.handle(ansi_input_message))
    {
        std::fprintf(stderr, "ansi write input failed\n");
        return EXIT_FAILURE;
    }
    corehost::consoleinput::input_event ansi_event{};
    if (ansi_input_queue.read(std::span<corehost::consoleinput::input_event>{&ansi_event, 1}) != 1 ||
        ansi_event.key.character != u'Z')
    {
        std::fprintf(stderr, "ansi write input data failed\n");
        return EXIT_FAILURE;
    }

    corehost::coreio::process_registry ctrl_processes;
    ctrl_processes.add(42, 7);
    corehost::screen::screen_buffer ctrl_buffer{4, 2};
    corehost::screen::console_state ctrl_state{4, 2};
    corehost::consoleapi::console_api ctrl_api{ctrl_buffer, ctrl_state, nullptr, 0, &ctrl_processes};
    corehost::coreio::io_msg ctrl_group_message{};
    ctrl_group_message.descriptor.InputSize = sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_CTRLEVENT_MSG);
    auto *ctrl_group_header = reinterpret_cast<CONSOLE_MSG_HEADER *>(ctrl_group_message.body);
    ctrl_group_header->ApiNumber = ConsolepGenerateCtrlEvent;
    auto *ctrl_group_request =
        reinterpret_cast<CONSOLE_CTRLEVENT_MSG *>(ctrl_group_message.body + sizeof(CONSOLE_MSG_HEADER));
    ctrl_group_request->CtrlEvent = CTRL_C_EVENT;
    ctrl_group_request->ProcessGroupId = 7;
    if (!ctrl_api.handle(ctrl_group_message))
    {
        std::fprintf(stderr, "group ctrl event failed\n");
        return EXIT_FAILURE;
    }
    std::vector<std::pair<DWORD, DWORD>> ctrl_events;
    ctrl_api.take_ctrl_events(ctrl_events);
    if (ctrl_events.size() != 1 || ctrl_events[0].first != CTRL_C_EVENT || ctrl_events[0].second != 42)
    {
        std::fprintf(stderr, "ctrl target failed\n");
        return EXIT_FAILURE;
    }

    corehost::consoleinput::input_queue ctrl_c_queue;
    ctrl_c_queue.push(corehost::consoleinput::input_event{corehost::consoleinput::input_event_type::key,
                                                          corehost::consoleinput::key_event{u'\x03'}});
    corehost::screen::screen_buffer ctrl_c_buffer{8, 2};
    corehost::screen::console_state ctrl_c_state{8, 2};
    corehost::consoleapi::console_api ctrl_c_api{ctrl_c_buffer, ctrl_c_state, &ctrl_c_queue};
    corehost::coreio::io_msg ctrl_c_message{};
    ctrl_c_message.descriptor.InputSize = sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_READCONSOLE_MSG);
    ctrl_c_message.descriptor.OutputSize = sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_READCONSOLE_MSG) + 8;
    auto *ctrl_c_header = reinterpret_cast<CONSOLE_MSG_HEADER *>(ctrl_c_message.body);
    ctrl_c_header->ApiNumber = ConsolepReadConsole;
    auto *ctrl_c_request =
        reinterpret_cast<CONSOLE_READCONSOLE_MSG *>(ctrl_c_message.body + sizeof(CONSOLE_MSG_HEADER));
    ctrl_c_request->Unicode = TRUE;
    if (!ctrl_c_api.handle(ctrl_c_message) ||
        ctrl_c_message.complete.IoStatus.Status != corehost::coreio::error::status_alerted ||
        ctrl_c_request->NumBytes != 0)
    {
        std::fprintf(stderr, "ctrl+c read alert failed\n");
        return EXIT_FAILURE;
    }

    std::printf("libconsoleapi tests passed\n");
    return EXIT_SUCCESS;
}
