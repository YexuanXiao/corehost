#pragma once

#include "console_state.hpp"
#include "screen_buffer.hpp"

#include "win32_types.hpp"
#include "input.hpp"
#include "input_payload.hpp"
#include "process_registry.hpp"

#include "condrv_io.hpp"
#include "os/Console/conmsgl1.h"
#include "os/Console/ntcon.h"
#include "os/Console/conmsgl2.h"
#include "os/Console/conmsgl3.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstring>
#include <span>
#include <string>
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
    console_api(screen::screen_buffer &buffer, screen::console_state &state, consoleinput::input_queue *input = nullptr,
                std::uintptr_t input_object = 0, coreio::process_registry *processes = nullptr,
                coreio::input_payload_reader *payload = nullptr) noexcept
        : buffer_{buffer}, state_{state}, input_{input}, input_object_{input_object}, processes_{processes},
          payload_{payload}
    {
        last_window_width_ = buffer_.width();
        last_window_height_ = buffer_.height();
    }

    [[nodiscard]] bool write_text(std::u16string_view text)
    {
        screen::coord cursor{
            static_cast<std::int32_t>(state_.cursor().x),
            static_cast<std::int32_t>(state_.cursor().y),
        };
        const auto result = buffer_.write_text_row<screen::text_measurement::console>(
            cursor, text, {}, state_.default_style, true, state_.default_hyperlink);
        state_.set_cursor(static_cast<std::size_t>(cursor.x), static_cast<std::size_t>(cursor.y));
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

    void set_input_handle(std::uintptr_t handle) noexcept
    {
        input_handle_ = handle;
    }

    void set_window_handle(std::uintptr_t window) noexcept
    {
        window_handle_ = window;
    }

    void set_alternate_object(std::uintptr_t object) noexcept
    {
        alternate_object_ = object;
    }

    void set_output_object(std::uintptr_t object) noexcept
    {
        output_object_ = object;
    }

    [[nodiscard]] bool handle(coreio::io_msg &message) noexcept
    {
        if (message.descriptor.InputSize < sizeof(CONSOLE_MSG_HEADER))
        {
            coreio::prepare_completion(message, coreio::error::status_invalid_parameter);
            return true;
        }

        const auto *header = reinterpret_cast<const CONSOLE_MSG_HEADER *>(message.body);
        const auto body_size = static_cast<std::size_t>(message.descriptor.InputSize - sizeof(CONSOLE_MSG_HEADER));
        if (header->ApiDescriptorSize != 0 &&
            (header->ApiDescriptorSize > sizeof(message.body) || header->ApiDescriptorSize > body_size))
        {
            coreio::prepare_completion(message, coreio::error::status_illegal_function);
            return true;
        }
        const auto layer = header->ApiNumber >> 24;
        switch (layer)
        {
        case 1:
            switch (header->ApiNumber)
            {
            case ConsolepGetCP:
                return get_code_page(message);
            case ConsolepGetMode:
                return get_mode(message, !is_output_object(message.descriptor.Object));
            case ConsolepSetMode:
                return set_mode(message, !is_output_object(message.descriptor.Object));
            case ConsolepGetNumberOfInputEvents:
                return get_number_of_input_events(message);
            case ConsolepGetConsoleInput:
                return read_console_input(message);
            case ConsolepReadConsole:
                return read_console(message);
            case ConsolepWriteConsole:
                return write_console(message);
            case ConsolepGetLangId:
                return get_language_id(message);
            default:
                break;
            }
            break;
        case 2:
            switch (header->ApiNumber)
            {
            case ConsolepFillConsoleOutput:
                return fill_console_output(message);
            case ConsolepGenerateCtrlEvent:
                return generate_ctrl_event(message);
            case ConsolepSetActiveScreenBuffer:
                return set_active_screen_buffer(message);
            case ConsolepFlushInputBuffer:
                return flush_input_buffer(message);
            case ConsolepSetCP:
                return set_code_page(message);
            case ConsolepGetCursorInfo:
                return get_cursor_info(message);
            case ConsolepSetCursorInfo:
                return set_cursor_info(message);
            case ConsolepGetScreenBufferInfo:
                return get_screen_buffer_info(message);
            case ConsolepSetScreenBufferInfo:
                return set_screen_buffer_info(message);
            case ConsolepSetScreenBufferSize:
                return set_screen_buffer_size(message);
            case ConsolepSetCursorPosition:
                return set_cursor_position(message);
            case ConsolepGetLargestWindowSize:
                return get_largest_window_size(message);
            case ConsolepScrollScreenBuffer:
                return scroll_console_buffer(message);
            case ConsolepSetTextAttribute:
                return set_text_attribute(message);
            case ConsolepSetWindowInfo:
                return set_window_info(message);
            case ConsolepReadConsoleOutputString:
                return read_console_output_string(message);
            case ConsolepWriteConsoleInput:
                return write_console_input(message);
            case ConsolepWriteConsoleOutput:
                return write_console_output(message);
            case ConsolepWriteConsoleOutputString:
                return write_console_output_string(message);
            case ConsolepReadConsoleOutput:
                return read_console_output(message);
            case ConsolepGetTitle:
                return get_title(message);
            case ConsolepSetTitle:
                return set_title(message);
            default:
                break;
            }
            break;
        case 3:
            switch (header->ApiNumber)
            {
            case ConsolepGetConsoleProcessList:
                return get_process_list(message);
            case ConsolepGetMouseInfo:
                return get_mouse_info(message);
            case ConsolepGetFontSize:
                return get_font_size(message);
            case ConsolepGetCurrentFont:
                return get_current_font(message);
            case ConsolepSetCurrentFont:
                return set_current_font(message);
            case ConsolepGetDisplayMode:
                return get_display_mode(message);
            case ConsolepSetDisplayMode:
                return set_display_mode(message);
            case ConsolepGetAliasesLength:
                return get_aliases_length(message);
            case ConsolepGetAliasExesLength:
                return get_alias_exes_length(message);
            case ConsolepGetAliases:
                return get_aliases(message);
            case ConsolepGetAliasExes:
                return get_alias_exes(message);
            case ConsolepAddAlias:
                return add_alias(message);
            case ConsolepGetAlias:
                return get_alias(message);
            case ConsolepExpungeCommandHistory:
                return expunge_command_history(message);
            case ConsolepSetNumberOfCommands:
                return set_number_of_commands(message);
            case ConsolepGetCommandHistoryLength:
                return get_command_history_length(message);
            case ConsolepGetCommandHistory:
                return get_command_history(message);
            case ConsolepGetHistory:
                return get_history_info(message);
            case ConsolepSetHistory:
                return set_history_info(message);
            case ConsolepGetConsoleWindow:
                return get_console_window(message);
            case ConsolepGetSelectionInfo:
                return get_selection_info(message);
            default:
                break;
            }
            break;
        default:
            break;
        }

        coreio::prepare_completion(message, coreio::error::status_not_implemented);
        return true;
    }

    void queue_window_buffer_size_event()
    {
        if (!has_input() || (state_.input_mode & 0x0008) == 0)
        {
            return;
        }
        const auto width = buffer_.width();
        const auto height = buffer_.height();
        if (width == last_window_width_ && height == last_window_height_)
        {
            return;
        }
        last_window_width_ = width;
        last_window_height_ = height;
        consoleinput::input_event event{consoleinput::input_event_type::window};
        event.window.width = static_cast<std::int16_t>(width);
        event.window.height = static_cast<std::int16_t>(height);
        input_->push(event);
    }

    [[nodiscard]] bool get_cursor_info(coreio::io_msg &message) noexcept
    {
        if (message.descriptor.InputSize < sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_GETCURSORINFO_MSG))
        {
            coreio::prepare_completion(message, coreio::error::status_invalid_parameter);
            return true;
        }
        auto *info = reinterpret_cast<CONSOLE_GETCURSORINFO_MSG *>(message.body + sizeof(CONSOLE_MSG_HEADER));
        info->CursorSize = state_.cursor().size;
        info->Visible = state_.cursor().visible ? TRUE : FALSE;
        coreio::prepare_completion(message, 0, sizeof(CONSOLE_GETCURSORINFO_MSG),
                                   message.body + sizeof(CONSOLE_MSG_HEADER), sizeof(CONSOLE_GETCURSORINFO_MSG));
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
        if (!input_handle_matches(message))
        {
            coreio::prepare_completion(message, coreio::error::status_invalid_handle);
            return true;
        }
        auto *request = reinterpret_cast<CONSOLE_GETCONSOLEINPUT_MSG *>(message.body + sizeof(CONSOLE_MSG_HEADER));
        if ((request->Flags & ~CONSOLE_READ_VALID) != 0)
        {
            coreio::prepare_completion(message, coreio::error::status_invalid_parameter);
            return true;
        }
        const auto local = sizeof(message.body) - sizeof(CONSOLE_MSG_HEADER) - sizeof(CONSOLE_GETCONSOLEINPUT_MSG);
        if (message.descriptor.OutputSize <= sizeof(CONSOLE_GETCONSOLEINPUT_MSG))
        {
            request->NumRecords = 0;
            coreio::prepare_completion(message, 0, 0, message.body + sizeof(CONSOLE_MSG_HEADER),
                                       sizeof(CONSOLE_GETCONSOLEINPUT_MSG));
            return true;
        }
        const auto capacity =
            std::min<std::size_t>(message.descriptor.OutputSize - sizeof(CONSOLE_GETCONSOLEINPUT_MSG), local);
        const auto max_records = capacity / sizeof(INPUT_RECORD);
        auto *output = reinterpret_cast<INPUT_RECORD *>(message.body + sizeof(CONSOLE_MSG_HEADER) +
                                                        sizeof(CONSOLE_GETCONSOLEINPUT_MSG));

        std::size_t count = 0;
        if (has_input())
        {
            auto &events = read_events_;
            const auto requested = std::min<std::size_t>(max_records, events.size());
            count = (request->Flags & CONSOLE_READ_NOREMOVE) != 0
                        ? input_->peek(std::span<consoleinput::input_event>{events.data(), requested})
                        : input_->read(std::span<consoleinput::input_event>{events.data(), requested});
            for (std::size_t i = 0; i < count; ++i)
            {
                auto &record = output[i];
                const auto &event = events[i];
                if (event.type == consoleinput::input_event_type::key)
                {
                    record.EventType = KEY_EVENT;
                    record.Event.KeyEvent.bKeyDown = event.key.key_down ? TRUE : FALSE;
                    record.Event.KeyEvent.wRepeatCount = event.key.repeat_count;
                    record.Event.KeyEvent.wVirtualKeyCode = static_cast<WORD>(std::to_underlying(event.key.key));
                    record.Event.KeyEvent.wVirtualScanCode = event.key.scan_code;
                    if (request->Unicode)
                    {
                        record.Event.KeyEvent.uChar.UnicodeChar = event.key.character;
                    }
                    else
                    {
                        const auto code_page = state_.output_code_page;
                        char narrow = 0;
                        (void)::WideCharToMultiByte(code_page, 0,
                                                    reinterpret_cast<const wchar_t *>(&event.key.character), 1, &narrow,
                                                    1, nullptr, nullptr);
                        record.Event.KeyEvent.uChar.AsciiChar = narrow;
                    }
                    record.Event.KeyEvent.dwControlKeyState = event.key.control_state;
                }
                else if (event.type == consoleinput::input_event_type::mouse)
                {
                    record.EventType = MOUSE_EVENT;
                    record.Event.MouseEvent.dwMousePosition =
                        COORD{static_cast<SHORT>(event.mouse.x), static_cast<SHORT>(event.mouse.y)};
                    record.Event.MouseEvent.dwButtonState = event.mouse.button_state;
                    record.Event.MouseEvent.dwControlKeyState = event.mouse.control_state;
                    record.Event.MouseEvent.dwEventFlags = event.mouse.event_flags;
                }
                else
                {
                    record.EventType = WINDOW_BUFFER_SIZE_EVENT;
                    record.Event.WindowBufferSizeEvent.dwSize = COORD{event.window.width, event.window.height};
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
        const auto data_bytes = static_cast<ULONG>(count * sizeof(INPUT_RECORD));
        coreio::prepare_completion(message, 0, data_bytes, message.body + sizeof(CONSOLE_MSG_HEADER),
                                   static_cast<ULONG>(sizeof(CONSOLE_GETCONSOLEINPUT_MSG) + data_bytes));
        return true;
    }

    [[nodiscard]] bool resume_read(coreio::io_msg &message) noexcept
    {
        return read_console(message, false);
    }

    [[nodiscard]] bool input_handle_matches(const coreio::io_msg &message) const noexcept
    {
        if (message.descriptor.Object == 0)
        {
            return true;
        }
        if (message.descriptor.Object == input_object_ ||
            (input_handle_ != 0 && message.descriptor.Object == input_handle_))
        {
            return true;
        }
        // Unknown objects are treated as input handles, matching corehost.
        return !is_output_object(message.descriptor.Object);
    }

    [[nodiscard]] bool is_output_object(std::uintptr_t object) const noexcept
    {
        return (output_object_ != 0 && object == output_object_) ||
               (alternate_object_ != 0 && object == alternate_object_);
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
        if (message.descriptor.OutputSize <= sizeof(CONSOLE_READCONSOLE_MSG) || !has_input())
        {
            request->NumBytes = 0;
            coreio::prepare_completion(message, 0, sizeof(CONSOLE_READCONSOLE_MSG),
                                       message.body + sizeof(CONSOLE_MSG_HEADER), sizeof(CONSOLE_READCONSOLE_MSG));
            return true;
        }
        const auto capacity =
            std::min<std::size_t>(message.descriptor.OutputSize - sizeof(CONSOLE_READCONSOLE_MSG), local);
        auto *output =
            reinterpret_cast<char16_t *>(message.body + sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_READCONSOLE_MSG));
        const auto max_units = capacity / sizeof(char16_t);
        std::size_t units = 0;
        if (cooked_active_)
        {
            units = std::min<std::size_t>(cooked_line_.size(), max_units);
            std::copy_n(cooked_line_.data(), units, output);
        }
        else if (request->Unicode && request->InitialNumBytes != 0)
        {
            const auto payload = read_input_payload(
                message, sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_READCONSOLE_MSG),
                message.descriptor.InputSize - sizeof(CONSOLE_MSG_HEADER) - sizeof(CONSOLE_READCONSOLE_MSG));
            const auto exe_bytes = static_cast<std::size_t>(request->ExeNameLength) * sizeof(char16_t);
            if (exe_bytes <= payload.size())
            {
                const auto *initial = reinterpret_cast<const char16_t *>(payload.data() + exe_bytes);
                const auto available = (payload.size() - exe_bytes) / sizeof(char16_t);
                const auto initial_units = std::min(
                    {static_cast<std::size_t>(request->InitialNumBytes / sizeof(char16_t)), available, max_units});
                std::copy_n(initial, initial_units, output);
                units = initial_units;
            }
        }

        const bool echo = (state_.input_mode & 0x0004) != 0;
        const screen::coord echo_origin = cooked_active_ ? cooked_origin_
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

        auto &events = read_events_;
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
            const auto &event = events[i];
            if (event.type != consoleinput::input_event_type::key || !event.key.key_down)
            {
                continue;
            }
            const auto character = event.key.character;
            if (event.key.repeat_count > 1)
            {
                auto remaining = event;
                --remaining.key.repeat_count;
                input_->push_front(remaining);
            }
            if (character == 0)
            {
                switch (event.key.key)
                {
                case consoleinput::virtual_key::left:
                    if (cursor != 0)
                    {
                        --cursor;
                    }
                    break;
                case consoleinput::virtual_key::right:
                    if (cursor < units)
                    {
                        ++cursor;
                    }
                    break;
                case consoleinput::virtual_key::home:
                    cursor = 0;
                    break;
                case consoleinput::virtual_key::end:
                    cursor = units;
                    break;
                case consoleinput::virtual_key::del:
                    if (cursor < units)
                    {
                        erase_character(output, units, cursor, cursor);
                    }
                    break;
                case consoleinput::virtual_key::up:
                {
                    auto &selected = history_selection_;
                    if (state_.browse_history_up(std::u16string_view{output, units}, selected))
                    {
                        units = std::min<std::size_t>(selected.size(), max_units);
                        std::copy_n(selected.data(), units, output);
                        cursor = units;
                    }
                    break;
                }
                case consoleinput::virtual_key::down:
                {
                    auto &selected = history_selection_;
                    if (state_.browse_history_down(selected))
                    {
                        units = std::min<std::size_t>(selected.size(), max_units);
                        std::copy_n(selected.data(), units, output);
                        cursor = units;
                    }
                    break;
                }
                default:
                    break;
                }
                redraw_echo_line(output, units, cursor, echo, echo_origin, echo_length, echo_cursor);
                continue;
            }

            if (character < u' ')
            {
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
                if (request->ProcessControlZ != FALSE && character == 26 && (state_.input_mode & 0x0001) != 0)
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
                        state_.echo_line_feed = true;
                    }
                    auto &expanded = history_selection_;
                    expanded.assign(output, units);
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
                        state_.break_history_browse();
                    }
                    redraw_echo_line(output, units, cursor, echo, echo_origin, echo_length, echo_cursor);
                    continue;
                }
                if (character != u'\t')
                {
                    continue;
                }
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
                state_.set_cursor(static_cast<std::size_t>(echo_cursor.x), static_cast<std::size_t>(echo_cursor.y));
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
            state_.set_cursor(static_cast<std::size_t>(echo_cursor.x), static_cast<std::size_t>(echo_cursor.y));
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
            const auto code_page = state_.input_code_page;
            const auto capacity = max_units * sizeof(char16_t);
            ansi_buffer_.resize(capacity);
            const auto converted =
                ::WideCharToMultiByte(code_page, 0, reinterpret_cast<const wchar_t *>(output), static_cast<int>(units),
                                      ansi_buffer_.data(), static_cast<int>(ansi_buffer_.size()), nullptr, nullptr);
            if (converted == 0 && units != 0)
            {
                coreio::prepare_completion(message, coreio::error::status_invalid_parameter);
                return true;
            }
            bytes = static_cast<std::size_t>(converted);
            std::memcpy(output, ansi_buffer_.data(), bytes);
            request->NumBytes = static_cast<ULONG>(bytes);
        }
        coreio::prepare_completion(message, 0, bytes, message.body + sizeof(CONSOLE_MSG_HEADER),
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
        const auto *request =
            reinterpret_cast<const CONSOLE_SETTEXTATTRIBUTE_MSG *>(message.body + sizeof(CONSOLE_MSG_HEADER));
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
        const auto *request =
            reinterpret_cast<const CONSOLE_SCROLLSCREENBUFFER_MSG *>(message.body + sizeof(CONSOLE_MSG_HEADER));
        const auto source = request->ScrollRectangle;
        if (source.Left > source.Right || source.Top > source.Bottom ||
            (source.Left == request->DestinationOrigin.X && source.Top == request->DestinationOrigin.Y))
        {
            coreio::prepare_completion(message);
            return true;
        }
        const screen::rectangle source_rect{static_cast<std::size_t>(source.Left), static_cast<std::size_t>(source.Top),
                                            static_cast<std::size_t>(source.Right - source.Left + 1),
                                            static_cast<std::size_t>(source.Bottom - source.Top + 1)};
        const screen::rectangle clip_rect{
            static_cast<std::size_t>(request->ClipRectangle.Left), static_cast<std::size_t>(request->ClipRectangle.Top),
            static_cast<std::size_t>(request->ClipRectangle.Right - request->ClipRectangle.Left + 1),
            static_cast<std::size_t>(request->ClipRectangle.Bottom - request->ClipRectangle.Top + 1)};
        const screen::coord destination{request->DestinationOrigin.X, request->DestinationOrigin.Y};
        buffer_.copy_rect(source_rect, clip_rect, request->Clip != FALSE, destination);

        char16_t fill_char = request->Fill.Char.UnicodeChar;
        if (!request->Unicode)
        {
            const auto code_page = state_.output_code_page;
            const char byte = request->Fill.Char.AsciiChar;
            wchar_t wide[4]{};
            const auto units = ::MultiByteToWideChar(code_page, 0, &byte, 1, wide, 4);
            fill_char = units > 0 ? static_cast<char16_t>(wide[0]) : u'?';
        }
        auto fill_attributes = request->Fill.Attributes;
        if (fill_char == 0 && fill_attributes == 0)
        {
            fill_char = u' ';
            fill_attributes = state_.default_attributes;
        }
        auto copy_left = source_rect.x;
        auto copy_top = source_rect.y;
        auto copy_right = source_rect.x + source_rect.width;
        auto copy_bottom = source_rect.y + source_rect.height;
        if (request->Clip != FALSE)
        {
            copy_left = std::max(copy_left, clip_rect.x);
            copy_top = std::max(copy_top, clip_rect.y);
            copy_right = std::min(copy_right, clip_rect.x + clip_rect.width);
            copy_bottom = std::min(copy_bottom, clip_rect.y + clip_rect.height);
        }
        if (copy_left < copy_right && copy_top < copy_bottom)
        {
            const auto dest_left = destination.x + static_cast<std::int32_t>(copy_left - source_rect.x);
            const auto dest_top = destination.y + static_cast<std::int32_t>(copy_top - source_rect.y);
            const auto dest_right = dest_left + static_cast<std::int32_t>(copy_right - copy_left);
            const auto copy_width = static_cast<std::int32_t>(copy_right - copy_left);
            for (auto y = copy_top; y < copy_bottom; ++y)
            {
                const auto dest_y = dest_top + static_cast<std::int32_t>(y - copy_top);
                if (dest_y < 0 || dest_y >= static_cast<std::int32_t>(buffer_.height()))
                {
                    fill_rect_segment(copy_left, y, copy_width, fill_char, fill_attributes);
                    continue;
                }
                if (copy_left < dest_left)
                {
                    fill_rect_segment(copy_left, y,
                                      std::min(dest_left, static_cast<std::int32_t>(copy_right)) -
                                          static_cast<std::int32_t>(copy_left),
                                      fill_char, fill_attributes);
                }
                const auto right_start = std::max(dest_right, static_cast<std::int32_t>(copy_left));
                if (right_start < static_cast<std::int32_t>(copy_right))
                {
                    fill_rect_segment(right_start, y, static_cast<std::int32_t>(copy_right) - right_start, fill_char,
                                      fill_attributes);
                }
            }
        }
        const auto full_screen_scroll =
            source.Left <= 0 && source.Top <= 0 && source.Right >= static_cast<SHORT>(buffer_.width() - 1) &&
            source.Bottom >= static_cast<SHORT>(buffer_.height() - 1) && request->DestinationOrigin.X == 0 &&
            request->DestinationOrigin.Y <= -static_cast<SHORT>(buffer_.height()) && request->Clip == FALSE &&
            fill_char == u' ';
        if (full_screen_scroll)
        {
            buffer_.clear_history();
            state_.scrollback_cleared = true;
            state_.set_cursor(0, 0);
        }
        state_.viewport_changed = true;
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
        const auto *request =
            reinterpret_cast<const CONSOLE_SETWINDOWINFO_MSG *>(message.body + sizeof(CONSOLE_MSG_HEADER));
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
            area =
                screen::rect{area.left + origin.x, area.top + origin.y, area.right + origin.x, area.bottom + origin.y};
        }
        const auto window_width = static_cast<std::size_t>(area.right - area.left + 1);
        const auto window_height = static_cast<std::size_t>(area.bottom - area.top + 1);
        if (window_width > buffer_.width() || window_height > buffer_.height())
        {
            const auto width = std::max<std::size_t>(buffer_.width(), window_width);
            const auto height = std::max<std::size_t>(buffer_.height(), window_height);
            state_.resize(width, height);
            buffer_.resize(width, height, state_.default_style);
            queue_window_buffer_size_event();
        }
        buffer_.set_viewport(area);
        state_.viewport_changed = true;
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
        const auto payload = read_input_payload(message, sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_WRITECONSOLE_MSG),
                                                message.descriptor.InputSize - sizeof(CONSOLE_MSG_HEADER) -
                                                    sizeof(CONSOLE_WRITECONSOLE_MSG));

        std::size_t written = 0;
        if (request->Unicode)
        {
            const auto *data = reinterpret_cast<const char16_t *>(payload.data());
            const auto count = payload.size() / sizeof(char16_t);
            written = write_console_text(std::u16string_view{data, count});
            request->NumBytes = static_cast<ULONG>(written * sizeof(char16_t));
        }
        else
        {
            const auto code_page = state_.output_code_page;
            const auto *bytes_data = reinterpret_cast<const char *>(payload.data());
            const auto units =
                ::MultiByteToWideChar(code_page, 0, bytes_data, static_cast<int>(payload.size()), nullptr, 0);
            text_buffer_.resize(static_cast<std::size_t>(std::max(units, 0)));
            if (units > 0)
            {
                (void)::MultiByteToWideChar(code_page, 0, bytes_data, static_cast<int>(payload.size()),
                                            reinterpret_cast<LPWSTR>(text_buffer_.data()), units);
            }
            written = write_console_text(text_buffer_);
            const auto encoded =
                ::WideCharToMultiByte(code_page, 0, reinterpret_cast<const wchar_t *>(text_buffer_.data()),
                                      static_cast<int>(written), nullptr, 0, nullptr, nullptr);
            request->NumBytes = static_cast<ULONG>(std::max(encoded, 0));
        }
        coreio::prepare_completion(message, 0, sizeof(CONSOLE_WRITECONSOLE_MSG),
                                   message.body + sizeof(CONSOLE_MSG_HEADER), sizeof(CONSOLE_WRITECONSOLE_MSG));
        return true;
    }
    [[nodiscard]] bool write_console_input(coreio::io_msg &message) noexcept
    {
        if (message.descriptor.InputSize < sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_WRITECONSOLEINPUT_MSG))
        {
            coreio::prepare_completion(message, coreio::error::status_invalid_parameter);
            return true;
        }
        auto *request = reinterpret_cast<CONSOLE_WRITECONSOLEINPUT_MSG *>(message.body + sizeof(CONSOLE_MSG_HEADER));
        const auto payload = read_input_payload(
            message, sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_WRITECONSOLEINPUT_MSG),
            message.descriptor.InputSize - sizeof(CONSOLE_MSG_HEADER) - sizeof(CONSOLE_WRITECONSOLEINPUT_MSG));
        const auto records = std::min<std::size_t>(request->NumRecords, payload.size() / sizeof(INPUT_RECORD));
        const auto *input = reinterpret_cast<const INPUT_RECORD *>(payload.data());
        std::size_t written = 0;
        prepend_buffer_.clear();
        auto &prepend = prepend_buffer_;
        if (has_input())
        {
            for (std::size_t i = 0; i < records; ++i)
            {
                consoleinput::input_event converted;
                if (input[i].EventType == KEY_EVENT)
                {
                    consoleinput::key_event key;
                    if (request->Unicode)
                    {
                        key.character = input[i].Event.KeyEvent.uChar.UnicodeChar;
                    }
                    else
                    {
                        const auto code_page = state_.input_code_page;
                        char bytes[2]{input[i].Event.KeyEvent.uChar.AsciiChar, 0};
                        int byte_count = 1;
                        if (::IsDBCSLeadByteEx(code_page, static_cast<BYTE>(bytes[0])) && i + 1 < records &&
                            input[i + 1].EventType == KEY_EVENT)
                        {
                            bytes[1] = input[i + 1].Event.KeyEvent.uChar.AsciiChar;
                            byte_count = 2;
                            ++i;
                        }
                        wchar_t wide[2];
                        const auto units = ::MultiByteToWideChar(code_page, 0, bytes, byte_count, wide, 2);
                        key.character = units > 0 ? static_cast<char16_t>(wide[0]) : 0;
                    }
                    key.key = static_cast<consoleinput::virtual_key>(input[i].Event.KeyEvent.wVirtualKeyCode);
                    key.scan_code = input[i].Event.KeyEvent.wVirtualScanCode;
                    key.control_state = input[i].Event.KeyEvent.dwControlKeyState;
                    key.repeat_count = input[i].Event.KeyEvent.wRepeatCount;
                    key.key_down = input[i].Event.KeyEvent.bKeyDown != FALSE;
                    converted.type = consoleinput::input_event_type::key;
                    converted.key = key;
                }
                else if (input[i].EventType == MOUSE_EVENT)
                {
                    converted.type = consoleinput::input_event_type::mouse;
                    converted.mouse.x = input[i].Event.MouseEvent.dwMousePosition.X;
                    converted.mouse.y = input[i].Event.MouseEvent.dwMousePosition.Y;
                    converted.mouse.button_state = input[i].Event.MouseEvent.dwButtonState;
                    converted.mouse.control_state = input[i].Event.MouseEvent.dwControlKeyState;
                    converted.mouse.event_flags = input[i].Event.MouseEvent.dwEventFlags;
                }
                else if (input[i].EventType == WINDOW_BUFFER_SIZE_EVENT)
                {
                    converted.type = consoleinput::input_event_type::window;
                    converted.window.width = input[i].Event.WindowBufferSizeEvent.dwSize.X;
                    converted.window.height = input[i].Event.WindowBufferSizeEvent.dwSize.Y;
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
                                   message.body + sizeof(CONSOLE_MSG_HEADER), sizeof(CONSOLE_WRITECONSOLEINPUT_MSG));
        return true;
    }

    [[nodiscard]] bool set_screen_buffer_info(coreio::io_msg &message) noexcept
    {
        if (message.descriptor.InputSize < sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_SCREENBUFFERINFO_MSG))
        {
            coreio::prepare_completion(message, coreio::error::status_invalid_parameter);
            return true;
        }
        const auto *request =
            reinterpret_cast<const CONSOLE_SCREENBUFFERINFO_MSG *>(message.body + sizeof(CONSOLE_MSG_HEADER));
        if (request->Size.X <= 0 || request->Size.Y <= 0 || request->Size.X == SHRT_MAX ||
            request->Size.Y == SHRT_MAX || request->CurrentWindowSize.X <= 0 || request->CurrentWindowSize.Y <= 0 ||
            request->CurrentWindowSize.X > request->Size.X || request->CurrentWindowSize.Y > request->Size.Y)
        {
            coreio::prepare_completion(message, coreio::error::status_invalid_parameter);
            return true;
        }
        state_.resize(static_cast<std::size_t>(request->Size.X), static_cast<std::size_t>(request->Size.Y));
        buffer_.resize(static_cast<std::size_t>(request->Size.X), static_cast<std::size_t>(request->Size.Y),
                       state_.default_style);
        queue_window_buffer_size_event();
        state_.clamp_cursor();
        state_.default_attributes = request->Attributes;
        state_.default_style = buffer_.style_for_attributes(request->Attributes);
        state_.max_window_size = screen::coord{request->MaximumWindowSize.X, request->MaximumWindowSize.Y};
        state_.popup_attributes = request->PopupAttributes;
        for (std::size_t i = 0; i < state_.color_table.size(); ++i)
        {
            state_.color_table[i] = request->ColorTable[i];
        }
        const auto origin = buffer_.viewport_origin();
        buffer_.set_viewport(screen::rect{origin.x, origin.y, origin.x + request->CurrentWindowSize.X - 1,
                                          origin.y + request->CurrentWindowSize.Y - 1});
        state_.viewport_changed = true;
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
        info->Mode = input ? (state_.input_mode | 0x0080 | state_.input_private_mode) : state_.output_mode;
        coreio::prepare_completion(message, 0, sizeof(CONSOLE_MODE_MSG), message.body + sizeof(CONSOLE_MSG_HEADER),
                                   sizeof(CONSOLE_MODE_MSG));
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
            state_.win32_input_mode = (state_.input_mode & 0x0200) == 0;
            state_.win32_input_changed = true;
            if ((state_.input_mode & 0x0010) != 0)
            {
                if (!state_.mouse_tracking())
                {
                    state_.set_private_mode(1000, true);
                }
                state_.set_private_mode(1006, true);
            }
            else
            {
                state_.set_private_mode(1000, false);
                state_.set_private_mode(1002, false);
                state_.set_private_mode(1003, false);
                state_.set_private_mode(1006, false);
            }
            state_.set_private_mode(1004, (state_.input_mode & 0x0008) != 0);
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
        const bool alternate = alternate_object_ != 0 && message.descriptor.Object == alternate_object_;
        buffer_.set_alternate(alternate);
        state_.alternate_changed = true;
        coreio::prepare_completion(message);
        return true;
    }

    [[nodiscard]] bool flush_input_buffer(coreio::io_msg &message) noexcept
    {
        if (has_input())
        {
            input_->clear();
        }
        cancel_pending_ = true;
        coreio::prepare_completion(message);
        return true;
    }

    void take_ctrl_events(std::vector<std::pair<DWORD, DWORD>> &output)
    {
        output.append_range(ctrl_events_);
        ctrl_events_.clear();
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
        const auto local =
            sizeof(message.body) - sizeof(CONSOLE_MSG_HEADER) - sizeof(CONSOLE_GETCONSOLEPROCESSLIST_MSG);
        const auto capacity =
            message.descriptor.OutputSize <= sizeof(CONSOLE_GETCONSOLEPROCESSLIST_MSG)
                ? 0
                : std::min<std::size_t>(message.descriptor.OutputSize - sizeof(CONSOLE_GETCONSOLEPROCESSLIST_MSG),
                                        local) /
                      sizeof(std::uint32_t);
        const auto count = has_processes() ? std::min<std::size_t>(processes_->size(), capacity) : 0;
        info->dwProcessCount = static_cast<ULONG>(count);
        auto *output = reinterpret_cast<std::uint32_t *>(message.body + sizeof(CONSOLE_MSG_HEADER) +
                                                         sizeof(CONSOLE_GETCONSOLEPROCESSLIST_MSG));
        for (std::size_t i = 0; i < count; ++i)
        {
            output[i] = static_cast<std::uint32_t>(processes_->processes()[processes_->size() - 1 - i]);
        }
        coreio::prepare_completion(message, 0, count * sizeof(std::uint32_t), message.body + sizeof(CONSOLE_MSG_HEADER),
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
                                   message.body + sizeof(CONSOLE_MSG_HEADER), sizeof(CONSOLE_GETMOUSEINFO_MSG));
        return true;
    }

    [[nodiscard]] bool set_current_font(coreio::io_msg &message) noexcept
    {
        if (message.descriptor.InputSize < sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_CURRENTFONT_MSG))
        {
            coreio::prepare_completion(message, coreio::error::status_invalid_parameter);
            return true;
        }
        const auto *request =
            reinterpret_cast<const CONSOLE_CURRENTFONT_MSG *>(message.body + sizeof(CONSOLE_MSG_HEADER));
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
                                                               std::size_t descriptor_size, bool unicode)
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
        const auto exe = alias_exe_from_message(message, sizeof(CONSOLE_GETALIASESLENGTH_MSG), info->Unicode != FALSE);
        std::size_t length = 0;
        for (const auto &alias : state_.aliases)
        {
            if (!exe.empty() && alias.executable != exe)
            {
                continue;
            }
            if (info->Unicode)
            {
                length += (alias.source.size() + alias.target.size() + 2) * sizeof(char16_t);
            }
            else
            {
                const auto code_page = state_.input_code_page;
                length += console_text_length(alias.source, code_page) +
                          console_text_length(alias.target, code_page) + 2;
            }
        }
        info->AliasesLength = static_cast<ULONG>(length);
        coreio::prepare_completion(message, 0, sizeof(CONSOLE_GETALIASESLENGTH_MSG),
                                   message.body + sizeof(CONSOLE_MSG_HEADER), sizeof(CONSOLE_GETALIASESLENGTH_MSG));
        return true;
    }

    [[nodiscard]] bool get_alias_exes_length(coreio::io_msg &message) noexcept
    {
        if (message.descriptor.InputSize < sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_GETALIASEXESLENGTH_MSG))
        {
            coreio::prepare_completion(message, coreio::error::status_invalid_parameter);
            return true;
        }
        auto *info = reinterpret_cast<CONSOLE_GETALIASEXESLENGTH_MSG *>(message.body + sizeof(CONSOLE_MSG_HEADER));
        const auto unit = info->Unicode ? sizeof(char16_t) : sizeof(char);
        std::size_t length = 0;
        auto &seen = alias_exes_seen_;
        seen.clear();
        for (const auto &alias : state_.aliases)
        {
            if (std::ranges::find(seen, alias.executable) != seen.end())
            {
                continue;
            }
            seen.push_back(alias.executable);
            if (info->Unicode)
            {
                length += (alias.executable.size() + 1) * sizeof(char16_t);
            }
            else
            {
                length += console_text_length(alias.executable, state_.input_code_page) + 1;
            }
        }
        info->AliasExesLength = static_cast<ULONG>(length);
        coreio::prepare_completion(message, 0, sizeof(CONSOLE_GETALIASEXESLENGTH_MSG),
                                   message.body + sizeof(CONSOLE_MSG_HEADER), sizeof(CONSOLE_GETALIASEXESLENGTH_MSG));
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
        const auto exe = alias_exe_from_message(message, sizeof(CONSOLE_GETALIASES_MSG), info->Unicode != FALSE);
        std::size_t written = 0;
        for (const auto &alias : state_.aliases)
        {
            if (!exe.empty() && alias.executable != exe)
            {
                continue;
            }
            const auto bytes = info->Unicode
                                   ? (alias.source.size() + alias.target.size() + 2) * sizeof(char16_t)
                                   : console_text_length(alias.source, state_.input_code_page) +
                                         console_text_length(alias.target, state_.input_code_page) + 2;
            if (written + bytes > capacity)
            {
                break;
            }
            append_alias_value(reinterpret_cast<char *>(output), written, capacity, alias.source,
                               info->Unicode != FALSE);
            if (info->Unicode)
            {
                *reinterpret_cast<char16_t *>(output + written - sizeof(char16_t)) = u'=';
            }
            else
            {
                output[written - 1] = '=';
            }
            append_alias_value(reinterpret_cast<char *>(output), written, capacity, alias.target,
                               info->Unicode != FALSE);
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
        auto &seen = alias_exes_seen_;
        seen.clear();
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
                std::memcpy(output + written, alias.executable.data(), alias.executable.size() * sizeof(char16_t));
                written += alias.executable.size() * sizeof(char16_t);
                output[written] = 0;
                written += sizeof(char16_t);
            }
            else
            {
                const auto code_page = state_.input_code_page;
                const auto units =
                    ::WideCharToMultiByte(code_page, 0, reinterpret_cast<const wchar_t *>(alias.executable.data()),
                                          static_cast<int>(alias.executable.size()), nullptr, 0, nullptr, nullptr);
                const auto size = static_cast<std::size_t>(std::max(units, 0));
                if (written + size + 1 > capacity)
                {
                    break;
                }
                if (size != 0)
                {
                    (void)::WideCharToMultiByte(
                        code_page, 0, reinterpret_cast<const wchar_t *>(alias.executable.data()),
                        static_cast<int>(alias.executable.size()), reinterpret_cast<char *>(output + written),
                        static_cast<int>(size), nullptr, nullptr);
                    written += size;
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

        auto &source = decoded_source_;
        decode_console_text_into(source, reinterpret_cast<const char *>(data), exe_end, source_end - exe_end,
                                 request->Unicode != FALSE, state_.input_code_page);
        auto &target = decoded_executable_;
        decode_console_text_into(target, reinterpret_cast<const char *>(data), source_end, target_end - source_end,
                                 request->Unicode != FALSE, state_.input_code_page);
        auto &executable = text_buffer_;
        decode_console_text_into(executable, reinterpret_cast<const char *>(data), 0, exe_end,
                                 request->Unicode != FALSE, state_.input_code_page);
        auto entry = screen::console_state::alias_entry{std::move(source), std::move(target), std::move(executable)};
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

        auto &executable = decoded_executable_;
        decode_console_text_into(executable, reinterpret_cast<const char *>(data), 0, exe_bytes,
                                 request->Unicode != FALSE, state_.input_code_page);
        auto &source = decoded_source_;
        decode_console_text_into(source, reinterpret_cast<const char *>(data), exe_bytes, source_bytes,
                                 request->Unicode != FALSE, state_.input_code_page);
        for (const auto &alias : state_.aliases)
        {
            if (alias.source == source && alias.executable == executable)
            {
                const auto available = message.descriptor.OutputSize > sizeof(CONSOLE_GETALIAS_MSG)
                                           ? message.descriptor.OutputSize - sizeof(CONSOLE_GETALIAS_MSG)
                                           : 0;
                auto *output = reinterpret_cast<char *>(request + 1);
                std::size_t bytes = 0;
                if (request->Unicode)
                {
                    bytes = (alias.target.size() + 1) * sizeof(char16_t);
                    if (bytes > available)
                    {
                        request->TargetLength = static_cast<USHORT>(available);
                        coreio::prepare_completion(
                            message, coreio::error::status_buffer_too_small, sizeof(CONSOLE_GETALIAS_MSG),
                            message.body + sizeof(CONSOLE_MSG_HEADER), sizeof(CONSOLE_GETALIAS_MSG));
                        return true;
                    }
                    std::memcpy(output, alias.target.data(), alias.target.size() * sizeof(char16_t));
                    *reinterpret_cast<char16_t *>(output + alias.target.size() * sizeof(char16_t)) = 0;
                }
                else
                {
                    const auto code_page = state_.input_code_page;
                    const auto units = console_text_length(alias.target, code_page);
                    if (units + 1 > available)
                    {
                        request->TargetLength = static_cast<USHORT>(available);
                        coreio::prepare_completion(
                            message, coreio::error::status_buffer_too_small, sizeof(CONSOLE_GETALIAS_MSG),
                            message.body + sizeof(CONSOLE_MSG_HEADER), sizeof(CONSOLE_GETALIAS_MSG));
                        return true;
                    }
                    if (units != 0)
                    {
                        (void)::WideCharToMultiByte(
                            code_page, 0, reinterpret_cast<const wchar_t *>(alias.target.data()),
                            static_cast<int>(alias.target.size()), output, static_cast<int>(units), nullptr, nullptr);
                    }
                    output[units] = 0;
                    bytes = units + 1;
                }
                request->TargetLength = static_cast<USHORT>(bytes);
                coreio::prepare_completion(message, 0, bytes, message.body + sizeof(CONSOLE_MSG_HEADER), bytes);
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
        const auto *info =
            reinterpret_cast<const CONSOLE_SETNUMBEROFCOMMANDS_MSG *>(message.body + sizeof(CONSOLE_MSG_HEADER));
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
        auto *info = reinterpret_cast<CONSOLE_GETCOMMANDHISTORYLENGTH_MSG *>(message.body + sizeof(CONSOLE_MSG_HEADER));
        std::size_t length = 0;
        for (const auto &command : state_.command_history)
        {
            if (info->Unicode)
            {
                length += (command.size() + 1) * sizeof(char16_t);
            }
            else
            {
                length += console_text_length(command, state_.input_code_page) + 1;
            }
        }
        info->CommandHistoryLength = static_cast<ULONG>(length);
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
            const auto code_page = state_.input_code_page;
            const auto bytes = info->Unicode ? (command.size() + 1) * sizeof(char16_t)
                                             : console_text_length(command, code_page) + 1;
            if (written + bytes > capacity)
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
                const auto units = bytes - 1;
                if (units != 0)
                {
                    (void)::WideCharToMultiByte(code_page, 0, reinterpret_cast<const wchar_t *>(command.data()),
                                                static_cast<int>(command.size()),
                                                reinterpret_cast<char *>(output + written),
                                                static_cast<int>(units), nullptr, nullptr);
                    written += units;
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
        coreio::prepare_completion(message, 0, sizeof(CONSOLE_HISTORY_MSG), message.body + sizeof(CONSOLE_MSG_HEADER),
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
        const auto *info = reinterpret_cast<const CONSOLE_HISTORY_MSG *>(message.body + sizeof(CONSOLE_MSG_HEADER));
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
        auto *info = reinterpret_cast<CONSOLE_GETCONSOLEWINDOW_MSG64 *>(message.body + sizeof(CONSOLE_MSG_HEADER));
        info->hwnd = reinterpret_cast<HWND>(window_handle_);
        coreio::prepare_completion(message, 0, sizeof(CONSOLE_GETCONSOLEWINDOW_MSG64),
                                   message.body + sizeof(CONSOLE_MSG_HEADER), sizeof(CONSOLE_GETCONSOLEWINDOW_MSG64));
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
                                   message.body + sizeof(CONSOLE_MSG_HEADER), sizeof(CONSOLE_GETDISPLAYMODE_MSG));
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
        if ((info->dwFlags & (CONSOLE_FULLSCREEN_MODE | CONSOLE_WINDOWED_MODE)) == 0)
        {
            coreio::prepare_completion(message, coreio::error::status_invalid_parameter);
            return true;
        }
        state_.display_mode = (info->dwFlags & CONSOLE_FULLSCREEN_MODE) != 0 ? CONSOLE_FULLSCREEN_MODE : 0;
        info->ScreenBufferDimensions = COORD{static_cast<SHORT>(buffer_.width()), static_cast<SHORT>(buffer_.height())};
        coreio::prepare_completion(message, 0, sizeof(CONSOLE_SETDISPLAYMODE_MSG),
                                   message.body + sizeof(CONSOLE_MSG_HEADER), sizeof(CONSOLE_SETDISPLAYMODE_MSG));
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
        info->FontSize = COORD{static_cast<SHORT>(state_.font_size.x), static_cast<SHORT>(state_.font_size.y)};
        coreio::prepare_completion(message, 0, sizeof(CONSOLE_GETFONTSIZE_MSG),
                                   message.body + sizeof(CONSOLE_MSG_HEADER), sizeof(CONSOLE_GETFONTSIZE_MSG));
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
        info->FontSize = COORD{static_cast<SHORT>(state_.font_size.x), static_cast<SHORT>(state_.font_size.y)};
        info->FontFamily = state_.font_family;
        info->FontWeight = state_.font_weight;
        const auto units = std::min<std::size_t>(state_.face_name.size(), LF_FACESIZE - 1);
        std::memcpy(info->FaceName, state_.face_name.data(), units * sizeof(char16_t));
        info->FaceName[units] = 0;
        coreio::prepare_completion(message, 0, sizeof(CONSOLE_CURRENTFONT_MSG),
                                   message.body + sizeof(CONSOLE_MSG_HEADER), sizeof(CONSOLE_CURRENTFONT_MSG));
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
            info->SelectionInfo.dwSelectionAnchor =
                COORD{static_cast<SHORT>(selection.anchor_x), static_cast<SHORT>(selection.anchor_y)};
            const auto left = std::min(selection.anchor_x, selection.caret_x);
            const auto top = std::min(selection.anchor_y, selection.caret_y);
            const auto right = std::max(selection.anchor_x, selection.caret_x);
            const auto bottom = std::max(selection.anchor_y, selection.caret_y);
            info->SelectionInfo.srSelection = SMALL_RECT{static_cast<SHORT>(left), static_cast<SHORT>(top),
                                                         static_cast<SHORT>(right), static_cast<SHORT>(bottom)};
        }
        coreio::prepare_completion(message, 0, sizeof(CONSOLE_GETSELECTIONINFO_MSG),
                                   message.body + sizeof(CONSOLE_MSG_HEADER), sizeof(CONSOLE_GETSELECTIONINFO_MSG));
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
                                       message.body + sizeof(CONSOLE_MSG_HEADER), sizeof(CONSOLE_GETTITLE_MSG));
            return true;
        }
        const auto capacity =
            std::min<std::size_t>(message.descriptor.OutputSize - sizeof(CONSOLE_GETTITLE_MSG), local);
        const auto &title = request->Original ? state_.original_title : state_.title;
        auto *output = message.body + sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_GETTITLE_MSG);
        std::size_t written = 0;
        if (request->Unicode)
        {
            const auto units = std::min<std::size_t>(title.size(), capacity / sizeof(char16_t));
            std::memcpy(output, title.data(), units * sizeof(char16_t));
            if (units < capacity / sizeof(char16_t))
            {
                *reinterpret_cast<char16_t *>(output + units * sizeof(char16_t)) = 0;
            }
            written = units * sizeof(char16_t);
            request->TitleLength = static_cast<ULONG>(title.size());
        }
        else
        {
            const auto code_page = state_.input_code_page;
            ansi_buffer_.resize(title.size() * 2 + 2);
            const auto converted =
                ::WideCharToMultiByte(code_page, 0, reinterpret_cast<const wchar_t *>(title.data()),
                                      static_cast<int>(title.size()), ansi_buffer_.data(),
                                      static_cast<int>(ansi_buffer_.size()), nullptr, nullptr);
            const auto total = static_cast<std::size_t>(std::max(converted, 0));
            const auto units = std::min<std::size_t>(total, capacity);
            std::memcpy(output, ansi_buffer_.data(), units);
            if (units < capacity)
            {
                output[units] = 0;
            }
            written = units;
            request->TitleLength = static_cast<ULONG>(total);
        }
        coreio::prepare_completion(message, 0, written, message.body + sizeof(CONSOLE_MSG_HEADER),
                                   sizeof(CONSOLE_GETTITLE_MSG) + written);
        return true;
    }

    [[nodiscard]] bool set_title(coreio::io_msg &message) noexcept
    {
        if (message.descriptor.InputSize < sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_SETTITLE_MSG))
        {
            coreio::prepare_completion(message, coreio::error::status_invalid_parameter);
            return true;
        }
        const auto *request = reinterpret_cast<const CONSOLE_SETTITLE_MSG *>(message.body + sizeof(CONSOLE_MSG_HEADER));
        const auto payload = read_input_payload(message, sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_SETTITLE_MSG),
                                                message.descriptor.InputSize - sizeof(CONSOLE_MSG_HEADER) -
                                                    sizeof(CONSOLE_SETTITLE_MSG));

        auto &title = text_buffer_;
        title.clear();
        if (request->Unicode)
        {
            const auto *chars = reinterpret_cast<const char16_t *>(payload.data());
            title.assign(chars, payload.size() / sizeof(char16_t));
        }
        else
        {
            const auto code_page = state_.input_code_page;
            const auto units = ::MultiByteToWideChar(code_page, 0, reinterpret_cast<const char *>(payload.data()),
                                                     static_cast<int>(payload.size()), nullptr, 0);
            title.resize(static_cast<std::size_t>(std::max(units, 0)));
            if (units > 0)
            {
                (void)::MultiByteToWideChar(code_page, 0, reinterpret_cast<const char *>(payload.data()),
                                            static_cast<int>(payload.size()), reinterpret_cast<LPWSTR>(title.data()),
                                            units);
            }
        }

        if (state_.title.empty())
        {
            state_.original_title = title;
        }
        state_.title = title;
        state_.title_changed = true;

#ifdef COREHOST2_TESTING
        if (window_handle_ != 0)
#endif
        {
            assert(window_handle_ != 0);
            static_assert(sizeof(wchar_t) == sizeof(char16_t));
            ::SetWindowTextW(reinterpret_cast<HWND>(window_handle_),
                             reinterpret_cast<LPCWSTR>(title.c_str()));
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
        const auto acp = ::GetACP();
        if (acp != 932 && acp != 936 && acp != 949 && acp != 950)
        {
            coreio::prepare_completion(message, coreio::error::status_not_supported);
            return true;
        }
        auto *info = reinterpret_cast<CONSOLE_LANGID_MSG *>(message.body + sizeof(CONSOLE_MSG_HEADER));
        switch (state_.output_code_page)
        {
        case 932:
            info->LangId = 0x0411;
            break;
        case 936:
            info->LangId = 0x0804;
            break;
        case 949:
            info->LangId = 0x0412;
            break;
        case 950:
            info->LangId = 0x0404;
            break;
        default:
            info->LangId = state_.language_id;
            break;
        }
        coreio::prepare_completion(message, 0, sizeof(CONSOLE_LANGID_MSG), message.body + sizeof(CONSOLE_MSG_HEADER),
                                   sizeof(CONSOLE_LANGID_MSG));
        return true;
    }

    [[nodiscard]] bool generate_ctrl_event(coreio::io_msg &message) noexcept
    {
        if (message.descriptor.InputSize < sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_CTRLEVENT_MSG))
        {
            coreio::prepare_completion(message, coreio::error::status_invalid_parameter);
            return true;
        }
        const auto *request =
            reinterpret_cast<const CONSOLE_CTRLEVENT_MSG *>(message.body + sizeof(CONSOLE_MSG_HEADER));
        if (request->CtrlEvent != 0 && request->CtrlEvent != 1)
        {
            coreio::prepare_completion(message, coreio::error::status_invalid_parameter);
            return true;
        }
        if (request->ProcessGroupId != 0 && (!has_processes() || !processes_->contains_group(request->ProcessGroupId)))
        {
            coreio::prepare_completion(message, coreio::error::status_invalid_parameter);
            return true;
        }
        if (has_processes())
        {
            ctrl_targets_.resize(processes_->size());
            const auto count = processes_->copy_for_group(request->ProcessGroupId, ctrl_targets_);
            for (std::size_t i = 0; i < count; ++i)
            {
                ctrl_events_.push_back(
                    {static_cast<DWORD>(request->CtrlEvent), static_cast<DWORD>(ctrl_targets_[i])});
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
        coreio::prepare_completion(message, 0, sizeof(CONSOLE_GETCP_MSG), message.body + sizeof(CONSOLE_MSG_HEADER),
                                   sizeof(CONSOLE_GETCP_MSG));
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
        if (info->CodePage == 0)
        {
            coreio::prepare_completion(message, coreio::error::status_invalid_parameter);
            return true;
        }
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
        if (!input_handle_matches(message))
        {
            coreio::prepare_completion(message, coreio::error::status_invalid_handle);
            return true;
        }
        auto *info = reinterpret_cast<CONSOLE_GETNUMBEROFINPUTEVENTS_MSG *>(message.body + sizeof(CONSOLE_MSG_HEADER));
        info->ReadyEvents = has_input() ? static_cast<ULONG>(input_->size()) : 0;
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
        const auto *request =
            reinterpret_cast<const CONSOLE_SETSCREENBUFFERSIZE_MSG *>(message.body + sizeof(CONSOLE_MSG_HEADER));
        if (request->Size.X <= 0 || request->Size.Y <= 0 || request->Size.X == SHRT_MAX || request->Size.Y == SHRT_MAX)
        {
            coreio::prepare_completion(message, coreio::error::status_invalid_parameter);
            return true;
        }
        state_.resize(static_cast<std::size_t>(request->Size.X), static_cast<std::size_t>(request->Size.Y));
        buffer_.resize(static_cast<std::size_t>(request->Size.X), static_cast<std::size_t>(request->Size.Y),
                       state_.default_style);
        queue_window_buffer_size_event();
        state_.clamp_cursor();
        state_.viewport_changed = true;
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
        auto *request =
            reinterpret_cast<CONSOLE_READCONSOLEOUTPUTSTRING_MSG *>(message.body + sizeof(CONSOLE_MSG_HEADER));
        if (request->StringType != CONSOLE_ASCII && request->StringType != CONSOLE_REAL_UNICODE &&
            request->StringType != CONSOLE_FALSE_UNICODE && request->StringType != CONSOLE_ATTRIBUTE)
        {
            coreio::prepare_completion(message, coreio::error::status_invalid_parameter);
            return true;
        }
        const auto local =
            sizeof(message.body) - sizeof(CONSOLE_MSG_HEADER) - sizeof(CONSOLE_READCONSOLEOUTPUTSTRING_MSG);
        if (message.descriptor.OutputSize <= sizeof(CONSOLE_READCONSOLEOUTPUTSTRING_MSG))
        {
            coreio::prepare_completion(message, coreio::error::status_invalid_parameter);
            return true;
        }
        const auto capacity =
            std::min<std::size_t>(message.descriptor.OutputSize - sizeof(CONSOLE_READCONSOLEOUTPUTSTRING_MSG), local);
        auto *data = reinterpret_cast<std::byte *>(message.body + sizeof(CONSOLE_MSG_HEADER) +
                                                   sizeof(CONSOLE_READCONSOLEOUTPUTSTRING_MSG));
        std::memset(data, 0, capacity);
        const auto unit = request->StringType == CONSOLE_ATTRIBUTE ? sizeof(std::uint16_t)
                          : request->StringType == CONSOLE_ASCII   ? sizeof(char)
                                                                   : sizeof(char16_t);
        const auto requested = request->NumRecords != 0
                                   ? std::min<std::size_t>(request->NumRecords, capacity / unit)
                                   : capacity / unit;
        request->NumRecords = 0;
        std::size_t written = 0;
        const auto coordinate = screen::coord{request->ReadCoord.X, request->ReadCoord.Y};
        if (request->StringType == CONSOLE_ATTRIBUTE)
        {
            const auto count = requested;
            written = buffer_.read_attrs(coordinate,
                                         std::span<std::uint16_t>{reinterpret_cast<std::uint16_t *>(data), count});
        }
        else if (request->StringType == CONSOLE_REAL_UNICODE || request->StringType == CONSOLE_FALSE_UNICODE)
        {
            const auto count = requested;
            written = buffer_.read_chars(coordinate, std::span<char16_t>{reinterpret_cast<char16_t *>(data), count});
        }
        else if (request->StringType == CONSOLE_ASCII)
        {
            const auto count = requested;
            text_buffer_.resize(count);
            const auto read = buffer_.read_chars(coordinate, std::span<char16_t>{text_buffer_});
            const auto code_page = state_.output_code_page;
            const auto converted = ::WideCharToMultiByte(
                code_page, 0, reinterpret_cast<const wchar_t *>(text_buffer_.data()), static_cast<int>(read),
                reinterpret_cast<char *>(data), static_cast<int>(capacity), nullptr, nullptr);
            written = static_cast<std::size_t>(std::max(converted, 0));
        }
        request->NumRecords = static_cast<ULONG>(written);
        coreio::prepare_completion(message, 0, written * unit, message.body + sizeof(CONSOLE_MSG_HEADER),
                                   sizeof(CONSOLE_READCONSOLEOUTPUTSTRING_MSG) + written * unit);
        return true;
    }

    [[nodiscard]] bool write_console_output_string(coreio::io_msg &message) noexcept
    {
        if (message.descriptor.InputSize < sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_WRITECONSOLEOUTPUTSTRING_MSG))
        {
            coreio::prepare_completion(message, coreio::error::status_invalid_parameter);
            return true;
        }
        auto *request =
            reinterpret_cast<CONSOLE_WRITECONSOLEOUTPUTSTRING_MSG *>(message.body + sizeof(CONSOLE_MSG_HEADER));
        if (request->StringType != CONSOLE_ASCII && request->StringType != CONSOLE_REAL_UNICODE &&
            request->StringType != CONSOLE_FALSE_UNICODE && request->StringType != CONSOLE_ATTRIBUTE)
        {
            coreio::prepare_completion(message, coreio::error::status_invalid_parameter);
            return true;
        }
        const auto *data = reinterpret_cast<const std::byte *>(message.body + sizeof(CONSOLE_MSG_HEADER) +
                                                               sizeof(CONSOLE_WRITECONSOLEOUTPUTSTRING_MSG));
        const auto data_size =
            message.descriptor.InputSize - sizeof(CONSOLE_MSG_HEADER) - sizeof(CONSOLE_WRITECONSOLEOUTPUTSTRING_MSG);
        const auto coordinate = screen::coord{request->WriteCoord.X, request->WriteCoord.Y};
        std::size_t written = 0;
        if (request->StringType == CONSOLE_ATTRIBUTE)
        {
            const auto count = data_size / sizeof(std::uint16_t);
            written = buffer_.write_attrs(
                coordinate, std::span<const std::uint16_t>{reinterpret_cast<const std::uint16_t *>(data), count});
        }
        else if (request->StringType == CONSOLE_REAL_UNICODE || request->StringType == CONSOLE_FALSE_UNICODE)
        {
            const auto count = data_size / sizeof(char16_t);
            written =
                buffer_.write_chars(coordinate, std::u16string_view{reinterpret_cast<const char16_t *>(data), count});
        }
        else if (request->StringType == CONSOLE_ASCII)
        {
            const auto count = data_size;
            const auto *chars = reinterpret_cast<const char *>(data);
            const auto code_page = state_.output_code_page;
            const auto units = ::MultiByteToWideChar(code_page, 0, chars, static_cast<int>(count), nullptr, 0);
            text_buffer_.resize(static_cast<std::size_t>(std::max(units, 0)));
            if (units > 0)
            {
                (void)::MultiByteToWideChar(code_page, 0, chars, static_cast<int>(count),
                                            reinterpret_cast<LPWSTR>(text_buffer_.data()), units);
            }
            written = buffer_.write_chars(coordinate, text_buffer_);
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
        const auto *request =
            reinterpret_cast<const CONSOLE_FILLCONSOLEOUTPUT_MSG *>(message.body + sizeof(CONSOLE_MSG_HEADER));
        if (request->WriteCoord.X < 0 || request->WriteCoord.Y < 0)
        {
            coreio::prepare_completion(message, coreio::error::status_invalid_parameter);
            return true;
        }

        if (request->ElementType != CONSOLE_ASCII && request->ElementType != CONSOLE_REAL_UNICODE &&
            request->ElementType != CONSOLE_FALSE_UNICODE && request->ElementType != CONSOLE_ATTRIBUTE)
        {
            coreio::prepare_completion(message, coreio::error::status_invalid_parameter);
            return true;
        }

        char16_t fill = static_cast<char16_t>(request->Element);
        if (request->ElementType == CONSOLE_ASCII)
        {
            const auto code_page = state_.output_code_page;
            const char byte = static_cast<char>(request->Element & 0xFF);
            wchar_t wide[4]{};
            const auto units = ::MultiByteToWideChar(code_page, 0, &byte, 1, wide, 4);
            if (units > 0)
            {
                fill = static_cast<char16_t>(wide[0]);
            }
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
                buffer_.fill_chars(area, fill);
            }
            remaining -= count;
            x = 0;
            ++y;
        }

        if (request->ElementType != CONSOLE_ATTRIBUTE && request->WriteCoord.X == 0 && request->WriteCoord.Y == 0 &&
            fill == u' ' && request->Length >= width * buffer_.height())
        {
            buffer_.clear_history();
            state_.scrollback_cleared = true;
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
        auto *request = reinterpret_cast<CONSOLE_READCONSOLEOUTPUT_MSG *>(message.body + sizeof(CONSOLE_MSG_HEADER));
        const auto &region = request->CharRegion;
        if (region.Left > region.Right || region.Top > region.Bottom)
        {
            request->CharRegion = SMALL_RECT{0, 0, 0, 0};
            coreio::prepare_completion(message, 0, 0, message.body + sizeof(CONSOLE_MSG_HEADER), 0);
            return true;
        }

        const auto local = sizeof(message.body) - sizeof(CONSOLE_MSG_HEADER) - sizeof(CONSOLE_READCONSOLEOUTPUT_MSG);
        if (message.descriptor.OutputSize <= sizeof(CONSOLE_READCONSOLEOUTPUT_MSG))
        {
            coreio::prepare_completion(message, coreio::error::status_invalid_parameter);
            return true;
        }
        const auto capacity =
            std::min<std::size_t>(message.descriptor.OutputSize - sizeof(CONSOLE_READCONSOLEOUTPUT_MSG), local);
        const auto width = static_cast<int>(region.Right) - static_cast<int>(region.Left) + 1;
        const auto height = static_cast<int>(region.Bottom) - static_cast<int>(region.Top) + 1;
        const auto required =
            static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * sizeof(interop::win32::char_info);
        if (width <= 0 || height <= 0 || required > capacity)
        {
            request->CharRegion = SMALL_RECT{0, 0, 0, 0};
            coreio::prepare_completion(message, coreio::error::status_invalid_parameter);
            return true;
        }

        auto *out = reinterpret_cast<interop::win32::char_info *>(message.body + sizeof(CONSOLE_MSG_HEADER) +
                                                                  sizeof(CONSOLE_READCONSOLEOUTPUT_MSG));
        std::memset(out, 0, required);

        auto clipped = region;
        if (clipped.Left < 0)
        {
            clipped.Left = 0;
        }
        if (clipped.Top < 0)
        {
            clipped.Top = 0;
        }
        if (clipped.Right >= static_cast<SHORT>(buffer_.width()))
        {
            clipped.Right = static_cast<SHORT>(buffer_.width() - 1);
        }
        if (clipped.Bottom >= static_cast<SHORT>(buffer_.height()))
        {
            clipped.Bottom = static_cast<SHORT>(buffer_.height() - 1);
        }
        if (clipped.Left > clipped.Right || clipped.Top > clipped.Bottom)
        {
            request->CharRegion = SMALL_RECT{0, 0, 0, 0};
            coreio::prepare_completion(message, 0, required, message.body + sizeof(CONSOLE_MSG_HEADER),
                                       sizeof(CONSOLE_READCONSOLEOUTPUT_MSG) + required);
            return true;
        }

        const auto code_page = state_.output_code_page;
        for (auto y = clipped.Top; y <= clipped.Bottom; ++y)
        {
            auto *row = out + static_cast<std::size_t>(y - region.Top) * static_cast<std::size_t>(width) +
                        static_cast<std::size_t>(clipped.Left - region.Left);
            (void)buffer_.read_char_info_abi(
                screen::rect{clipped.Left, y, clipped.Right, y},
                std::span<interop::win32::char_info>{row, static_cast<std::size_t>(clipped.Right - clipped.Left + 1)});
            if (!request->Unicode)
            {
                for (auto x = clipped.Left; x <= clipped.Right; ++x)
                {
                    auto &target = row[x - clipped.Left];
                    char mb = ' ';
                    (void)::WideCharToMultiByte(code_page, 0,
                                                reinterpret_cast<const wchar_t *>(&target.char_data.unicode_char), 1,
                                                &mb, 1, nullptr, nullptr);
                    target.char_data.ascii_char = mb;
                }
            }
        }

        request->CharRegion = clipped;
        coreio::prepare_completion(message, 0, required, message.body + sizeof(CONSOLE_MSG_HEADER),
                                   sizeof(CONSOLE_READCONSOLEOUTPUT_MSG) + required);
        return true;
    }

    [[nodiscard]] bool write_console_output(coreio::io_msg &message) noexcept
    {
        if (message.descriptor.InputSize < sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_WRITECONSOLEOUTPUT_MSG))
        {
            coreio::prepare_completion(message, coreio::error::status_invalid_parameter);
            return true;
        }
        const auto *request =
            reinterpret_cast<const CONSOLE_WRITECONSOLEOUTPUT_MSG *>(message.body + sizeof(CONSOLE_MSG_HEADER));
        if (request->CharRegion.Left < 0 || request->CharRegion.Top < 0 ||
            request->CharRegion.Right < request->CharRegion.Left ||
            request->CharRegion.Bottom < request->CharRegion.Top)
        {
            coreio::prepare_completion(message, coreio::error::status_invalid_parameter);
            return true;
        }
        const auto width = static_cast<std::size_t>(request->CharRegion.Right - request->CharRegion.Left + 1);
        const auto height = static_cast<std::size_t>(request->CharRegion.Bottom - request->CharRegion.Top + 1);
        const auto count = width * height;
        const auto required = sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_WRITECONSOLEOUTPUT_MSG) +
                              count * sizeof(interop::win32::char_info);
        if (message.descriptor.InputSize < required)
        {
            coreio::prepare_completion(message, coreio::error::status_invalid_parameter);
            return true;
        }

        const auto cell_bytes =
            read_input_payload(message, sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_WRITECONSOLEOUTPUT_MSG),
                               count * sizeof(interop::win32::char_info));
        if (cell_bytes.size() < count * sizeof(interop::win32::char_info))
        {
            coreio::prepare_completion(message, coreio::error::status_invalid_parameter);
            return true;
        }
        const auto *cells = reinterpret_cast<const interop::win32::char_info *>(cell_bytes.data());
        const screen::rect area{request->CharRegion.Left, request->CharRegion.Top, request->CharRegion.Right,
                                request->CharRegion.Bottom};
        const auto written =
            buffer_.write_char_info_abi(area, std::span<const interop::win32::char_info>{cells, count});
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
        const auto *info =
            reinterpret_cast<const CONSOLE_SETCURSORINFO_MSG *>(message.body + sizeof(CONSOLE_MSG_HEADER));
        if (info->CursorSize == 0 || info->CursorSize > 100)
        {
            coreio::prepare_completion(message, coreio::error::status_invalid_parameter);
            return true;
        }
        state_.cursor().visible = info->Visible != FALSE;
        state_.cursor().size = info->CursorSize;
        state_.cursor_changed = true;
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
        const auto *position =
            reinterpret_cast<const CONSOLE_SETCURSORPOSITION_MSG *>(message.body + sizeof(CONSOLE_MSG_HEADER));
        if (position->CursorPosition.X < 0 || position->CursorPosition.Y < 0 ||
            static_cast<std::size_t>(position->CursorPosition.X) >= buffer_.width() ||
            static_cast<std::size_t>(position->CursorPosition.Y) >= buffer_.height())
        {
            coreio::prepare_completion(message, coreio::error::status_invalid_parameter);
            return true;
        }
        state_.set_cursor(static_cast<std::size_t>(position->CursorPosition.X),
                          static_cast<std::size_t>(position->CursorPosition.Y));
        state_.cursor_changed = true;
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
        auto *info = reinterpret_cast<CONSOLE_GETLARGESTWINDOWSIZE_MSG *>(message.body + sizeof(CONSOLE_MSG_HEADER));
        info->Size = COORD{static_cast<SHORT>(state_.max_window_size.x), static_cast<SHORT>(state_.max_window_size.y)};
        coreio::prepare_completion(message, 0, sizeof(CONSOLE_GETLARGESTWINDOWSIZE_MSG),
                                   message.body + sizeof(CONSOLE_MSG_HEADER), sizeof(CONSOLE_GETLARGESTWINDOWSIZE_MSG));
        return true;
    }

    [[nodiscard]] bool get_screen_buffer_info(coreio::io_msg &message) noexcept
    {
        if (message.descriptor.InputSize < sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_SCREENBUFFERINFO_MSG))
        {
            coreio::prepare_completion(message, coreio::error::status_invalid_parameter);
            return true;
        }

        auto *info = reinterpret_cast<CONSOLE_SCREENBUFFERINFO_MSG *>(message.body + sizeof(CONSOLE_MSG_HEADER));
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
        info->MaximumWindowSize =
            COORD{static_cast<SHORT>(state_.max_window_size.x), static_cast<SHORT>(state_.max_window_size.y)};
        info->PopupAttributes = state_.popup_attributes;
        info->FullscreenSupported = FALSE;
        for (std::size_t i = 0; i < state_.color_table.size(); ++i)
        {
            info->ColorTable[i] = state_.color_table[i];
        }

        coreio::prepare_completion(message, 0, sizeof(CONSOLE_SCREENBUFFERINFO_MSG),
                                   message.body + sizeof(CONSOLE_MSG_HEADER), sizeof(CONSOLE_SCREENBUFFERINFO_MSG));
        return true;
    }

    [[nodiscard]] std::size_t read_cells(screen::rect area, std::span<interop::win32::char_info> output) noexcept
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
            while (segment_end < text.size() && text[segment_end] != u'\r' && text[segment_end] != u'\n')
            {
                ++segment_end;
            }
            if (segment_end > offset)
            {
                const auto result = buffer_.write_text_row<screen::text_measurement::console>(
                    cursor, text.substr(offset, segment_end - offset), {}, state_.default_style, true,
                    state_.default_hyperlink);
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
                buffer_.scroll_region_up(screen::rectangle{0, top, state_.width(), bottom - top + 1}, 1,
                                         state_.default_style);
                cursor.y = static_cast<std::int32_t>(bottom);
            }
            offset = segment_end;
        }
        state_.set_cursor(static_cast<std::size_t>(cursor.x), static_cast<std::size_t>(cursor.y));
        return written;
    }

  private:
    [[nodiscard]] bool has_input() const noexcept
    {
#ifdef COREHOST2_TESTING
        return input_ != nullptr;
#else
        return true;
#endif
    }

    [[nodiscard]] bool has_processes() const noexcept
    {
#ifdef COREHOST2_TESTING
        return processes_ != nullptr;
#else
        return true;
#endif
    }

    [[nodiscard]] std::span<const std::byte> read_input_payload(coreio::io_msg &message, std::size_t header_size,
                                                                std::size_t fallback_size) const noexcept
    {
#ifdef COREHOST2_TESTING
        if (payload_ == nullptr)
        {
            return std::span<const std::byte>{reinterpret_cast<const std::byte *>(message.body + header_size),
                                              fallback_size};
        }
#else
        (void)fallback_size;
#endif
        return payload_->read(message, header_size);
    }

    void expand_console_alias(std::u16string &line)
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
            auto &expanded = decoded_source_;
            expanded.assign(alias.target);
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

    static void echo_position(screen::coord origin, std::int32_t width, std::size_t index, std::int32_t &x,
                              std::int32_t &y) noexcept
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
                          screen::coord origin, std::size_t &echo_length, screen::coord &echo_cursor) const noexcept
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
            (void)buffer_.write_scalar(static_cast<std::size_t>(x), static_cast<std::size_t>(y), output[i],
                                       state_.default_style, screen::cell_width::narrow, state_.default_hyperlink);
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
            (void)buffer_.write_scalar(static_cast<std::size_t>(x), static_cast<std::size_t>(y), u' ',
                                       state_.default_style, screen::cell_width::narrow, state_.default_hyperlink);
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
                                   static_cast<std::int32_t>(x + width - 1), static_cast<std::int32_t>(y)};
        buffer_.fill_chars(segment, fill_char);
        buffer_.fill_attrs(segment, attributes);
    }

    static std::size_t console_text_length(std::u16string_view value, UINT code_page) noexcept
    {
        const auto units = ::WideCharToMultiByte(code_page, 0, reinterpret_cast<const wchar_t *>(value.data()),
                                                 static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
        return static_cast<std::size_t>(std::max(units, 0));
    }

    static void decode_console_text_into(std::u16string &value, const char *data, std::size_t offset,
                                         std::size_t bytes, bool unicode, UINT code_page)
    {
        value.clear();
        if (unicode)
        {
            const auto *chars = reinterpret_cast<const char16_t *>(data + offset);
            value.assign(chars, chars + bytes / sizeof(char16_t));
        }
        else
        {
            const auto units = ::MultiByteToWideChar(code_page, 0, data + offset, static_cast<int>(bytes), nullptr, 0);
            value.resize(static_cast<std::size_t>(std::max(units, 0)));
            if (units > 0)
            {
                (void)::MultiByteToWideChar(code_page, 0, data + offset, static_cast<int>(bytes),
                                            reinterpret_cast<LPWSTR>(value.data()), units);
            }
        }
    }

    void append_alias_value(char *output, std::size_t &written, std::size_t capacity, std::u16string_view value,
                            bool unicode) const
    {
        if (unicode)
        {
            const auto bytes = value.size() * sizeof(char16_t);
            if (written + bytes + sizeof(char16_t) > capacity)
            {
                return;
            }
            std::memcpy(output + written, value.data(), bytes);
            written += bytes;
            *reinterpret_cast<char16_t *>(output + written) = 0;
            written += sizeof(char16_t);
            return;
        }

        const auto code_page = state_.input_code_page;
        const auto units = console_text_length(value, code_page);
        const auto available = capacity - written - 1;
        assert(units <= available);
        if (units != 0)
        {
            (void)::WideCharToMultiByte(code_page, 0, reinterpret_cast<const wchar_t *>(value.data()),
                                        static_cast<int>(value.size()), output + written, static_cast<int>(units),
                                        nullptr, nullptr);
            written += units;
        }
        output[written++] = 0;
    }
    screen::screen_buffer &buffer_;
    screen::console_state &state_;
    consoleinput::input_queue *input_{};
    std::uintptr_t input_object_{};
    std::size_t last_window_width_{};
    std::size_t last_window_height_{};
    std::uintptr_t input_handle_{};
    std::uintptr_t alternate_object_{};
    std::uintptr_t output_object_{};
    std::uintptr_t window_handle_{};
    coreio::process_registry *processes_{};
    coreio::input_payload_reader *payload_{};
    std::u16string text_buffer_;
    std::u16string history_selection_;
    std::vector<std::u16string_view> alias_exes_seen_;
    std::vector<std::uintptr_t> ctrl_targets_;
    std::array<consoleinput::input_event, 64> read_events_;
    std::vector<char> ansi_buffer_;
    std::vector<consoleinput::input_event> prepend_buffer_;
    std::u16string cooked_line_;
    std::u16string decoded_executable_;
    std::u16string decoded_source_;
    std::size_t cooked_cursor_{};
    screen::coord cooked_origin_{};
    std::size_t cooked_echo_length_{};
    bool cooked_active_{};
    pending_read_kind pending_read_kind_{};
    bool cancel_pending_{};
    std::vector<std::pair<DWORD, DWORD>> ctrl_events_;
};

} // namespace corehost::consoleapi