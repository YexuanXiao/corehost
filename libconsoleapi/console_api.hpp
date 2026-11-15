#pragma once

#include "console_state.hpp"
#include "screen_buffer.hpp"

#include "win32_types.hpp"

#include "condrv_io.hpp"
#include "os/Console/conmsgl1.h"
#include "os/Console/conmsgl2.h"

#include <algorithm>
#include <cstring>
#include <span>
#include <string_view>

namespace corehost::consoleapi
{

// Native console-facing operations. This layer owns cursor advancement and
// ABI projection, while libscreen owns cell storage.
class console_api final
{
  public:
    console_api(screen::screen_buffer &buffer, screen::console_state &state) noexcept
        : buffer_{buffer}, state_{state}
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
        info->CursorSize = 25;
        info->Visible = state_.cursor().visible ? TRUE : FALSE;
        coreio::prepare_completion(message, 0, sizeof(CONSOLE_GETCURSORINFO_MSG),
                                   message.body + sizeof(CONSOLE_MSG_HEADER),
                                   sizeof(CONSOLE_GETCURSORINFO_MSG));
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
};

} // namespace corehost::consoleapi