#pragma once

#include "console_state.hpp"
#include "screen_buffer.hpp"

#include "win32_types.hpp"

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