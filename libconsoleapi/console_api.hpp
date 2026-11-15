#pragma once

#include "console_state.hpp"
#include "screen_buffer.hpp"

#include "win32_types.hpp"
#include "input.hpp"

#include "condrv_io.hpp"
#include "os/Console/conmsgl1.h"
#include "os/Console/conmsgl2.h"
#include "os/Console/conmsgl3.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <span>
#include <utility>
#include <string_view>

namespace corehost::consoleapi
{

// Native console-facing operations. This layer owns cursor advancement and
// ABI projection, while libscreen owns cell storage.
class console_api final
{
  public:
    console_api(screen::screen_buffer &buffer, screen::console_state &state,
                consoleinput::input_queue *input = nullptr) noexcept
        : buffer_{buffer}, state_{state}, input_{input}
    {
    }

    [[nodiscard]] bool write_text(std::u16string_view text)
    {
        screen::coord cursor{
            static_cast<std::int32_t>(state_.cursor().x),
            static_cast<std::int32_t>(state_.cursor().y),
        };
        const auto result = buffer_.write_text_row<screen::text_measurement::console>(
            cursor, text, {}, state_.default_style, true, state_.default_hyperlink);
        state_.set_cursor(static_cast<std::size_t>(cursor.x),
                          static_cast<std::size_t>(cursor.y));
        return result.consumed == text.size();
    }

    void fill_cells(screen::rect area, char16_t character, std::uint16_t attributes) noexcept
    {
        buffer_.fill_chars(area, character);
        buffer_.fill_attrs(area, attributes);
    }

    void scroll_cells(screen::rectangle source, screen::rectangle clip, bool use_clip,
                      screen::coord destination) noexcept
    {
        buffer_.copy_rect(source, clip, use_clip, destination);
    }

    [[nodiscard]] bool handle(coreio::io_msg &message) noexcept
    {
        if (message.descriptor.InputSize < sizeof(CONSOLE_MSG_HEADER))
        {
            coreio::prepare_completion(message, coreio::error::status_invalid_parameter);
            return true;
        }

        const auto *header = reinterpret_cast<const CONSOLE_MSG_HEADER *>(message.body);
        if (header->ApiNumber == ConsolepGetScreenBufferInfo)
        {
            return get_screen_buffer_info(message);
        }
        if (header->ApiNumber == ConsolepGetCursorInfo)
        {
            return get_cursor_info(message);
        }
        if (header->ApiNumber == ConsolepGetLargestWindowSize)
        {
            return get_largest_window_size(message);
        }
        if (header->ApiNumber == ConsolepReadConsole)
        {
            return read_console(message);
        }
        if (header->ApiNumber == ConsolepSetTextAttribute)
        {
            return set_text_attribute(message);
        }
        if (header->ApiNumber == ConsolepScrollScreenBuffer)
        {
            return scroll_console_buffer(message);
        }
        if (header->ApiNumber == ConsolepSetWindowInfo)
        {
            return set_window_info(message);
        }
        if (header->ApiNumber == ConsolepWriteConsole)
        {
            return write_console(message);
        }
        if (header->ApiNumber == ConsolepWriteConsoleInput)
        {
            return write_console_input(message);
        }
        if (header->ApiNumber == ConsolepGetFontSize)
        {
            return get_font_size(message);
        }
        if (header->ApiNumber == ConsolepGetSelectionInfo)
        {
            return get_selection_info(message);
        }
        if (header->ApiNumber == ConsolepGetLangId)
        {
            return get_language_id(message);
        }
        if (header->ApiNumber == ConsolepGetTitle)
        {
            return get_title(message);
        }
        if (header->ApiNumber == ConsolepSetTitle)
        {
            return set_title(message);
        }
        if (header->ApiNumber == ConsolepGetCP)
        {
            return get_code_page(message);
        }
        if (header->ApiNumber == ConsolepSetCP)
        {
            return set_code_page(message);
        }
        if (header->ApiNumber == ConsolepGetConsoleInput)
        {
            return read_console_input(message);
        }
        if (header->ApiNumber == ConsolepGetNumberOfInputEvents)
        {
            return get_number_of_input_events(message);
        }
        if (header->ApiNumber == ConsolepSetScreenBufferSize)
        {
            return set_screen_buffer_size(message);
        }
        if (header->ApiNumber == ConsolepReadConsoleOutputString)
        {
            return read_console_output_string(message);
        }
        if (header->ApiNumber == ConsolepWriteConsoleOutputString)
        {
            return write_console_output_string(message);
        }
        if (header->ApiNumber == ConsolepFillConsoleOutput)
        {
            return fill_console_output(message);
        }
        if (header->ApiNumber == ConsolepReadConsoleOutput)
        {
            return read_console_output(message);
        }
        if (header->ApiNumber == ConsolepWriteConsoleOutput)
        {
            return write_console_output(message);
        }
        if (header->ApiNumber == ConsolepSetCursorInfo)
        {
            return set_cursor_info(message);
        }
        if (header->ApiNumber == ConsolepSetCursorPosition)
        {
            return set_cursor_position(message);
        }

        coreio::prepare_completion(message, coreio::error::status_not_implemented);
        return true;
    }

    [[nodiscard]] bool get_cursor_info(coreio::io_msg &message) noexcept
    {
        if (message.descriptor.InputSize < sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_GETCURSORINFO_MSG))
        {
            coreio::prepare_completion(message, coreio::error::status_invalid_parameter);
            return true;
        }
        auto *info = reinterpret_cast<CONSOLE_GETCURSORINFO_MSG *>(
            message.body + sizeof(CONSOLE_MSG_HEADER));
        info->CursorSize = state_.cursor().size;
        info->Visible = state_.cursor().visible ? TRUE : FALSE;
        coreio::prepare_completion(message, 0, sizeof(CONSOLE_GETCURSORINFO_MSG),
                                   message.body + sizeof(CONSOLE_MSG_HEADER),
                                   sizeof(CONSOLE_GETCURSORINFO_MSG));
        return true;
    }

    [[nodiscard]] bool read_console_input(coreio::io_msg &message) noexcept
    {
        if (message.descriptor.InputSize < sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_GETCONSOLEINPUT_MSG))
        {
            coreio::prepare_completion(message, coreio::error::status_invalid_parameter);
            return true;
        }
        auto *request = reinterpret_cast<CONSOLE_GETCONSOLEINPUT_MSG *>(
            message.body + sizeof(CONSOLE_MSG_HEADER));
        const auto local = sizeof(message.body) - sizeof(CONSOLE_MSG_HEADER) -
                           sizeof(CONSOLE_GETCONSOLEINPUT_MSG);
        if (message.descriptor.OutputSize <= sizeof(CONSOLE_GETCONSOLEINPUT_MSG))
        {
            request->NumRecords = 0;
            coreio::prepare_completion(message, 0, sizeof(CONSOLE_GETCONSOLEINPUT_MSG),
                                       message.body + sizeof(CONSOLE_MSG_HEADER),
                                       sizeof(CONSOLE_GETCONSOLEINPUT_MSG));
            return true;
        }
        const auto capacity = std::min<std::size_t>(
            message.descriptor.OutputSize - sizeof(CONSOLE_GETCONSOLEINPUT_MSG), local);
        const auto max_records = capacity / sizeof(INPUT_RECORD);
        auto *output = reinterpret_cast<INPUT_RECORD *>(
            message.body + sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_GETCONSOLEINPUT_MSG));

        std::size_t count = 0;
        if (input_ != nullptr)
        {
            std::array<consoleinput::input_event, 64> events{};
            const auto requested = std::min<std::size_t>(max_records, events.size());
            count = input_->read(std::span<consoleinput::input_event>{events.data(), requested});
            for (std::size_t i = 0; i < count; ++i)
            {
                auto &record = output[i];
                record = INPUT_RECORD{};
                record.EventType = KEY_EVENT;
                record.Event.KeyEvent.bKeyDown = events[i].key.key_down ? TRUE : FALSE;
                record.Event.KeyEvent.wRepeatCount = events[i].key.repeat_count;
                record.Event.KeyEvent.wVirtualKeyCode = static_cast<WORD>(std::to_underlying(events[i].key.key));
                record.Event.KeyEvent.wVirtualScanCode = events[i].key.scan_code;
                record.Event.KeyEvent.uChar.UnicodeChar = events[i].key.character;
                record.Event.KeyEvent.dwControlKeyState = events[i].key.control_state;
            }
        }
        request->NumRecords = static_cast<ULONG>(count);
        coreio::prepare_completion(message, 0, count * sizeof(INPUT_RECORD),
                                   message.body + sizeof(CONSOLE_MSG_HEADER),
                                   count * sizeof(INPUT_RECORD));
        return true;
    }

    [[nodiscard]] bool read_console(coreio::io_msg &message) noexcept
    {
        if (message.descriptor.InputSize < sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_READCONSOLE_MSG))
        {
            coreio::prepare_completion(message, coreio::error::status_invalid_parameter);
            return true;
        }
        auto *request = reinterpret_cast<CONSOLE_READCONSOLE_MSG *>(message.body + sizeof(CONSOLE_MSG_HEADER));
        const auto local = sizeof(message.body) - sizeof(CONSOLE_MSG_HEADER) - sizeof(CONSOLE_READCONSOLE_MSG);
        if (message.descriptor.OutputSize <= sizeof(CONSOLE_READCONSOLE_MSG) || input_ == nullptr)
        {
            request->NumBytes = 0;
            coreio::prepare_completion(message, 0, sizeof(CONSOLE_READCONSOLE_MSG),
                                       message.body + sizeof(CONSOLE_MSG_HEADER),
                                       sizeof(CONSOLE_READCONSOLE_MSG));
            return true;
        }
        const auto capacity = std::min<std::size_t>(
            message.descriptor.OutputSize - sizeof(CONSOLE_READCONSOLE_MSG), local);
        auto *output = reinterpret_cast<char16_t *>(
            message.body + sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_READCONSOLE_MSG));
        const auto max_units = capacity / sizeof(char16_t);
        std::size_t units = 0;

        std::array<consoleinput::input_event, 64> events{};
        const auto parsed = input_->read(std::span<consoleinput::input_event>{events.data(), events.size()});
        for (std::size_t i = 0; i < parsed; ++i)
        {
            if (events[i].type != consoleinput::input_event_type::key || !events[i].key.key_down)
            {
                continue;
            }
            const auto character = events[i].key.character;
            if (character == u'\u000D' || character == u'\u000A')
            {
                break;
            }
            if (character == u'\u0008')
            {
                if (units != 0)
                {
                    --units;
                }
                continue;
            }
            if (character != 0 && units < max_units)
            {
                output[units++] = character;
            }
        }

        request->NumBytes = static_cast<ULONG>(units * sizeof(char16_t));
        coreio::prepare_completion(message, 0, units * sizeof(char16_t),
                                   message.body + sizeof(CONSOLE_MSG_HEADER),
                                   units * sizeof(char16_t));
        return true;
    }

    [[nodiscard]] bool set_text_attribute(coreio::io_msg &message) noexcept
    {
        if (message.descriptor.InputSize < sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_SETTEXTATTRIBUTE_MSG))
        {
            coreio::prepare_completion(message, coreio::error::status_invalid_parameter);
            return true;
        }
        const auto *request = reinterpret_cast<const CONSOLE_SETTEXTATTRIBUTE_MSG *>(
            message.body + sizeof(CONSOLE_MSG_HEADER));
        state_.default_attributes = request->Attributes;
        state_.default_style = buffer_.style_for_attributes(request->Attributes);
        coreio::prepare_completion(message);
        return true;
    }

    [[nodiscard]] bool scroll_console_buffer(coreio::io_msg &message) noexcept
    {
        if (message.descriptor.InputSize < sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_SCROLLSCREENBUFFER_MSG))
        {
            coreio::prepare_completion(message, coreio::error::status_invalid_parameter);
            return true;
        }
        const auto *request = reinterpret_cast<const CONSOLE_SCROLLSCREENBUFFER_MSG *>(
            message.body + sizeof(CONSOLE_MSG_HEADER));
        const auto source = request->ScrollRectangle;
        if (source.Left > source.Right || source.Top > source.Bottom)
        {
            coreio::prepare_completion(message, coreio::error::status_invalid_parameter);
            return true;
        }
        const screen::rectangle source_rect{static_cast<std::size_t>(source.Left),
                                           static_cast<std::size_t>(source.Top),
                                           static_cast<std::size_t>(source.Right - source.Left + 1),
                                           static_cast<std::size_t>(source.Bottom - source.Top + 1)};
        const screen::rectangle clip_rect{static_cast<std::size_t>(request->ClipRectangle.Left),
                                          static_cast<std::size_t>(request->ClipRectangle.Top),
                                          static_cast<std::size_t>(request->ClipRectangle.Right - request->ClipRectangle.Left + 1),
                                          static_cast<std::size_t>(request->ClipRectangle.Bottom - request->ClipRectangle.Top + 1)};
        const screen::coord destination{request->DestinationOrigin.X, request->DestinationOrigin.Y};
        buffer_.copy_rect(source_rect, clip_rect, request->Clip != FALSE, destination);

        const auto fill_char = request->Unicode ? request->Fill.Char.UnicodeChar
                                               : static_cast<char16_t>(request->Fill.Char.AsciiChar);
        const auto fill_attributes = request->Fill.Attributes;
        const auto fill_segment = [&](std::size_t x, std::size_t y, std::size_t width)
        {
            if (width == 0)
            {
                return;
            }
            const screen::rect segment{static_cast<std::int32_t>(x), static_cast<std::int32_t>(y),
                                       static_cast<std::int32_t>(x + width - 1), static_cast<std::int32_t>(y)};
            buffer_.fill_chars(segment, fill_char);
            buffer_.fill_attrs(segment, fill_attributes);
        };

        const auto dest_left = static_cast<std::size_t>(std::max<std::int32_t>(destination.x, 0));
        const auto dest_top = static_cast<std::size_t>(std::max<std::int32_t>(destination.y, 0));
        const auto dest_right = dest_left + source_rect.width;
        const auto dest_bottom = dest_top + source_rect.height;
        for (std::size_t y = source_rect.y; y < source_rect.y + source_rect.height; ++y)
        {
            if (y < dest_top || y >= dest_bottom)
            {
                fill_segment(source_rect.x, y, source_rect.width);
                continue;
            }
            if (source_rect.x < dest_left)
            {
                fill_segment(source_rect.x, y, std::min(dest_left, source_rect.x + source_rect.width) - source_rect.x);
            }
            const auto right_start = std::max(dest_right, source_rect.x);
            if (right_start < source_rect.x + source_rect.width)
            {
                fill_segment(right_start, y, source_rect.x + source_rect.width - right_start);
            }
        }
        coreio::prepare_completion(message);
        return true;
    }

    [[nodiscard]] bool set_window_info(coreio::io_msg &message) noexcept
    {
        if (message.descriptor.InputSize < sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_SETWINDOWINFO_MSG))
        {
            coreio::prepare_completion(message, coreio::error::status_invalid_parameter);
            return true;
        }
        const auto *request = reinterpret_cast<const CONSOLE_SETWINDOWINFO_MSG *>(
            message.body + sizeof(CONSOLE_MSG_HEADER));
        const auto &window = request->Window;
        if (window.Left > window.Right || window.Top > window.Bottom)
        {
            coreio::prepare_completion(message, coreio::error::status_invalid_parameter);
            return true;
        }
        auto area = screen::rect{window.Left, window.Top, window.Right, window.Bottom};
        if (!request->Absolute)
        {
            const auto origin = buffer_.viewport_origin();
            area = screen::rect{area.left + origin.x, area.top + origin.y,
                                area.right + origin.x, area.bottom + origin.y};
        }
        buffer_.set_viewport(area);
        coreio::prepare_completion(message);
        return true;
    }

    [[nodiscard]] bool write_console(coreio::io_msg &message) noexcept
    {
        if (message.descriptor.InputSize < sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_WRITECONSOLE_MSG))
        {
            coreio::prepare_completion(message, coreio::error::status_invalid_parameter);
            return true;
        }
        auto *request = reinterpret_cast<CONSOLE_WRITECONSOLE_MSG *>(message.body + sizeof(CONSOLE_MSG_HEADER));
        const auto bytes = message.descriptor.InputSize - sizeof(CONSOLE_MSG_HEADER) -
                           sizeof(CONSOLE_WRITECONSOLE_MSG);
        const auto count = request->Unicode ? bytes / sizeof(char16_t) : bytes;
        const auto *data = reinterpret_cast<const char16_t *>(
            message.body + sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_WRITECONSOLE_MSG));
        std::size_t written = 0;
        if (request->Unicode)
        {
            screen::coord cursor{static_cast<std::int32_t>(state_.cursor().x),
                                 static_cast<std::int32_t>(state_.cursor().y)};
            const auto result = buffer_.write_text_row<screen::text_measurement::console>(
                cursor, std::u16string_view{data, count}, {}, state_.default_style, true,
                state_.default_hyperlink);
            state_.set_cursor(static_cast<std::size_t>(cursor.x),
                              static_cast<std::size_t>(cursor.y));
            written = result.consumed;
        }
        request->NumBytes = static_cast<ULONG>(written * (request->Unicode ? sizeof(char16_t) : 1));
        coreio::prepare_completion(message, 0, sizeof(CONSOLE_WRITECONSOLE_MSG),
                                   message.body + sizeof(CONSOLE_MSG_HEADER),
                                   sizeof(CONSOLE_WRITECONSOLE_MSG));
        return true;
    }

    [[nodiscard]] bool write_console_input(coreio::io_msg &message) noexcept
    {
        if (message.descriptor.InputSize < sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_WRITECONSOLEINPUT_MSG))
        {
            coreio::prepare_completion(message, coreio::error::status_invalid_parameter);
            return true;
        }
        auto *request = reinterpret_cast<CONSOLE_WRITECONSOLEINPUT_MSG *>(
            message.body + sizeof(CONSOLE_MSG_HEADER));
        const auto bytes = message.descriptor.InputSize - sizeof(CONSOLE_MSG_HEADER) -
                           sizeof(CONSOLE_WRITECONSOLEINPUT_MSG);
        const auto records = std::min<std::size_t>(request->NumRecords, bytes / sizeof(INPUT_RECORD));
        const auto *input = reinterpret_cast<const INPUT_RECORD *>(
            message.body + sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_WRITECONSOLEINPUT_MSG));
        std::size_t written = 0;
        if (input_ != nullptr)
        {
            for (std::size_t i = 0; i < records; ++i)
            {
                if (input[i].EventType != KEY_EVENT)
                {
                    continue;
                }
                consoleinput::key_event key;
                key.character = input[i].Event.KeyEvent.uChar.UnicodeChar;
                key.key = static_cast<consoleinput::virtual_key>(input[i].Event.KeyEvent.wVirtualKeyCode);
                key.scan_code = input[i].Event.KeyEvent.wVirtualScanCode;
                key.control_state = input[i].Event.KeyEvent.dwControlKeyState;
                key.repeat_count = input[i].Event.KeyEvent.wRepeatCount;
                key.key_down = input[i].Event.KeyEvent.bKeyDown != FALSE;
                input_->push(consoleinput::input_event{consoleinput::input_event_type::key, key});
                ++written;
            }
        }
        request->NumRecords = static_cast<ULONG>(written);
        coreio::prepare_completion(message, 0, sizeof(CONSOLE_WRITECONSOLEINPUT_MSG),
                                   message.body + sizeof(CONSOLE_MSG_HEADER),
                                   sizeof(CONSOLE_WRITECONSOLEINPUT_MSG));
        return true;
    }

    [[nodiscard]] bool get_font_size(coreio::io_msg &message) noexcept
    {
        if (message.descriptor.InputSize < sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_GETFONTSIZE_MSG))
        {
            coreio::prepare_completion(message, coreio::error::status_invalid_parameter);
            return true;
        }
        auto *info = reinterpret_cast<CONSOLE_GETFONTSIZE_MSG *>(message.body + sizeof(CONSOLE_MSG_HEADER));
        if (info->FontIndex != 0)
        {
            coreio::prepare_completion(message, coreio::error::status_invalid_parameter);
            return true;
        }
        info->FontSize = COORD{static_cast<SHORT>(state_.font_size.x),
                              static_cast<SHORT>(state_.font_size.y)};
        coreio::prepare_completion(message, 0, sizeof(CONSOLE_GETFONTSIZE_MSG),
                                   message.body + sizeof(CONSOLE_MSG_HEADER),
                                   sizeof(CONSOLE_GETFONTSIZE_MSG));
        return true;
    }

    [[nodiscard]] bool get_selection_info(coreio::io_msg &message) noexcept
    {
        if (message.descriptor.InputSize < sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_GETSELECTIONINFO_MSG))
        {
            coreio::prepare_completion(message, coreio::error::status_invalid_parameter);
            return true;
        }
        auto *info = reinterpret_cast<CONSOLE_GETSELECTIONINFO_MSG *>(message.body + sizeof(CONSOLE_MSG_HEADER));
        info->SelectionInfo = CONSOLE_SELECTION_INFO{};
        const auto &selection = state_.selection();
        if (selection.active)
        {
            info->SelectionInfo.dwFlags = CONSOLE_SELECTION_IN_PROGRESS | CONSOLE_SELECTION_NOT_EMPTY;
            info->SelectionInfo.dwSelectionAnchor = COORD{static_cast<SHORT>(selection.anchor_x),
                                                          static_cast<SHORT>(selection.anchor_y)};
            const auto left = std::min(selection.anchor_x, selection.caret_x);
            const auto top = std::min(selection.anchor_y, selection.caret_y);
            const auto right = std::max(selection.anchor_x, selection.caret_x);
            const auto bottom = std::max(selection.anchor_y, selection.caret_y);
            info->SelectionInfo.srSelection = SMALL_RECT{static_cast<SHORT>(left), static_cast<SHORT>(top),
                                                         static_cast<SHORT>(right), static_cast<SHORT>(bottom)};
        }
        coreio::prepare_completion(message, 0, sizeof(CONSOLE_GETSELECTIONINFO_MSG),
                                   message.body + sizeof(CONSOLE_MSG_HEADER),
                                   sizeof(CONSOLE_GETSELECTIONINFO_MSG));
        return true;
    }

    [[nodiscard]] bool get_title(coreio::io_msg &message) noexcept
    {
        if (message.descriptor.InputSize < sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_GETTITLE_MSG))
        {
            coreio::prepare_completion(message, coreio::error::status_invalid_parameter);
            return true;
        }
        auto *request = reinterpret_cast<CONSOLE_GETTITLE_MSG *>(message.body + sizeof(CONSOLE_MSG_HEADER));
        const auto local = sizeof(message.body) - sizeof(CONSOLE_MSG_HEADER) - sizeof(CONSOLE_GETTITLE_MSG);
        if (message.descriptor.OutputSize <= sizeof(CONSOLE_GETTITLE_MSG))
        {
            request->TitleLength = 0;
            coreio::prepare_completion(message, 0, sizeof(CONSOLE_GETTITLE_MSG),
                                       message.body + sizeof(CONSOLE_MSG_HEADER),
                                       sizeof(CONSOLE_GETTITLE_MSG));
            return true;
        }
        const auto capacity = std::min<std::size_t>(
            message.descriptor.OutputSize - sizeof(CONSOLE_GETTITLE_MSG), local);
        const auto &title = request->Original ? state_.original_title : state_.title;
        const auto units = std::min<std::size_t>(title.size(), capacity / sizeof(char16_t));
        auto *output = reinterpret_cast<char16_t *>(
            message.body + sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_GETTITLE_MSG));
        std::memcpy(output, title.data(), units * sizeof(char16_t));
        request->TitleLength = static_cast<ULONG>(units);
        coreio::prepare_completion(message, 0, units * sizeof(char16_t),
                                   message.body + sizeof(CONSOLE_MSG_HEADER),
                                   units * sizeof(char16_t));
        return true;
    }

    [[nodiscard]] bool set_title(coreio::io_msg &message) noexcept
    {
        if (message.descriptor.InputSize < sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_SETTITLE_MSG))
        {
            coreio::prepare_completion(message, coreio::error::status_invalid_parameter);
            return true;
        }
        const auto bytes = message.descriptor.InputSize - sizeof(CONSOLE_MSG_HEADER) - sizeof(CONSOLE_SETTITLE_MSG);
        const auto *data = reinterpret_cast<const char16_t *>(
            message.body + sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_SETTITLE_MSG));
        state_.title.assign(data, bytes / sizeof(char16_t));
        coreio::prepare_completion(message);
        return true;
    }

    [[nodiscard]] bool get_language_id(coreio::io_msg &message) noexcept
    {
        if (message.descriptor.InputSize < sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_LANGID_MSG))
        {
            coreio::prepare_completion(message, coreio::error::status_invalid_parameter);
            return true;
        }
        auto *info = reinterpret_cast<CONSOLE_LANGID_MSG *>(message.body + sizeof(CONSOLE_MSG_HEADER));
        info->LangId = state_.language_id;
        coreio::prepare_completion(message, 0, sizeof(CONSOLE_LANGID_MSG),
                                   message.body + sizeof(CONSOLE_MSG_HEADER), sizeof(CONSOLE_LANGID_MSG));
        return true;
    }

    [[nodiscard]] bool get_code_page(coreio::io_msg &message) noexcept
    {
        if (message.descriptor.InputSize < sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_GETCP_MSG))
        {
            coreio::prepare_completion(message, coreio::error::status_invalid_parameter);
            return true;
        }
        auto *info = reinterpret_cast<CONSOLE_GETCP_MSG *>(message.body + sizeof(CONSOLE_MSG_HEADER));
        info->CodePage = info->Output ? state_.output_code_page : state_.input_code_page;
        coreio::prepare_completion(message, 0, sizeof(CONSOLE_GETCP_MSG),
                                   message.body + sizeof(CONSOLE_MSG_HEADER), sizeof(CONSOLE_GETCP_MSG));
        return true;
    }

    [[nodiscard]] bool set_code_page(coreio::io_msg &message) noexcept
    {
        if (message.descriptor.InputSize < sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_SETCP_MSG))
        {
            coreio::prepare_completion(message, coreio::error::status_invalid_parameter);
            return true;
        }
        const auto *info = reinterpret_cast<const CONSOLE_SETCP_MSG *>(message.body + sizeof(CONSOLE_MSG_HEADER));
        if (info->Output)
        {
            state_.output_code_page = info->CodePage;
        }
        else
        {
            state_.input_code_page = info->CodePage;
        }
        coreio::prepare_completion(message);
        return true;
    }

    [[nodiscard]] bool get_number_of_input_events(coreio::io_msg &message) noexcept
    {
        if (message.descriptor.InputSize < sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_GETNUMBEROFINPUTEVENTS_MSG))
        {
            coreio::prepare_completion(message, coreio::error::status_invalid_parameter);
            return true;
        }
        auto *info = reinterpret_cast<CONSOLE_GETNUMBEROFINPUTEVENTS_MSG *>(
            message.body + sizeof(CONSOLE_MSG_HEADER));
        info->ReadyEvents = input_ != nullptr ? static_cast<ULONG>(input_->size()) : 0;
        coreio::prepare_completion(message, 0, sizeof(CONSOLE_GETNUMBEROFINPUTEVENTS_MSG),
                                   message.body + sizeof(CONSOLE_MSG_HEADER),
                                   sizeof(CONSOLE_GETNUMBEROFINPUTEVENTS_MSG));
        return true;
    }

    [[nodiscard]] bool set_screen_buffer_size(coreio::io_msg &message) noexcept
    {
        if (message.descriptor.InputSize < sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_SETSCREENBUFFERSIZE_MSG))
        {
            coreio::prepare_completion(message, coreio::error::status_invalid_parameter);
            return true;
        }
        const auto *request = reinterpret_cast<const CONSOLE_SETSCREENBUFFERSIZE_MSG *>(
            message.body + sizeof(CONSOLE_MSG_HEADER));
        if (request->Size.X <= 0 || request->Size.Y <= 0)
        {
            coreio::prepare_completion(message, coreio::error::status_invalid_parameter);
            return true;
        }
        state_.resize(static_cast<std::size_t>(request->Size.X),
                      static_cast<std::size_t>(request->Size.Y));
        buffer_.resize(static_cast<std::size_t>(request->Size.X),
                       static_cast<std::size_t>(request->Size.Y), state_.default_style);
        coreio::prepare_completion(message);
        return true;
    }

    [[nodiscard]] bool read_console_output_string(coreio::io_msg &message) noexcept
    {
        if (message.descriptor.InputSize < sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_READCONSOLEOUTPUTSTRING_MSG))
        {
            coreio::prepare_completion(message, coreio::error::status_invalid_parameter);
            return true;
        }
        auto *request = reinterpret_cast<CONSOLE_READCONSOLEOUTPUTSTRING_MSG *>(
            message.body + sizeof(CONSOLE_MSG_HEADER));
        if (request->ReadCoord.X < 0 || request->ReadCoord.Y < 0)
        {
            coreio::prepare_completion(message, coreio::error::status_invalid_parameter);
            return true;
        }
        const auto local = sizeof(message.body) - sizeof(CONSOLE_MSG_HEADER) -
                           sizeof(CONSOLE_READCONSOLEOUTPUTSTRING_MSG);
        if (message.descriptor.OutputSize <= sizeof(CONSOLE_READCONSOLEOUTPUTSTRING_MSG))
        {
            coreio::prepare_completion(message, coreio::error::status_invalid_parameter);
            return true;
        }
        const auto capacity = std::min<std::size_t>(
            message.descriptor.OutputSize - sizeof(CONSOLE_READCONSOLEOUTPUTSTRING_MSG), local);
        auto *data = reinterpret_cast<std::byte *>(
            message.body + sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_READCONSOLEOUTPUTSTRING_MSG));
        std::memset(data, 0, capacity);
        std::size_t written = 0;
        const auto coordinate = screen::coord{request->ReadCoord.X, request->ReadCoord.Y};
        if (request->StringType == CONSOLE_ATTRIBUTE)
        {
            const auto count = std::min<std::size_t>(request->NumRecords, capacity / sizeof(std::uint16_t));
            written = buffer_.read_attrs(coordinate,
                std::span<std::uint16_t>{reinterpret_cast<std::uint16_t *>(data), count});
        }
        else if (request->StringType == CONSOLE_REAL_UNICODE)
        {
            const auto count = std::min<std::size_t>(request->NumRecords, capacity / sizeof(char16_t));
            written = buffer_.read_chars(coordinate,
                std::span<char16_t>{reinterpret_cast<char16_t *>(data), count});
        }
        request->NumRecords = static_cast<ULONG>(written);
        const auto unit = request->StringType == CONSOLE_ATTRIBUTE ? sizeof(std::uint16_t)
                                                                   : sizeof(char16_t);
        coreio::prepare_completion(message, 0, written * unit,
                                   message.body + sizeof(CONSOLE_MSG_HEADER), written * unit);
        return true;
    }

    [[nodiscard]] bool write_console_output_string(coreio::io_msg &message) noexcept
    {
        if (message.descriptor.InputSize < sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_WRITECONSOLEOUTPUTSTRING_MSG))
        {
            coreio::prepare_completion(message, coreio::error::status_invalid_parameter);
            return true;
        }
        auto *request = reinterpret_cast<CONSOLE_WRITECONSOLEOUTPUTSTRING_MSG *>(
            message.body + sizeof(CONSOLE_MSG_HEADER));
        if (request->WriteCoord.X < 0 || request->WriteCoord.Y < 0)
        {
            coreio::prepare_completion(message, coreio::error::status_invalid_parameter);
            return true;
        }
        const auto *data = reinterpret_cast<const std::byte *>(
            message.body + sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_WRITECONSOLEOUTPUTSTRING_MSG));
        const auto data_size = message.descriptor.InputSize - sizeof(CONSOLE_MSG_HEADER) -
                               sizeof(CONSOLE_WRITECONSOLEOUTPUTSTRING_MSG);
        const auto coordinate = screen::coord{request->WriteCoord.X, request->WriteCoord.Y};
        std::size_t written = 0;
        if (request->StringType == CONSOLE_ATTRIBUTE)
        {
            const auto count = std::min<std::size_t>(request->NumRecords, data_size / sizeof(std::uint16_t));
            written = buffer_.write_attrs(coordinate,
                std::span<const std::uint16_t>{reinterpret_cast<const std::uint16_t *>(data), count});
        }
        else if (request->StringType == CONSOLE_REAL_UNICODE)
        {
            const auto count = std::min<std::size_t>(request->NumRecords, data_size / sizeof(char16_t));
            written = buffer_.write_chars(coordinate,
                std::u16string_view{reinterpret_cast<const char16_t *>(data), count});
        }
        request->NumRecords = static_cast<ULONG>(written);
        coreio::prepare_completion(message, 0, sizeof(CONSOLE_WRITECONSOLEOUTPUTSTRING_MSG),
                                   message.body + sizeof(CONSOLE_MSG_HEADER),
                                   sizeof(CONSOLE_WRITECONSOLEOUTPUTSTRING_MSG));
        return true;
    }

    [[nodiscard]] bool fill_console_output(coreio::io_msg &message) noexcept
    {
        if (message.descriptor.InputSize < sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_FILLCONSOLEOUTPUT_MSG))
        {
            coreio::prepare_completion(message, coreio::error::status_invalid_parameter);
            return true;
        }
        const auto *request = reinterpret_cast<const CONSOLE_FILLCONSOLEOUTPUT_MSG *>(
            message.body + sizeof(CONSOLE_MSG_HEADER));
        if (request->WriteCoord.X < 0 || request->WriteCoord.Y < 0)
        {
            coreio::prepare_completion(message, coreio::error::status_invalid_parameter);
            return true;
        }

        std::size_t remaining = request->Length;
        auto x = static_cast<std::size_t>(request->WriteCoord.X);
        auto y = static_cast<std::size_t>(request->WriteCoord.Y);
        const auto width = buffer_.width();
        while (remaining != 0 && y < buffer_.height() && x < width)
        {
            const auto count = std::min(remaining, width - x);
            const screen::rect area{static_cast<std::int32_t>(x), static_cast<std::int32_t>(y),
                                    static_cast<std::int32_t>(x + count - 1), static_cast<std::int32_t>(y)};
            if (request->ElementType == CONSOLE_ATTRIBUTE)
            {
                buffer_.fill_attrs(area, request->Element);
            }
            else
            {
                buffer_.fill_chars(area, static_cast<char16_t>(request->Element));
            }
            remaining -= count;
            x = 0;
            ++y;
        }

        coreio::prepare_completion(message, 0, request->Length - remaining);
        return true;
    }

    [[nodiscard]] bool read_console_output(coreio::io_msg &message) noexcept
    {
        if (message.descriptor.InputSize < sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_READCONSOLEOUTPUT_MSG))
        {
            coreio::prepare_completion(message, coreio::error::status_invalid_parameter);
            return true;
        }
        const auto *request = reinterpret_cast<const CONSOLE_READCONSOLEOUTPUT_MSG *>(
            message.body + sizeof(CONSOLE_MSG_HEADER));
        const auto &region = request->CharRegion;
        if (region.Left > region.Right || region.Top > region.Bottom)
        {
            coreio::prepare_completion(message, coreio::error::status_invalid_parameter);
            return true;
        }

        const auto local = sizeof(message.body) - sizeof(CONSOLE_MSG_HEADER) -
                           sizeof(CONSOLE_READCONSOLEOUTPUT_MSG);
        if (message.descriptor.OutputSize <= sizeof(CONSOLE_READCONSOLEOUTPUT_MSG))
        {
            coreio::prepare_completion(message, coreio::error::status_invalid_parameter);
            return true;
        }
        const auto capacity = std::min<std::size_t>(
            message.descriptor.OutputSize - sizeof(CONSOLE_READCONSOLEOUTPUT_MSG), local);
        const auto width = static_cast<std::size_t>(region.Right - region.Left + 1);
        const auto height = static_cast<std::size_t>(region.Bottom - region.Top + 1);
        const auto required = width * height * sizeof(interop::win32::char_info);
        if (required > capacity)
        {
            coreio::prepare_completion(message, coreio::error::status_invalid_parameter);
            return true;
        }

        auto *out = reinterpret_cast<interop::win32::char_info *>(
            message.body + sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_READCONSOLEOUTPUT_MSG));
        std::memset(out, 0, required);

        const auto left = std::max<std::int32_t>(region.Left, 0);
        const auto top = std::max<std::int32_t>(region.Top, 0);
        const auto right = std::min<std::int32_t>(region.Right, static_cast<std::int32_t>(buffer_.width()) - 1);
        const auto bottom = std::min<std::int32_t>(region.Bottom, static_cast<std::int32_t>(buffer_.height()) - 1);
        if (left <= right && top <= bottom)
        {
            for (auto y = top; y <= bottom; ++y)
            {
                auto *row = out + static_cast<std::size_t>(y - region.Top) * width +
                            static_cast<std::size_t>(left - region.Left);
                (void)buffer_.read_char_info_abi(
                    screen::rect{left, y, right, y},
                    std::span<interop::win32::char_info>{row, static_cast<std::size_t>(right - left + 1)});
            }
        }
        coreio::prepare_completion(message, 0, required,
                                   message.body + sizeof(CONSOLE_MSG_HEADER), required);
        return true;
    }

    [[nodiscard]] bool write_console_output(coreio::io_msg &message) noexcept
    {
        if (message.descriptor.InputSize < sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_WRITECONSOLEOUTPUT_MSG))
        {
            coreio::prepare_completion(message, coreio::error::status_invalid_parameter);
            return true;
        }
        const auto *request = reinterpret_cast<const CONSOLE_WRITECONSOLEOUTPUT_MSG *>(
            message.body + sizeof(CONSOLE_MSG_HEADER));
        const auto width = static_cast<std::size_t>(request->CharRegion.Right - request->CharRegion.Left + 1);
        const auto height = static_cast<std::size_t>(request->CharRegion.Bottom - request->CharRegion.Top + 1);
        const auto count = width * height;
        const auto required = sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_WRITECONSOLEOUTPUT_MSG) +
                              count * sizeof(interop::win32::char_info);
        if (message.descriptor.InputSize < required || request->CharRegion.Left < 0 ||
            request->CharRegion.Top < 0 || request->CharRegion.Right < request->CharRegion.Left ||
            request->CharRegion.Bottom < request->CharRegion.Top)
        {
            coreio::prepare_completion(message, coreio::error::status_invalid_parameter);
            return true;
        }

        const auto *cells = reinterpret_cast<const interop::win32::char_info *>(
            message.body + sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_WRITECONSOLEOUTPUT_MSG));
        const screen::rect area{request->CharRegion.Left, request->CharRegion.Top,
                                request->CharRegion.Right, request->CharRegion.Bottom};
        const auto written = buffer_.write_char_info_abi(area, std::span<const interop::win32::char_info>{cells, count});
        coreio::prepare_completion(message, 0, written);
        return true;
    }

    [[nodiscard]] bool set_cursor_info(coreio::io_msg &message) noexcept
    {
        if (message.descriptor.InputSize < sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_SETCURSORINFO_MSG))
        {
            coreio::prepare_completion(message, coreio::error::status_invalid_parameter);
            return true;
        }
        const auto *info = reinterpret_cast<const CONSOLE_SETCURSORINFO_MSG *>(
            message.body + sizeof(CONSOLE_MSG_HEADER));
        state_.cursor().visible = info->Visible != FALSE;
        state_.cursor().size = info->CursorSize;
        coreio::prepare_completion(message);
        return true;
    }

    [[nodiscard]] bool set_cursor_position(coreio::io_msg &message) noexcept
    {
        if (message.descriptor.InputSize < sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_SETCURSORPOSITION_MSG))
        {
            coreio::prepare_completion(message, coreio::error::status_invalid_parameter);
            return true;
        }
        const auto *position = reinterpret_cast<const CONSOLE_SETCURSORPOSITION_MSG *>(
            message.body + sizeof(CONSOLE_MSG_HEADER));
        if (position->CursorPosition.X < 0 || position->CursorPosition.Y < 0)
        {
            coreio::prepare_completion(message, coreio::error::status_invalid_parameter);
            return true;
        }
        state_.set_cursor(static_cast<std::size_t>(position->CursorPosition.X),
                          static_cast<std::size_t>(position->CursorPosition.Y));
        coreio::prepare_completion(message);
        return true;
    }

    [[nodiscard]] bool get_largest_window_size(coreio::io_msg &message) noexcept
    {
        if (message.descriptor.InputSize < sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_GETLARGESTWINDOWSIZE_MSG))
        {
            coreio::prepare_completion(message, coreio::error::status_invalid_parameter);
            return true;
        }
        auto *info = reinterpret_cast<CONSOLE_GETLARGESTWINDOWSIZE_MSG *>(
            message.body + sizeof(CONSOLE_MSG_HEADER));
        info->Size = COORD{static_cast<SHORT>(state_.max_window_size.x),
                           static_cast<SHORT>(state_.max_window_size.y)};
        coreio::prepare_completion(message, 0, sizeof(CONSOLE_GETLARGESTWINDOWSIZE_MSG),
                                   message.body + sizeof(CONSOLE_MSG_HEADER),
                                   sizeof(CONSOLE_GETLARGESTWINDOWSIZE_MSG));
        return true;
    }

    [[nodiscard]] bool get_screen_buffer_info(coreio::io_msg &message) noexcept
    {
        if (message.descriptor.InputSize < sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_SCREENBUFFERINFO_MSG))
        {
            coreio::prepare_completion(message, coreio::error::status_invalid_parameter);
            return true;
        }

        auto *info = reinterpret_cast<CONSOLE_SCREENBUFFERINFO_MSG *>(
            message.body + sizeof(CONSOLE_MSG_HEADER));
        std::memset(info, 0, sizeof(*info));
        const auto size = buffer_.buffer_size();
        const auto cursor = state_.cursor();
        const auto origin = buffer_.viewport_origin();
        const auto window = buffer_.viewport_size();
        info->Size = COORD{static_cast<SHORT>(size.x), static_cast<SHORT>(size.y)};
        info->CursorPosition = COORD{static_cast<SHORT>(cursor.x), static_cast<SHORT>(cursor.y)};
        info->ScrollPosition = COORD{static_cast<SHORT>(origin.x), static_cast<SHORT>(origin.y)};
        info->Attributes = state_.default_attributes;
        info->CurrentWindowSize = COORD{static_cast<SHORT>(window.x), static_cast<SHORT>(window.y)};
        info->MaximumWindowSize = COORD{static_cast<SHORT>(state_.max_window_size.x),
                                        static_cast<SHORT>(state_.max_window_size.y)};
        info->PopupAttributes = state_.popup_attributes;
        info->FullscreenSupported = FALSE;
        for (std::size_t i = 0; i < state_.color_table.size(); ++i)
        {
            info->ColorTable[i] = state_.color_table[i];
        }

        coreio::prepare_completion(message, 0, sizeof(CONSOLE_SCREENBUFFERINFO_MSG),
                                   message.body + sizeof(CONSOLE_MSG_HEADER),
                                   sizeof(CONSOLE_SCREENBUFFERINFO_MSG));
        return true;
    }

    [[nodiscard]] std::size_t read_cells(screen::rect area,
                                         std::span<interop::win32::char_info> output) noexcept
    {
        return buffer_.read_char_info_abi(area, output);
    }

  private:
    screen::screen_buffer &buffer_;
    screen::console_state &state_;
    consoleinput::input_queue *input_{};
};

} // namespace corehost::consoleapi