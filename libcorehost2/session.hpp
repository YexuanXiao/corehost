#pragma once

#include "apply.hpp"
#include "key_map.hpp"
#include "vt_parser.hpp"
#include "vtstate.hpp"

#include "console_state.hpp"
#include "screen_buffer.hpp"

#include <cstddef>
#include <optional>
#include <span>

namespace corehost::corehost2
{

// Minimal session assembly: owns the screen/console/parser objects and routes
// parsed VT messages into libvtstate. I/O and entry modes are added separately.
class session final
{
  public:
    session(std::size_t width, std::size_t height,
            screen::text_measurement measurement = screen::text_measurement::graphemes)
        : screen_{width, height}, state_{width, height}, parser_{raw_}, applier_{measurement}
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

            if (const auto key = consoleinput::key_from_message(result.id, result.message);
                key.has_value())
            {
                input_.push(consoleinput::input_event{consoleinput::input_event_type::key, *key});
            }
            (void)vtstate::apply(result.id, result.message, screen_, state_, applier_);
            parser_.reset();
        }
    }

    [[nodiscard]] screen::screen_buffer &screen() noexcept
    {
        return screen_;
    }

    void feed_input(std::span<const char32_t> input)
    {
        while (!input.empty())
        {
            const auto result = parser_.parse(std::u32string_view{input.data(), input.size()});
            input = input.subspan(result.consumed);
            if (result.id == vtparser::vt_message_id::continue_)
            {
                continue;
            }

            if (const auto key = consoleinput::key_from_message(result.id, result.message);
                key.has_value())
            {
                input_.push(consoleinput::input_event{consoleinput::input_event_type::key, *key});
                parser_.reset();
                continue;
            }

            if (const auto mouse = consoleinput::mouse_from_message(result.id, result.message);
                mouse.has_value() && state_.mouse_tracking())
            {
                consoleinput::input_event event{consoleinput::input_event_type::mouse};
                event.mouse = *mouse;
                input_.push(event);
                parser_.reset();
                continue;
            }

            if (result.id == vtparser::vt_message_id::continue_text ||
                result.id == vtparser::vt_message_id::text)
            {
                for (const char32_t ch : result.message.payload.text)
                {
                    consoleinput::key_event key;
                    key.character = ch <= 0xFFFF ? static_cast<char16_t>(ch) : u'\uFFFD';
                    input_.push(consoleinput::input_event{consoleinput::input_event_type::key, key});
                }
                parser_.reset();
                continue;
            }

            (void)vtstate::apply(result.id, result.message, screen_, state_, applier_);
            parser_.reset();
        }
    }

    [[nodiscard]] consoleinput::input_queue &input() noexcept
    {
        return input_;
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
    consoleinput::input_queue input_;
};

} // namespace corehost::corehost2