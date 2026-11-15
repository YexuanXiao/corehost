#pragma once

#include "apply.hpp"
#include "vt_parser.hpp"
#include "vtstate.hpp"

#include "console_state.hpp"
#include "screen_buffer.hpp"

#include <cstddef>
#include <span>

namespace corehost::corehost2
{

// Minimal session assembly: owns the screen/console/parser objects and routes
// parsed VT messages into libvtstate. I/O and entry modes are added separately.
class session final
{
  public:
    session(std::size_t width, std::size_t height)
        : screen_{width, height}, state_{width, height}, parser_{raw_}
    {
    }

    void feed(std::span<const char32_t> input)
    {
        while (!input.empty())
        {
            const auto result = parser_.parse(std::u32string_view{input.data(), input.size()});
            input = input.subspan(result.consumed);
            if (result.id == vtparser::vt_message_id::continue_)
            {
                continue;
            }

            (void)vtstate::apply(result.id, result.message, screen_, state_, applier_);
            parser_.reset();
        }
    }

    [[nodiscard]] screen::screen_buffer &screen() noexcept
    {
        return screen_;
    }

    [[nodiscard]] screen::console_state &state() noexcept
    {
        return state_;
    }

  private:
    screen::screen_buffer screen_;
    screen::console_state state_;
    vtparser::raw_u32_buffer raw_;
    vtparser::vt_parser parser_;
    vtstate::state_applier applier_;
};

} // namespace corehost::corehost2