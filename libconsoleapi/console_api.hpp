#pragma once

#include "console_state.hpp"
#include "screen_buffer.hpp"

#include "win32_types.hpp"

#include "condrv_io.hpp"
#include "os/Console/conmsgl1.h"
#include "os/Console/conmsgl2.h"

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