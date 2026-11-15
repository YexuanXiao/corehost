#pragma once

#include "console_state.hpp"
#include "screen_buffer.hpp"

#include "win32_types.hpp"
#include "input.hpp"
#include "process_registry.hpp"

#include "condrv_io.hpp"
#include "os/Console/conmsgl1.h"
#include "os/Console/ntcon.h"
#include "os/Console/conmsgl2.h"
#include "os/Console/conmsgl3.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <span>
#include <utility>
#include <vector>
#include <string_view>

namespace corehost::consoleapi
{

// Native console-facing operations. This layer owns cursor advancement and
// ABI projection, while libscreen owns cell storage.
class console_api final
{
  public:
    enum class pending_read_kind : std::uint8_t
    {
        none,
        console_read,
        console_input,
    };
    console_api(screen::screen_buffer &buffer, screen::console_state &state,
                consoleinput::input_queue *input = nullptr,
                std::uintptr_t input_object = 0,
                coreio::process_registry *processes = nullptr) noexcept
        : buffer_{buffer}, state_{state}, input_{input}, input_object_{input_object}, processes_{processes}
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

    void set_input_object(std::uintptr_t object) noexcept
    {
        input_object_ = object;
    }

    void set_window_handle(std::uintptr_t window) noexcept
    {
        window_handle_ = window;
    }

    void set_alternate_object(std::uintptr_t object) noexcept
    {
        alternate_object_ = object;
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
        if (header->ApiNumber == ConsolepSetScreenBufferInfo)
        {
            return set_screen_buffer_info(message);
        }
        if (header->ApiNumber == ConsolepGetMode || header->ApiNumber == ConsolepSetMode)
        {
            const bool input = input_object_ != 0 && message.descriptor.Object == input_object_;
            return header->ApiNumber == ConsolepGetMode ? get_mode(message, input) : set_mode(message, input);
        }
        if (header->ApiNumber == ConsolepSetActiveScreenBuffer)
        {
            return set_active_screen_buffer(message);
        }
        if (header->ApiNumber == ConsolepFlushInputBuffer)
        {
            return flush_input_buffer(message);
        }
        if (header->ApiNumber == ConsolepGetConsoleProcessList)
        {
            return get_process_list(message);
        }
        if (header->ApiNumber == ConsolepGetMouseInfo)
        {
            return get_mouse_info(message);
        }
        if (header->ApiNumber == ConsolepSetCurrentFont)
        {
            return set_current_font(message);
        }
        if (header->ApiNumber == ConsolepGetAliasesLength)
        {
            return get_aliases_length(message);
        }
        if (header->ApiNumber == ConsolepGetAliasExesLength)
        {
            return get_alias_exes_length(message);
        }
        if (header->ApiNumber == ConsolepGetAliasExes)
        {
            return get_alias_exes(message);
        }
        if (header->ApiNumber == ConsolepGetAliases)
        {
            return get_aliases(message);
        }
        if (header->ApiNumber == ConsolepAddAlias)
        {
            return add_alias(message);
        }
        if (header->ApiNumber == ConsolepGetAlias)
        {
            return get_alias(message);
        }
        if (header->ApiNumber == ConsolepExpungeCommandHistory)
        {
            return expunge_command_history(message);
        }
        if (header->ApiNumber == ConsolepSetNumberOfCommands)
        {
            return set_number_of_commands(message);
        }
        if (header->ApiNumber == ConsolepGetCommandHistoryLength)
        {
            return get_command_history_length(message);
        }
        if (header->ApiNumber == ConsolepGetCommandHistory)
        {
            return get_command_history(message);
        }
        if (header->ApiNumber == ConsolepGetHistory)
        {
            return get_history_info(message);
        }
        if (header->ApiNumber == ConsolepSetHistory)
        {
            return set_history_info(message);
        }
        if (header->ApiNumber == ConsolepGetConsoleWindow)
        {
            return get_console_window(message);
        }
        if (header->ApiNumber == ConsolepGetDisplayMode)
        {
            return get_display_mode(message);
        }
        if (header->ApiNumber == ConsolepSetDisplayMode)
        {
            return set_display_mode(message);
        }
        if (header->ApiNumber == ConsolepGetFontSize)
        {
            return get_font_size(message);
        }
        if (header->ApiNumber == ConsolepGetCurrentFont)
        {
            return get_current_font(message);
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
        if (header->ApiNumber == ConsolepGenerateCtrlEvent)
        {
            return generate_ctrl_event(message);
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

    [[nodiscard]] bool resume_console_input(coreio::io_msg &message) noexcept
    {
        return read_console_input(message, false);
    }

    [[nodiscard]] pending_read_kind take_pending_read_kind() noexcept
    {
        return std::exchange(pending_read_kind_, pending_read_kind::none);
    }

    [[nodiscard]] bool read_console_input(coreio::io_msg &message, bool pending_allowed = true) noexcept
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
            count = (request->Flags & CONSOLE_READ_NOREMOVE) != 0
                        ? input_->peek(std::span<consoleinput::input_event>{events.data(), requested})
                        : input_->read(std::span<consoleinput::input_event>{events.data(), requested});
            for (std::size_t i = 0; i < count; ++i)
            {
                auto &record = output[i];
                record = INPUT_RECORD{};
                if (events[i].type == consoleinput::input_event_type::mouse)
                {
                    record.EventType = MOUSE_EVENT;
                    record.Event.MouseEvent.dwMousePosition = COORD{
                        static_cast<SHORT>(events[i].mouse.x), static_cast<SHORT>(events[i].mouse.y)};
                    record.Event.MouseEvent.dwButtonState = events[i].mouse.button_state;
                    record.Event.MouseEvent.dwControlKeyState = events[i].mouse.control_state;
                    record.Event.MouseEvent.dwEventFlags = events[i].mouse.event_flags;
                }
                else
                {
                    record.EventType = KEY_EVENT;
                    record.Event.KeyEvent.bKeyDown = events[i].key.key_down ? TRUE : FALSE;
                    record.Event.KeyEvent.wRepeatCount = events[i].key.repeat_count;
                    record.Event.KeyEvent.wVirtualKeyCode =
                        static_cast<WORD>(std::to_underlying(events[i].key.key));
                    record.Event.KeyEvent.wVirtualScanCode = events[i].key.scan_code;
                    if (request->Unicode)
                    {
                        record.Event.KeyEvent.uChar.UnicodeChar = events[i].key.character;
                    }
                    else
                    {
                        const auto code_page =
                            state_.output_code_page != 0 ? state_.output_code_page : CP_ACP;
                        char narrow = 0;
                        (void)::WideCharToMultiByte(
                            code_page, 0, reinterpret_cast<const wchar_t *>(&events[i].key.character),
                            1, &narrow, 1, nullptr, nullptr);
                        record.Event.KeyEvent.uChar.AsciiChar = narrow;
                    }
                    record.Event.KeyEvent.dwControlKeyState = events[i].key.control_state;
                }
            }
        }
        if (count == 0 && pending_allowed && (request->Flags & CONSOLE_READ_NOWAIT) == 0)
        {
            pending_read_kind_ = pending_read_kind::console_input;
            return false;
        }
        pending_read_kind_ = pending_read_kind::none;
        request->NumRecords = static_cast<ULONG>(count);
        coreio::prepare_completion(message, 0, count * sizeof(INPUT_RECORD),
                                   message.body + sizeof(CONSOLE_MSG_HEADER),
                                   count * sizeof(INPUT_RECORD));
        return true;
    }

    [[nodiscard]] bool resume_read(coreio::io_msg &message) noexcept
    {
        return read_console(message, false);
    }

    [[nodiscard]] bool read_console(coreio::io_msg &message, bool pending_allowed = true) noexcept
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
        if (cooked_active_)
        {
            units = std::min<std::size_t>(cooked_line_.size(), max_units);
            std::copy_n(cooked_line_.data(), units, output);
        }
        else if (request->Unicode && request->InitialNumBytes != 0)
        {
            const auto payload_bytes = message.descriptor.InputSize - sizeof(CONSOLE_MSG_HEADER) -
                                       sizeof(CONSOLE_READCONSOLE_MSG);
            const auto exe_bytes = static_cast<std::size_t>(request->ExeNameLength) * sizeof(char16_t);
            if (exe_bytes <= payload_bytes)
            {
                const auto *initial = reinterpret_cast<const char16_t *>(
                    message.body + sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_READCONSOLE_MSG) +
                    exe_bytes);
                const auto available = (payload_bytes - exe_bytes) / sizeof(char16_t);
                const auto initial_units =
                    std::min({static_cast<std::size_t>(request->InitialNumBytes / sizeof(char16_t)),
                              available, max_units});
                std::copy_n(initial, initial_units, output);
                units = initial_units;
            }
        }

        const bool echo = (state_.input_mode & 0x0004) != 0;
        const screen::coord echo_origin = cooked_active_
                                               ? cooked_origin_
                                               : screen::coord{static_cast<std::int32_t>(state_.cursor().x),
                                                               static_cast<std::int32_t>(state_.cursor().y)};
        screen::coord echo_cursor = echo_origin;
        std::size_t cursor = cooked_active_ ? std::min(cooked_cursor_, units) : units;
        std::size_t echo_length = cooked_active_ ? cooked_echo_length_ : 0;
        if (!cooked_active_)
        {
            state_.reset_history_browse();
        }
        bool line_complete = false;

        std::array<consoleinput::input_event, 64> events{};
        const auto parsed = input_->read(std::span<consoleinput::input_event>{events.data(), events.size()});
        if (parsed == 0 && pending_allowed)
        {
            pending_read_kind_ = pending_read_kind::console_read;
            return false;
        }
        pending_read_kind_ = pending_read_kind::none;
        std::size_t consumed_events = 0;
        for (std::size_t i = 0; i < parsed; ++i)
        {
            consumed_events = i + 1;
            if (events[i].type != consoleinput::input_event_type::key || !events[i].key.key_down)
            {
                continue;
            }
            const auto character = events[i].key.character;
            if (character == 0)
            {
                if (events[i].key.key == consoleinput::virtual_key::left)
                {
                    if (cursor != 0)
                    {
                        --cursor;
                    }
                }
                else if (events[i].key.key == consoleinput::virtual_key::right)
                {
                    if (cursor < units)
                    {
                        ++cursor;
                    }
                }
                else if (events[i].key.key == consoleinput::virtual_key::home)
                {
                    cursor = 0;
                }
                else if (events[i].key.key == consoleinput::virtual_key::end)
                {
                    cursor = units;
                }
                else if (events[i].key.key == consoleinput::virtual_key::del)
                {
                    if (cursor < units)
                    {
                        erase_character(output, units, cursor, cursor);
                    }
                }
                else if (events[i].key.key == consoleinput::virtual_key::up)
                {
                    std::u16string selected;
                    if (state_.browse_history_up(std::u16string_view{output, units}, selected))
                    {
                        units = std::min<std::size_t>(selected.size(), max_units);
                        std::copy_n(selected.data(), units, output);
                        cursor = units;
                    }
                }
                else if (events[i].key.key == consoleinput::virtual_key::down)
                {
                    std::u16string selected;
                    if (state_.browse_history_down(selected))
                    {
                        units = std::min<std::size_t>(selected.size(), max_units);
                        std::copy_n(selected.data(), units, output);
                        cursor = units;
                    }
                }
                redraw_echo_line(output, units, cursor, echo, echo_origin, echo_length, echo_cursor);
                continue;
            }

            if (character == 3 && (state_.input_mode & 0x0001) != 0)
            {
                cooked_active_ = false;
                cooked_line_.clear();
                request->NumBytes = 0;
                request->ControlKeyState = 0;
                coreio::prepare_completion(message, coreio::error::status_alerted, 0,
                                           message.body + sizeof(CONSOLE_MSG_HEADER),
                                           sizeof(CONSOLE_READCONSOLE_MSG));
                return true;
            }
            if (request->ProcessControlZ != FALSE && character == 26 &&
                (state_.input_mode & 0x0001) != 0)
            {
                line_complete = true;
                break;
            }
            if (character >= 1 && character <= 26 &&
                (request->CtrlWakeupMask & (1u << (character - 1))) != 0)
            {
                if (units < max_units)
                {
                    if (cursor < units)
                    {
                        for (std::size_t j = units; j > cursor; --j)
                        {
                            output[j] = output[j - 1];
                        }
                    }
                    output[cursor++] = character;
                    ++units;
                }
                redraw_echo_line(output, units, cursor, echo, echo_origin, echo_length, echo_cursor);
                line_complete = true;
                break;
            }
            if (character == u'\u000D' || character == u'\u000A')
            {
                redraw_echo_line(output, units, cursor, echo, echo_origin, echo_length, echo_cursor);
                if (echo)
                {
                    echo_cursor.x = 0;
                    ++echo_cursor.y;
                }
                std::u16string expanded{output, units};
                expand_console_alias(expanded);
                units = std::min<std::size_t>(expanded.size(), max_units);
                std::copy_n(expanded.data(), units, output);
                state_.add_command(std::u16string_view{output, units});
                line_complete = true;
                break;
            }
            if (character == u'\u0008')
            {
                if (cursor != 0)
                {
                    erase_character(output, units, cursor, cursor - 1);
                    --cursor;
                    state_.break_history_browse();
                }
                redraw_echo_line(output, units, cursor, echo, echo_origin, echo_length, echo_cursor);
                continue;
            }
            state_.break_history_browse();
            if (units < max_units)
            {
                if (cursor < units)
                {
                    for (std::size_t j = units; j > cursor; --j)
                    {
                        output[j] = output[j - 1];
                    }
                }
                output[cursor++] = character;
                ++units;
            }
            redraw_echo_line(output, units, cursor, echo, echo_origin, echo_length, echo_cursor);
        }
        for (std::size_t i = parsed; i > consumed_events; --i)
        {
            input_->push_front(events[i - 1]);
        }
        if (!line_complete)
        {
            cooked_line_.assign(output, units);
            cooked_cursor_ = cursor;
            cooked_origin_ = echo_origin;
            cooked_echo_length_ = echo_length;
            cooked_active_ = true;
            pending_read_kind_ = pending_read_kind::console_read;
            if (echo)
            {
                state_.set_cursor(static_cast<std::size_t>(echo_cursor.x),
                                  static_cast<std::size_t>(echo_cursor.y));
            }
            return false;
        }
        if (units + 2 <= max_units)
        {
            output[units++] = u'\r';
            output[units++] = u'\n';
        }
        cooked_active_ = false;
        cooked_line_.clear();

        if (echo)
        {
            state_.set_cursor(static_cast<std::size_t>(echo_cursor.x),
                              static_cast<std::size_t>(echo_cursor.y));
        }
        request->ControlKeyState = 0;
        std::size_t bytes = 0;
        if (request->Unicode)
        {
            bytes = units * sizeof(char16_t);
            request->NumBytes = static_cast<ULONG>(bytes);
        }
        else
        {
            const auto code_page = state_.input_code_page != 0 ? state_.input_code_page : CP_ACP;
            const auto capacity = max_units * sizeof(char16_t);
            std::vector<char> ansi(capacity);
            const auto converted = ::WideCharToMultiByte(
                code_page, 0, reinterpret_cast<const wchar_t *>(output), static_cast<int>(units),
                ansi.data(), static_cast<int>(ansi.size()), nullptr, nullptr);
            if (converted == 0 && units != 0)
            {
                coreio::prepare_completion(message, coreio::error::status_invalid_parameter);
                return true;
            }
            bytes = static_cast<std::size_t>(converted);
            std::memcpy(output, ansi.data(), bytes);
            request->NumBytes = static_cast<ULONG>(bytes);
        }
        coreio::prepare_completion(message, 0, bytes,
                                   message.body + sizeof(CONSOLE_MSG_HEADER),
                                   sizeof(CONSOLE_READCONSOLE_MSG) + bytes);
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
        const auto dest_left = static_cast<std::size_t>(std::max<std::int32_t>(destination.x, 0));
        const auto dest_top = static_cast<std::size_t>(std::max<std::int32_t>(destination.y, 0));
        const auto dest_right = dest_left + source_rect.width;
        const auto dest_bottom = dest_top + source_rect.height;
        for (std::size_t y = source_rect.y; y < source_rect.y + source_rect.height; ++y)
        {
            if (y < dest_top || y >= dest_bottom)
            {
                fill_rect_segment(source_rect.x, y, source_rect.width, fill_char, fill_attributes);
                continue;
            }
            if (source_rect.x < dest_left)
            {
                fill_rect_segment(source_rect.x, y, std::min(dest_left, source_rect.x + source_rect.width) - source_rect.x, fill_char, fill_attributes);
            }
            const auto right_start = std::max(dest_right, source_rect.x);
            if (right_start < source_rect.x + source_rect.width)
            {
                fill_rect_segment(right_start, y, source_rect.x + source_rect.width - right_start, fill_char, fill_attributes);
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
            written = write_console_text(std::u16string_view{data, count});
        }
        else
        {
            const auto code_page = state_.input_code_page != 0 ? state_.input_code_page : CP_ACP;
            const auto *bytes_data = reinterpret_cast<const char *>(data);
            const auto units = ::MultiByteToWideChar(code_page, 0, bytes_data, static_cast<int>(bytes),
                                                     nullptr, 0);
            std::u16string text(static_cast<std::size_t>(std::max(units, 0)), u'\0');
            if (units > 0)
            {
                (void)::MultiByteToWideChar(code_page, 0, bytes_data, static_cast<int>(bytes),
                                            reinterpret_cast<LPWSTR>(text.data()), units);
            }
            written = write_console_text(text);
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
        std::vector<consoleinput::input_event> prepend;
        if (input_ != nullptr)
        {
            for (std::size_t i = 0; i < records; ++i)
            {
                consoleinput::input_event converted{};
                if (input[i].EventType == MOUSE_EVENT)
                {
                    converted.type = consoleinput::input_event_type::mouse;
                    converted.mouse.x = input[i].Event.MouseEvent.dwMousePosition.X;
                    converted.mouse.y = input[i].Event.MouseEvent.dwMousePosition.Y;
                    converted.mouse.button_state = input[i].Event.MouseEvent.dwButtonState;
                    converted.mouse.control_state = input[i].Event.MouseEvent.dwControlKeyState;
                    converted.mouse.event_flags = input[i].Event.MouseEvent.dwEventFlags;
                }
                else if (input[i].EventType == KEY_EVENT)
                {
                    consoleinput::key_event key;
                    if (request->Unicode)
                    {
                        key.character = input[i].Event.KeyEvent.uChar.UnicodeChar;
                    }
                    else
                    {
                        const auto code_page =
                            state_.input_code_page != 0 ? state_.input_code_page : CP_ACP;
                        const auto ascii = input[i].Event.KeyEvent.uChar.AsciiChar;
                        wchar_t wide = 0;
                        (void)::MultiByteToWideChar(code_page, 0, &ascii, 1, &wide, 1);
                        key.character = static_cast<char16_t>(wide);
                    }
                    key.key = static_cast<consoleinput::virtual_key>(
                        input[i].Event.KeyEvent.wVirtualKeyCode);
                    key.scan_code = input[i].Event.KeyEvent.wVirtualScanCode;
                    key.control_state = input[i].Event.KeyEvent.dwControlKeyState;
                    key.repeat_count = input[i].Event.KeyEvent.wRepeatCount;
                    key.key_down = input[i].Event.KeyEvent.bKeyDown != FALSE;
                    converted.type = consoleinput::input_event_type::key;
                    converted.key = key;
                }
                else
                {
                    continue;
                }
                if (request->Append)
                {
                    input_->push(converted);
                }
                else
                {
                    prepend.push_back(converted);
                }
                ++written;
            }
            for (auto iterator = prepend.rbegin(); iterator != prepend.rend(); ++iterator)
            {
                input_->push_front(*iterator);
            }
        }
        request->NumRecords = static_cast<ULONG>(written);
        coreio::prepare_completion(message, 0, sizeof(CONSOLE_WRITECONSOLEINPUT_MSG),
                                   message.body + sizeof(CONSOLE_MSG_HEADER),
                                   sizeof(CONSOLE_WRITECONSOLEINPUT_MSG));
        return true;
    }

    [[nodiscard]] bool set_screen_buffer_info(coreio::io_msg &message) noexcept
    {
        if (message.descriptor.InputSize < sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_SCREENBUFFERINFO_MSG))
        {
            coreio::prepare_completion(message, coreio::error::status_invalid_parameter);
            return true;
        }
        const auto *request = reinterpret_cast<const CONSOLE_SCREENBUFFERINFO_MSG *>(
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
        state_.set_cursor(static_cast<std::size_t>(request->CursorPosition.X),
                          static_cast<std::size_t>(request->CursorPosition.Y));
        state_.default_attributes = request->Attributes;
        state_.default_style = buffer_.style_for_attributes(request->Attributes);
        state_.max_window_size = screen::coord{request->MaximumWindowSize.X,
                                               request->MaximumWindowSize.Y};
        state_.popup_attributes = request->PopupAttributes;
        for (std::size_t i = 0; i < state_.color_table.size(); ++i)
        {
            state_.color_table[i] = request->ColorTable[i];
        }
        const auto window_width = std::max<std::int32_t>(request->CurrentWindowSize.X, 1);
        const auto window_height = std::max<std::int32_t>(request->CurrentWindowSize.Y, 1);
        buffer_.set_viewport(screen::rect{request->ScrollPosition.X, request->ScrollPosition.Y,
                                          request->ScrollPosition.X + window_width - 1,
                                          request->ScrollPosition.Y + window_height - 1});
        coreio::prepare_completion(message);
        return true;
    }

    [[nodiscard]] bool get_mode(coreio::io_msg &message, bool input) noexcept
    {
        if (message.descriptor.InputSize < sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_MODE_MSG))
        {
            coreio::prepare_completion(message, coreio::error::status_invalid_parameter);
            return true;
        }
        auto *info = reinterpret_cast<CONSOLE_MODE_MSG *>(message.body + sizeof(CONSOLE_MSG_HEADER));
        info->Mode = input ? (state_.input_mode | 0x0080 | state_.input_private_mode)
                            : state_.output_mode;
        coreio::prepare_completion(message, 0, sizeof(CONSOLE_MODE_MSG),
                                   message.body + sizeof(CONSOLE_MSG_HEADER), sizeof(CONSOLE_MODE_MSG));
        return true;
    }

    [[nodiscard]] bool set_mode(coreio::io_msg &message, bool input) noexcept
    {
        if (message.descriptor.InputSize < sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_MODE_MSG))
        {
            coreio::prepare_completion(message, coreio::error::status_invalid_parameter);
            return true;
        }
        const auto *info = reinterpret_cast<const CONSOLE_MODE_MSG *>(message.body + sizeof(CONSOLE_MSG_HEADER));
        constexpr std::uint32_t valid_input = 0x0001 | 0x0002 | 0x0004 | 0x0008 | 0x0010 | 0x0200;
        constexpr std::uint32_t private_input = 0x0020 | 0x0040 | 0x0080 | 0x0100;
        constexpr std::uint32_t valid_output = 0x0001 | 0x0002 | 0x0004 | 0x0008 | 0x0010;
        if (input)
        {
            const auto requested = info->Mode;
            state_.input_mode = requested & ~private_input;
            if ((requested & 0x0080) != 0)
            {
                state_.input_private_mode = requested & private_input;
            }
            else
            {
                state_.input_private_mode |= requested & private_input;
            }
            if ((requested & ~(valid_input | private_input)) != 0)
            {
                coreio::prepare_completion(message, coreio::error::status_invalid_parameter);
                return true;
            }
        }
        else
        {
            if ((info->Mode & ~valid_output) != 0)
            {
                coreio::prepare_completion(message, coreio::error::status_invalid_parameter);
                return true;
            }
            state_.output_mode = info->Mode;
        }
        coreio::prepare_completion(message);
        return true;
    }

    [[nodiscard]] bool set_active_screen_buffer(coreio::io_msg &message) noexcept
    {
        const bool alternate = alternate_object_ != 0 &&
                               message.descriptor.Object == alternate_object_;
        buffer_.set_alternate(alternate);
        coreio::prepare_completion(message);
        return true;
    }

    [[nodiscard]] bool flush_input_buffer(coreio::io_msg &message) noexcept
    {
        if (input_ != nullptr)
        {
            input_->clear();
        }
        cancel_pending_ = true;
        coreio::prepare_completion(message);
        return true;
    }

    void take_ctrl_events(std::vector<std::pair<DWORD, DWORD>> &output)
    {
        output.swap(ctrl_events_);
    }

    [[nodiscard]] bool take_cancel_pending() noexcept
    {
        return std::exchange(cancel_pending_, false);
    }

    [[nodiscard]] bool get_process_list(coreio::io_msg &message) noexcept
    {
        if (message.descriptor.InputSize < sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_GETCONSOLEPROCESSLIST_MSG))
        {
            coreio::prepare_completion(message, coreio::error::status_invalid_parameter);
            return true;
        }
        auto *info = reinterpret_cast<CONSOLE_GETCONSOLEPROCESSLIST_MSG *>(message.body + sizeof(CONSOLE_MSG_HEADER));
        const auto local = sizeof(message.body) - sizeof(CONSOLE_MSG_HEADER) - sizeof(CONSOLE_GETCONSOLEPROCESSLIST_MSG);
        const auto capacity = message.descriptor.OutputSize <= sizeof(CONSOLE_GETCONSOLEPROCESSLIST_MSG)
                                  ? 0
                                  : std::min<std::size_t>(message.descriptor.OutputSize - sizeof(CONSOLE_GETCONSOLEPROCESSLIST_MSG), local) / sizeof(std::uint32_t);
        const auto count = processes_ != nullptr
                               ? std::min<std::size_t>(processes_->size(), capacity)
                               : 0;
        info->dwProcessCount = static_cast<ULONG>(count);
        auto *output = reinterpret_cast<std::uint32_t *>(
            message.body + sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_GETCONSOLEPROCESSLIST_MSG));
        for (std::size_t i = 0; i < count; ++i)
        {
            output[i] = static_cast<std::uint32_t>(
                processes_->processes()[processes_->size() - 1 - i]);
        }
        coreio::prepare_completion(message, 0, count * sizeof(std::uint32_t),
                                   message.body + sizeof(CONSOLE_MSG_HEADER),
                                   count * sizeof(std::uint32_t));
        return true;
    }

    [[nodiscard]] bool get_mouse_info(coreio::io_msg &message) noexcept
    {
        if (message.descriptor.InputSize < sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_GETMOUSEINFO_MSG))
        {
            coreio::prepare_completion(message, coreio::error::status_invalid_parameter);
            return true;
        }
        auto *info = reinterpret_cast<CONSOLE_GETMOUSEINFO_MSG *>(message.body + sizeof(CONSOLE_MSG_HEADER));
        info->NumButtons = 5;
        coreio::prepare_completion(message, 0, sizeof(CONSOLE_GETMOUSEINFO_MSG),
                                   message.body + sizeof(CONSOLE_MSG_HEADER),
                                   sizeof(CONSOLE_GETMOUSEINFO_MSG));
        return true;
    }

    [[nodiscard]] bool set_current_font(coreio::io_msg &message) noexcept
    {
        if (message.descriptor.InputSize < sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_CURRENTFONT_MSG))
        {
            coreio::prepare_completion(message, coreio::error::status_invalid_parameter);
            return true;
        }
        const auto *request = reinterpret_cast<const CONSOLE_CURRENTFONT_MSG *>(
            message.body + sizeof(CONSOLE_MSG_HEADER));
        state_.font_index = request->FontIndex;
        state_.font_size = screen::coord{request->FontSize.X, request->FontSize.Y};
        state_.font_family = request->FontFamily;
        state_.font_weight = request->FontWeight;
        state_.face_name.clear();
        for (const wchar_t ch : request->FaceName)
        {
            if (ch == 0)
            {
                break;
            }
            state_.face_name.push_back(static_cast<char16_t>(ch));
        }
        coreio::prepare_completion(message);
        return true;
    }

    [[nodiscard]] static std::u16string alias_exe_from_message(const coreio::io_msg &message,
                                                              std::size_t descriptor_size,
                                                              bool unicode)
    {
        const auto offset = sizeof(CONSOLE_MSG_HEADER) + descriptor_size;
        if (offset >= message.descriptor.InputSize)
        {
            return {};
        }
        const auto bytes = message.descriptor.InputSize - offset;
        std::u16string exe;
        if (unicode)
        {
            const auto *chars = reinterpret_cast<const char16_t *>(message.body + offset);
            exe.assign(chars, bytes / sizeof(char16_t));
        }
        else
        {
            for (std::size_t i = 0; i < bytes; ++i)
            {
                exe.push_back(static_cast<unsigned char>(message.body[offset + i]));
            }
        }
        return exe;
    }
    [[nodiscard]] bool get_aliases_length(coreio::io_msg &message) noexcept
    {
        if (message.descriptor.InputSize < sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_GETALIASESLENGTH_MSG))
        {
            coreio::prepare_completion(message, coreio::error::status_invalid_parameter);
            return true;
        }
        auto *info = reinterpret_cast<CONSOLE_GETALIASESLENGTH_MSG *>(message.body + sizeof(CONSOLE_MSG_HEADER));
        const auto unit = info->Unicode ? sizeof(char16_t) : sizeof(char);
        const auto exe = alias_exe_from_message(message, sizeof(CONSOLE_GETALIASESLENGTH_MSG),
                                                info->Unicode != FALSE);
        std::size_t length = 0;
        for (const auto &alias : state_.aliases)
        {
            if (!exe.empty() && alias.executable != exe)
            {
                continue;
            }
            length += (alias.source.size() + alias.target.size() + 2) * unit;
        }
        info->AliasesLength = static_cast<ULONG>(length);
        coreio::prepare_completion(message, 0, sizeof(CONSOLE_GETALIASESLENGTH_MSG),
                                   message.body + sizeof(CONSOLE_MSG_HEADER),
                                   sizeof(CONSOLE_GETALIASESLENGTH_MSG));
        return true;
    }

    [[nodiscard]] bool get_alias_exes_length(coreio::io_msg &message) noexcept
    {
        if (message.descriptor.InputSize < sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_GETALIASEXESLENGTH_MSG))
        {
            coreio::prepare_completion(message, coreio::error::status_invalid_parameter);
            return true;
        }
        auto *info = reinterpret_cast<CONSOLE_GETALIASEXESLENGTH_MSG *>(
            message.body + sizeof(CONSOLE_MSG_HEADER));
        const auto unit = info->Unicode ? sizeof(char16_t) : sizeof(char);
        std::size_t length = 0;
        for (const auto &alias : state_.aliases)
        {
            length += (alias.executable.size() + 1) * unit;
        }
        info->AliasExesLength = static_cast<ULONG>(length);
        coreio::prepare_completion(message, 0, sizeof(CONSOLE_GETALIASEXESLENGTH_MSG),
                                   message.body + sizeof(CONSOLE_MSG_HEADER),
                                   sizeof(CONSOLE_GETALIASEXESLENGTH_MSG));
        return true;
    }

    [[nodiscard]] bool get_aliases(coreio::io_msg &message) noexcept
    {
        if (message.descriptor.InputSize < sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_GETALIASES_MSG))
        {
            coreio::prepare_completion(message, coreio::error::status_invalid_parameter);
            return true;
        }
        auto *info = reinterpret_cast<CONSOLE_GETALIASES_MSG *>(message.body + sizeof(CONSOLE_MSG_HEADER));
        auto *output = message.body + sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_GETALIASES_MSG);
        const auto capacity = message.descriptor.OutputSize > sizeof(CONSOLE_GETALIASES_MSG)
                                  ? message.descriptor.OutputSize - sizeof(CONSOLE_GETALIASES_MSG)
                                  : 0;
        const auto unit = info->Unicode ? sizeof(char16_t) : sizeof(char);
        const auto exe = alias_exe_from_message(message, sizeof(CONSOLE_GETALIASES_MSG),
                                                info->Unicode != FALSE);
        std::size_t written = 0;
        for (const auto &alias : state_.aliases)
        {
            if (!exe.empty() && alias.executable != exe)
            {
                continue;
            }
            const auto bytes = (alias.source.size() + alias.target.size() + 2) * unit;
            if (written + bytes > capacity)
            {
                break;
            }
            append_alias_value(reinterpret_cast<char *>(output), written, alias.source, info->Unicode != FALSE);
            if (info->Unicode)
            {
                *reinterpret_cast<char16_t *>(output + written - sizeof(char16_t)) = u'=';
            }
            else
            {
                output[written - 1] = '=';
            }
            append_alias_value(reinterpret_cast<char *>(output), written, alias.target, info->Unicode != FALSE);
        }
        info->AliasesBufferLength = static_cast<ULONG>(written);
        coreio::prepare_completion(message, 0, written, message.body + sizeof(CONSOLE_MSG_HEADER), written);
        return true;
    }

    [[nodiscard]] bool get_alias_exes(coreio::io_msg &message) noexcept
    {
        if (message.descriptor.InputSize < sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_GETALIASEXES_MSG))
        {
            coreio::prepare_completion(message, coreio::error::status_invalid_parameter);
            return true;
        }
        auto *info = reinterpret_cast<CONSOLE_GETALIASEXES_MSG *>(message.body + sizeof(CONSOLE_MSG_HEADER));
        auto *output = message.body + sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_GETALIASEXES_MSG);
        const auto capacity = message.descriptor.OutputSize > sizeof(CONSOLE_GETALIASEXES_MSG)
                                  ? message.descriptor.OutputSize - sizeof(CONSOLE_GETALIASEXES_MSG)
                                  : 0;
        const auto unit = info->Unicode ? sizeof(char16_t) : sizeof(char);
        std::size_t written = 0;
        std::vector<std::u16string_view> seen;
        for (const auto &alias : state_.aliases)
        {
            if (std::ranges::find(seen, alias.executable) != seen.end())
            {
                continue;
            }
            seen.push_back(alias.executable);
            const auto bytes = (alias.executable.size() + 1) * unit;
            if (written + bytes > capacity)
            {
                break;
            }
            if (info->Unicode)
            {
                std::memcpy(output + written, alias.executable.data(),
                            alias.executable.size() * sizeof(char16_t));
                written += alias.executable.size() * sizeof(char16_t);
                output[written] = 0;
                written += sizeof(char16_t);
            }
            else
            {
                for (const char16_t ch : alias.executable)
                {
                    output[written++] = static_cast<char>(ch < 0x80 ? ch : '?');
                }
                output[written++] = 0;
            }
        }
        info->AliasExesBufferLength = static_cast<ULONG>(written);
        coreio::prepare_completion(message, 0, written, message.body + sizeof(CONSOLE_MSG_HEADER), written);
        return true;
    }

    [[nodiscard]] bool add_alias(coreio::io_msg &message)
    {
        if (message.descriptor.InputSize < sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_ADDALIAS_MSG))
        {
            coreio::prepare_completion(message, coreio::error::status_invalid_parameter);
            return true;
        }
        auto *request = reinterpret_cast<CONSOLE_ADDALIAS_MSG *>(message.body + sizeof(CONSOLE_MSG_HEADER));
        const auto *data = reinterpret_cast<const char *>(request + 1);
        const auto unit = request->Unicode ? sizeof(char16_t) : sizeof(char);
        const auto exe_end = static_cast<std::size_t>(request->ExeLength) * unit;
        const auto source_end = exe_end + static_cast<std::size_t>(request->SourceLength) * unit;
        const auto target_end = source_end + static_cast<std::size_t>(request->TargetLength) * unit;
        if (exe_end > message.descriptor.InputSize - sizeof(CONSOLE_MSG_HEADER) - sizeof(CONSOLE_ADDALIAS_MSG))
        {
            coreio::prepare_completion(message, coreio::error::status_invalid_parameter);
            return true;
        }

        auto entry = screen::console_state::alias_entry{
            decode_console_text(reinterpret_cast<const char *>(data), exe_end, source_end - exe_end, request->Unicode != FALSE), decode_console_text(reinterpret_cast<const char *>(data), source_end, target_end - source_end, request->Unicode != FALSE),
            decode_console_text(reinterpret_cast<const char *>(data), 0, exe_end, request->Unicode != FALSE)};
        if (entry.source.empty())
        {
            coreio::prepare_completion(message, coreio::error::status_invalid_parameter);
            return true;
        }
        if (entry.target.empty())
        {
            for (auto iterator = state_.aliases.begin(); iterator != state_.aliases.end();)
            {
                if (iterator->source == entry.source && iterator->executable == entry.executable)
                {
                    iterator = state_.aliases.erase(iterator);
                }
                else
                {
                    ++iterator;
                }
            }
            coreio::prepare_completion(message);
            return true;
        }
        auto existing = state_.aliases.begin();
        while (existing != state_.aliases.end() &&
               !(existing->source == entry.source && existing->executable == entry.executable))
        {
            ++existing;
        }
        if (existing != state_.aliases.end())
        {
            existing->target = std::move(entry.target);
        }
        else
        {
            state_.aliases.push_back(std::move(entry));
        }
        coreio::prepare_completion(message);
        return true;
    }

    [[nodiscard]] bool get_alias(coreio::io_msg &message)
    {
        if (message.descriptor.InputSize < sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_GETALIAS_MSG))
        {
            coreio::prepare_completion(message, coreio::error::status_invalid_parameter);
            return true;
        }
        auto *request = reinterpret_cast<CONSOLE_GETALIAS_MSG *>(message.body + sizeof(CONSOLE_MSG_HEADER));
        const auto *data = reinterpret_cast<const char *>(request + 1);
        const auto unit = request->Unicode ? sizeof(char16_t) : sizeof(char);
        const auto source_bytes = static_cast<std::size_t>(request->SourceLength) * unit;
        const auto exe_bytes = static_cast<std::size_t>(request->ExeLength) * unit;
        if (source_bytes + exe_bytes >
            message.descriptor.InputSize - sizeof(CONSOLE_MSG_HEADER) - sizeof(CONSOLE_GETALIAS_MSG))
        {
            coreio::prepare_completion(message, coreio::error::status_invalid_parameter);
            return true;
        }

        const auto executable = decode_console_text(reinterpret_cast<const char *>(data), 0, exe_bytes, request->Unicode != FALSE);
        const auto source = decode_console_text(reinterpret_cast<const char *>(data), exe_bytes, source_bytes, request->Unicode != FALSE);
        for (const auto &alias : state_.aliases)
        {
            if (alias.source == source && alias.executable == executable)
            {
                const auto bytes = (alias.target.size() + 1) * unit;
                auto *output = reinterpret_cast<char *>(request + 1);
                if (request->Unicode)
                {
                    std::memcpy(output, alias.target.data(), alias.target.size() * sizeof(char16_t));
                    *reinterpret_cast<char16_t *>(output + alias.target.size() * sizeof(char16_t)) = 0;
                }
                else
                {
                    for (std::size_t i = 0; i < alias.target.size(); ++i)
                    {
                        output[i] = static_cast<char>(alias.target[i] < 0x80 ? alias.target[i] : '?');
                    }
                    output[alias.target.size()] = 0;
                }
                request->TargetLength = static_cast<USHORT>(bytes);
                coreio::prepare_completion(message, 0, bytes, message.body + sizeof(CONSOLE_MSG_HEADER),
                                           bytes);
                return true;
            }
        }
        coreio::prepare_completion(message, coreio::error::status_invalid_parameter);
        return true;
    }
    [[nodiscard]] bool expunge_command_history(coreio::io_msg &message) noexcept
    {
        if (message.descriptor.InputSize < sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_EXPUNGECOMMANDHISTORY_MSG))
        {
            coreio::prepare_completion(message, coreio::error::status_invalid_parameter);
            return true;
        }
        state_.expunge_commands();
        coreio::prepare_completion(message);
        return true;
    }

    [[nodiscard]] bool set_number_of_commands(coreio::io_msg &message) noexcept
    {
        if (message.descriptor.InputSize < sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_SETNUMBEROFCOMMANDS_MSG))
        {
            coreio::prepare_completion(message, coreio::error::status_invalid_parameter);
            return true;
        }
        const auto *info = reinterpret_cast<const CONSOLE_SETNUMBEROFCOMMANDS_MSG *>(
            message.body + sizeof(CONSOLE_MSG_HEADER));
        state_.set_command_limit(info->NumCommands);
        coreio::prepare_completion(message);
        return true;
    }

    [[nodiscard]] bool get_command_history_length(coreio::io_msg &message) noexcept
    {
        if (message.descriptor.InputSize < sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_GETCOMMANDHISTORYLENGTH_MSG))
        {
            coreio::prepare_completion(message, coreio::error::status_invalid_parameter);
            return true;
        }
        auto *info = reinterpret_cast<CONSOLE_GETCOMMANDHISTORYLENGTH_MSG *>(
            message.body + sizeof(CONSOLE_MSG_HEADER));
        const auto units = state_.command_history_length();
        info->CommandHistoryLength = static_cast<ULONG>(info->Unicode ? units * sizeof(char16_t) : units);
        coreio::prepare_completion(message, 0, sizeof(CONSOLE_GETCOMMANDHISTORYLENGTH_MSG),
                                   message.body + sizeof(CONSOLE_MSG_HEADER),
                                   sizeof(CONSOLE_GETCOMMANDHISTORYLENGTH_MSG));
        return true;
    }

    [[nodiscard]] bool get_command_history(coreio::io_msg &message) noexcept
    {
        if (message.descriptor.InputSize < sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_GETCOMMANDHISTORY_MSG))
        {
            coreio::prepare_completion(message, coreio::error::status_invalid_parameter);
            return true;
        }
        auto *info = reinterpret_cast<CONSOLE_GETCOMMANDHISTORY_MSG *>(message.body + sizeof(CONSOLE_MSG_HEADER));
        auto *output = message.body + sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_GETCOMMANDHISTORY_MSG);
        const auto capacity = message.descriptor.OutputSize > sizeof(CONSOLE_GETCOMMANDHISTORY_MSG)
                                  ? message.descriptor.OutputSize - sizeof(CONSOLE_GETCOMMANDHISTORY_MSG)
                                  : 0;
        std::size_t written = 0;
        for (const auto &command : state_.command_history)
        {
            const auto unit_size = info->Unicode ? sizeof(char16_t) : sizeof(char);
            if (written + (command.size() + 1) * unit_size > capacity)
            {
                break;
            }
            if (info->Unicode)
            {
                std::memcpy(output + written, command.data(), command.size() * sizeof(char16_t));
                written += command.size() * sizeof(char16_t);
                output[written] = 0;
                written += sizeof(char16_t);
            }
            else
            {
                for (const char16_t ch : command)
                {
                    output[written++] = static_cast<char>(ch < 0x80 ? ch : '?');
                }
                output[written++] = 0;
            }
        }
        info->CommandBufferLength = static_cast<ULONG>(written);
        coreio::prepare_completion(message, 0, written, message.body + sizeof(CONSOLE_MSG_HEADER), written);
        return true;
    }

    [[nodiscard]] bool get_history_info(coreio::io_msg &message) noexcept
    {
        if (message.descriptor.InputSize < sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_HISTORY_MSG))
        {
            coreio::prepare_completion(message, coreio::error::status_invalid_parameter);
            return true;
        }
        auto *info = reinterpret_cast<CONSOLE_HISTORY_MSG *>(message.body + sizeof(CONSOLE_MSG_HEADER));
        info->HistoryBufferSize = state_.history_buffer_size;
        info->NumberOfHistoryBuffers = state_.history_buffers;
        info->dwFlags = state_.history_flags;
        coreio::prepare_completion(message, 0, sizeof(CONSOLE_HISTORY_MSG),
                                   message.body + sizeof(CONSOLE_MSG_HEADER),
                                   sizeof(CONSOLE_HISTORY_MSG));
        return true;
    }

    [[nodiscard]] bool set_history_info(coreio::io_msg &message) noexcept
    {
        if (message.descriptor.InputSize < sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_HISTORY_MSG))
        {
            coreio::prepare_completion(message, coreio::error::status_invalid_parameter);
            return true;
        }
        const auto *info = reinterpret_cast<const CONSOLE_HISTORY_MSG *>(
            message.body + sizeof(CONSOLE_MSG_HEADER));
        state_.history_buffer_size = info->HistoryBufferSize;
        state_.history_buffers = info->NumberOfHistoryBuffers;
        state_.history_flags = info->dwFlags;
        coreio::prepare_completion(message);
        return true;
    }

    [[nodiscard]] bool get_console_window(coreio::io_msg &message) noexcept
    {
        if (message.descriptor.InputSize < sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_GETCONSOLEWINDOW_MSG64))
        {
            coreio::prepare_completion(message, coreio::error::status_invalid_parameter);
            return true;
        }
        auto *info = reinterpret_cast<CONSOLE_GETCONSOLEWINDOW_MSG64 *>(
            message.body + sizeof(CONSOLE_MSG_HEADER));
        info->hwnd = reinterpret_cast<HWND>(window_handle_);
        coreio::prepare_completion(message, 0, sizeof(CONSOLE_GETCONSOLEWINDOW_MSG64),
                                   message.body + sizeof(CONSOLE_MSG_HEADER),
                                   sizeof(CONSOLE_GETCONSOLEWINDOW_MSG64));
        return true;
    }

    [[nodiscard]] bool get_display_mode(coreio::io_msg &message) noexcept
    {
        if (message.descriptor.InputSize < sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_GETDISPLAYMODE_MSG))
        {
            coreio::prepare_completion(message, coreio::error::status_invalid_parameter);
            return true;
        }
        auto *info = reinterpret_cast<CONSOLE_GETDISPLAYMODE_MSG *>(message.body + sizeof(CONSOLE_MSG_HEADER));
        info->ModeFlags = state_.display_mode;
        coreio::prepare_completion(message, 0, sizeof(CONSOLE_GETDISPLAYMODE_MSG),
                                   message.body + sizeof(CONSOLE_MSG_HEADER),
                                   sizeof(CONSOLE_GETDISPLAYMODE_MSG));
        return true;
    }

    [[nodiscard]] bool set_display_mode(coreio::io_msg &message) noexcept
    {
        if (message.descriptor.InputSize < sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_SETDISPLAYMODE_MSG))
        {
            coreio::prepare_completion(message, coreio::error::status_invalid_parameter);
            return true;
        }
        auto *info = reinterpret_cast<CONSOLE_SETDISPLAYMODE_MSG *>(message.body + sizeof(CONSOLE_MSG_HEADER));
        state_.display_mode = info->dwFlags;
        info->ScreenBufferDimensions = COORD{static_cast<SHORT>(buffer_.width()),
                                             static_cast<SHORT>(buffer_.height())};
        coreio::prepare_completion(message, 0, sizeof(CONSOLE_SETDISPLAYMODE_MSG),
                                   message.body + sizeof(CONSOLE_MSG_HEADER),
                                   sizeof(CONSOLE_SETDISPLAYMODE_MSG));
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

    [[nodiscard]] bool get_current_font(coreio::io_msg &message) noexcept
    {
        if (message.descriptor.InputSize < sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_CURRENTFONT_MSG))
        {
            coreio::prepare_completion(message, coreio::error::status_invalid_parameter);
            return true;
        }
        auto *info = reinterpret_cast<CONSOLE_CURRENTFONT_MSG *>(message.body + sizeof(CONSOLE_MSG_HEADER));
        info->FontIndex = state_.font_index;
        info->FontSize = COORD{static_cast<SHORT>(state_.font_size.x),
                              static_cast<SHORT>(state_.font_size.y)};
        info->FontFamily = state_.font_family;
        info->FontWeight = state_.font_weight;
        const auto units = std::min<std::size_t>(state_.face_name.size(), LF_FACESIZE - 1);
        std::memcpy(info->FaceName, state_.face_name.data(), units * sizeof(char16_t));
        info->FaceName[units] = 0;
        coreio::prepare_completion(message, 0, sizeof(CONSOLE_CURRENTFONT_MSG),
                                   message.body + sizeof(CONSOLE_MSG_HEADER),
                                   sizeof(CONSOLE_CURRENTFONT_MSG));
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
        auto *output = message.body + sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_GETTITLE_MSG);
        std::size_t written = 0;
        if (request->Unicode)
        {
            const auto units = std::min<std::size_t>(title.size(), capacity / sizeof(char16_t));
            std::memcpy(output, title.data(), units * sizeof(char16_t));
            written = units * sizeof(char16_t);
            request->TitleLength = static_cast<ULONG>(units);
        }
        else
        {
            const auto units = std::min<std::size_t>(title.size(), capacity);
            for (std::size_t i = 0; i < units; ++i)
            {
                output[i] = static_cast<char>(title[i] < 0x80 ? title[i] : '?');
            }
            written = units;
            request->TitleLength = static_cast<ULONG>(units);
        }
        coreio::prepare_completion(message, 0, written,
                                   message.body + sizeof(CONSOLE_MSG_HEADER), written);
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
        const auto *data = message.body + sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_SETTITLE_MSG);
        const auto *request =
            reinterpret_cast<const CONSOLE_SETTITLE_MSG *>(message.body + sizeof(CONSOLE_MSG_HEADER));
        state_.title.clear();
        if (request->Unicode)
        {
            const auto *chars = reinterpret_cast<const char16_t *>(data);
            state_.title.assign(chars, bytes / sizeof(char16_t));
        }
        else
        {
            for (std::size_t i = 0; i < bytes; ++i)
            {
                state_.title.push_back(static_cast<unsigned char>(data[i]));
            }
        }
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

    [[nodiscard]] bool generate_ctrl_event(coreio::io_msg &message) noexcept
    {
        if (message.descriptor.InputSize < sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_CTRLEVENT_MSG))
        {
            coreio::prepare_completion(message, coreio::error::status_invalid_parameter);
            return true;
        }
        const auto *request = reinterpret_cast<const CONSOLE_CTRLEVENT_MSG *>(
            message.body + sizeof(CONSOLE_MSG_HEADER));
        if (request->CtrlEvent != 0 && request->CtrlEvent != 1)
        {
            coreio::prepare_completion(message, coreio::error::status_invalid_parameter);
            return true;
        }
        if (request->ProcessGroupId != 0 &&
            (processes_ == nullptr || !processes_->contains_group(request->ProcessGroupId)))
        {
            coreio::prepare_completion(message, coreio::error::status_invalid_parameter);
            return true;
        }
        if (processes_ != nullptr)
        {
            std::vector<std::uintptr_t> targets(processes_->size());
            const auto count = processes_->copy_for_group(request->ProcessGroupId, targets);
            for (std::size_t i = 0; i < count; ++i)
            {
                ctrl_events_.push_back({static_cast<DWORD>(request->CtrlEvent),
                                        static_cast<DWORD>(targets[i])});
            }
        }
        coreio::prepare_completion(message);
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
        else if (request->StringType == CONSOLE_ASCII)
        {
            const auto count = std::min<std::size_t>(request->NumRecords, capacity);
            std::vector<char16_t> chars(count);
            const auto read = buffer_.read_chars(coordinate, std::span<char16_t>{chars});
            const auto code_page = state_.output_code_page != 0 ? state_.output_code_page : CP_ACP;
            const auto converted = ::WideCharToMultiByte(
                code_page, 0, reinterpret_cast<const wchar_t *>(chars.data()),
                static_cast<int>(read), reinterpret_cast<char *>(data),
                static_cast<int>(capacity), nullptr, nullptr);
            written = static_cast<std::size_t>(std::max(converted, 0));
        }
        request->NumRecords = static_cast<ULONG>(written);
        const auto unit = request->StringType == CONSOLE_ATTRIBUTE ? sizeof(std::uint16_t)
                          : request->StringType == CONSOLE_ASCII ? sizeof(char)
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
        else if (request->StringType == CONSOLE_ASCII)
        {
            const auto count = std::min<std::size_t>(request->NumRecords, data_size);
            auto x = static_cast<std::size_t>(coordinate.x);
            auto y = static_cast<std::size_t>(coordinate.y);
            const auto *chars = reinterpret_cast<const char *>(data);
            for (std::size_t i = 0; i < count && y < buffer_.height(); ++i)
            {
                if (x >= buffer_.width())
                {
                    x = 0;
                    ++y;
                    if (y >= buffer_.height())
                    {
                        break;
                    }
                }
                (void)buffer_.write_codepoint(x, y, static_cast<unsigned char>(chars[i]),
                                             state_.default_style, screen::cell_width::narrow,
                                             state_.default_hyperlink);
                ++x;
                ++written;
            }
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

    std::size_t write_console_text(std::u16string_view text) noexcept
    {
        screen::coord cursor{static_cast<std::int32_t>(state_.cursor().x),
                             static_cast<std::int32_t>(state_.cursor().y)};
        std::size_t written = 0;
        std::size_t offset = 0;
        while (offset < text.size())
        {
            auto segment_end = offset;
            while (segment_end < text.size() && text[segment_end] != u'\r' &&
                   text[segment_end] != u'\n')
            {
                ++segment_end;
            }
            if (segment_end > offset)
            {
                const auto result = buffer_.write_text_row<screen::text_measurement::console>(
                    cursor, text.substr(offset, segment_end - offset), {}, state_.default_style,
                    true, state_.default_hyperlink);
                written += result.consumed;
                if (cursor.x >= static_cast<std::int32_t>(state_.width()))
                {
                    cursor.x = 0;
                    ++cursor.y;
                }
            }
            if (segment_end >= text.size())
            {
                break;
            }
            cursor.x = 0;
            if (text[segment_end] == u'\n')
            {
                ++cursor.y;
            }
            ++segment_end;
            ++written;
            if (cursor.y >= static_cast<std::int32_t>(state_.height()))
            {
                const auto top = state_.scroll_top();
                const auto bottom = state_.scroll_bottom();
                buffer_.scroll_region_up(screen::rectangle{0, top, state_.width(), bottom - top + 1},
                                         1, state_.default_style);
                cursor.y = static_cast<std::int32_t>(bottom);
            }
            offset = segment_end;
        }
        state_.set_cursor(static_cast<std::size_t>(cursor.x),
                          static_cast<std::size_t>(cursor.y));
        return written;
    }
  private:
    void expand_console_alias(std::u16string &line) const
    {
        if (line.empty())
        {
            return;
        }
        const auto word_end = line.find(u' ');
        const auto end = word_end == std::u16string::npos ? line.size() : word_end;
        const std::u16string_view source{line.data(), end};
        for (const auto &alias : state_.aliases)
        {
            if (alias.source != source)
            {
                continue;
            }
            std::u16string expanded = alias.target;
            if (word_end != std::u16string::npos)
            {
                expanded.append(line, word_end, std::u16string::npos);
            }
            line = std::move(expanded);
            return;
        }
    }
    static void erase_character(char16_t *output, std::size_t &units, std::size_t &cursor,
                                std::size_t position) noexcept
    {
        for (std::size_t i = position; i + 1 < units; ++i)
        {
            output[i] = output[i + 1];
        }
        if (units != 0)
        {
            --units;
        }
        if (cursor > units)
        {
            cursor = units;
        }
    }

    static void echo_position(screen::coord origin, std::int32_t width, std::size_t index,
                              std::int32_t &x, std::int32_t &y) noexcept
    {
        const auto first_capacity = std::max(width - origin.x, 1);
        if (index < static_cast<std::size_t>(first_capacity))
        {
            x = origin.x + static_cast<std::int32_t>(index);
            y = origin.y;
            return;
        }
        const auto rest = index - static_cast<std::size_t>(first_capacity);
        x = static_cast<std::int32_t>(rest % static_cast<std::size_t>(width));
        y = origin.y + 1 + static_cast<std::int32_t>(rest / static_cast<std::size_t>(width));
    }
    void redraw_echo_line(const char16_t *output, std::size_t units, std::size_t cursor, bool echo,
                          screen::coord origin, std::size_t &echo_length,
                          screen::coord &echo_cursor) const noexcept
    {
        if (!echo)
        {
            return;
        }
        const auto width = static_cast<std::int32_t>(state_.width());
        const auto height = static_cast<std::int32_t>(state_.height());
        const auto first_capacity = std::max(width - origin.x, 1);


        for (std::size_t i = 0; i < units; ++i)
        {
            std::int32_t x = 0;
            std::int32_t y = 0;
            echo_position(origin, width, i, x, y);
            if (y < 0 || y >= height)
            {
                continue;
            }
            (void)buffer_.write_scalar(static_cast<std::size_t>(x), static_cast<std::size_t>(y),
                                       output[i], state_.default_style, screen::cell_width::narrow,
                                       state_.default_hyperlink);
        }
        for (std::size_t i = units; i < echo_length; ++i)
        {
            std::int32_t x = 0;
            std::int32_t y = 0;
            echo_position(origin, width, i, x, y);
            if (y < 0 || y >= height)
            {
                continue;
            }
            (void)buffer_.write_scalar(static_cast<std::size_t>(x), static_cast<std::size_t>(y),
                                       u' ', state_.default_style, screen::cell_width::narrow,
                                       state_.default_hyperlink);
        }
        echo_length = units;
        std::int32_t cursor_x = origin.x;
        std::int32_t cursor_y = origin.y;
        echo_position(origin, width, cursor, cursor_x, cursor_y);
        echo_cursor = screen::coord{cursor_x, cursor_y};
    }
    void fill_rect_segment(std::size_t x, std::size_t y, std::size_t width, char16_t fill_char,
                           std::uint16_t attributes) noexcept
    {
        if (width == 0)
        {
            return;
        }
        const screen::rect segment{static_cast<std::int32_t>(x), static_cast<std::int32_t>(y),
                                   static_cast<std::int32_t>(x + width - 1),
                                   static_cast<std::int32_t>(y)};
        buffer_.fill_chars(segment, fill_char);
        buffer_.fill_attrs(segment, attributes);
    }

    static std::u16string decode_console_text(const char *data, std::size_t offset,
                                              std::size_t bytes, bool unicode)
    {
        std::u16string value;
        if (unicode)
        {
            const auto *chars = reinterpret_cast<const char16_t *>(data + offset);
            value.assign(chars, chars + bytes / sizeof(char16_t));
        }
        else
        {
            for (std::size_t i = 0; i < bytes; ++i)
            {
                value.push_back(static_cast<unsigned char>(data[offset + i]));
            }
        }
        return value;
    }

    static void append_alias_value(char *output, std::size_t &written, std::u16string_view value,
                                   bool unicode)
    {
        if (unicode)
        {
            std::memcpy(output + written, value.data(), value.size() * sizeof(char16_t));
            written += value.size() * sizeof(char16_t);
            *reinterpret_cast<char16_t *>(output + written) = 0;
            written += sizeof(char16_t);
            return;
        }
        for (const char16_t ch : value)
        {
            output[written++] = static_cast<char>(ch < 0x80 ? ch : '?');
        }
        output[written++] = 0;
    }
    screen::screen_buffer &buffer_;
    screen::console_state &state_;
    consoleinput::input_queue *input_{};
    std::uintptr_t input_object_{};
    std::uintptr_t alternate_object_{};
    std::uintptr_t window_handle_{};
    coreio::process_registry *processes_{};
    std::u16string cooked_line_;
    std::size_t cooked_cursor_{};
    screen::coord cooked_origin_{};
    std::size_t cooked_echo_length_{};
    bool cooked_active_{};
    pending_read_kind pending_read_kind_{};
    bool cancel_pending_{};
    std::vector<std::pair<DWORD, DWORD>> ctrl_events_;
};

} // namespace corehost::consoleapi