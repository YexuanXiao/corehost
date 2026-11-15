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

    [[nodiscard]] std::size_t last_consumed() const noexcept
    {
        return last_consumed_;
    }

  private:
    std::u16string text_;
    std::size_t last_consumed_{};
};

} // namespace corehost::vtstate