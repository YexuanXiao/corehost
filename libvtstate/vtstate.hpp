#pragma once

#include "vt_parser.hpp"

#include "console_state.hpp"
#include "screen_buffer.hpp"

#include <libunicode/convert.h>

#include <iterator>
#include <string>

namespace corehost::vtstate
{

// Applies parser text messages to libscreen. The UTF-16 conversion buffer is
// retained across calls so contiguous VT text does not allocate per message.
class state_applier final
{
  public:
    void apply_text(const vtparser::vt_message &message,
                    screen::screen_buffer &buffer,
                    screen::console_state &state)
    {
        text_.clear();
        unicode::convert_to<char16_t>(message.payload.text, std::back_inserter(text_));

        auto cursor = screen::coord{
            static_cast<std::int32_t>(state.cursor().x),
            static_cast<std::int32_t>(state.cursor().y),
        };

        const auto result = buffer.write_text_row<screen::text_measurement::graphemes>(
            cursor, text_, {}, state.default_style);

        state.set_cursor(static_cast<std::size_t>(cursor.x),
                         static_cast<std::size_t>(cursor.y));
        last_consumed_ = result.consumed;
    }

    void apply_line_rendition(const vtparser::vt_message &message,
                              screen::screen_buffer &buffer,
                              const screen::console_state &state)
    {
        const auto y = static_cast<std::size_t>(state.cursor().y);
        auto flags = buffer.flags(y);
        constexpr auto rendition_mask = static_cast<screen::line_flags>(
            screen::line_flag_ops::raw(screen::line_flag::double_width) |
            screen::line_flag_ops::raw(screen::line_flag::double_height_top) |
            screen::line_flag_ops::raw(screen::line_flag::double_height_bottom));
        flags &= ~rendition_mask;

        switch (message.payload.line_rendition)
        {
        case 3:
            flags |= screen::line_flag_ops::raw(screen::line_flag::double_height_top);
            break;
        case 4:
            flags |= screen::line_flag_ops::raw(screen::line_flag::double_height_bottom);
            break;
        case 6:
            flags |= screen::line_flag_ops::raw(screen::line_flag::double_width);
            break;
        case 5:
        default:
            break;
        }
        buffer.set_flags(y, flags);
    }

    [[nodiscard]] std::size_t last_consumed() const noexcept
    {
        return last_consumed_;
    }

  private:
    std::u16string text_;
    std::size_t last_consumed_{};
};

} // namespace corehost::vtstate