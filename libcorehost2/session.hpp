#pragma once

#include "apply.hpp"
#include "key_map.hpp"
#include "vt_parser.hpp"
#include "vtstate.hpp"

#include "console_state.hpp"
#include "screen_buffer.hpp"
#include "utility/log.hpp"

#include <cstddef>
#include <optional>
#include <span>
#include <string_view>

namespace corehost::corehost2
{

// Minimal session assembly: owns the screen/console/parser objects and routes
// parsed VT messages into libvtstate. I/O and entry modes are added separately.
class session final
{
  public:
    session(std::size_t width, std::size_t height,
            screen::text_measurement measurement = screen::text_measurement::graphemes, bool ambiguous_is_wide = false)
        : screen_{width, height}, state_{width, height}, parser_{raw_}, applier_{measurement, ambiguous_is_wide}
    {
        window_width_ = static_cast<std::int16_t>(width);
        window_height_ = static_cast<std::int16_t>(height);
    }

    template <std::size_t N>
    void feed(const char16_t (&input)[N])
    {
        feed(std::u16string_view{input, N});
    }

    void feed(std::u16string_view input)
    {
        while (!input.empty())
        {
            const auto result = parser_.parse(input);
            input.remove_prefix(result.consumed);
            if (result.id == vtparser::vt_message_id::continue_)
            {
                break;
            }
            consume(result, false);
            parser_.reset();
        }
    }

    [[nodiscard]] bool take_scrollback_cleared() noexcept
    {
        return state_.take_scrollback_cleared();
    }

    [[nodiscard]] bool take_title_changed() noexcept
    {
        return state_.take_title_changed();
    }

    [[nodiscard]] bool take_echo_line_feed() noexcept
    {
        return state_.take_echo_line_feed();
    }

    [[nodiscard]] bool take_win32_input_change(bool &enabled) noexcept
    {
        return state_.take_win32_input_change(enabled);
    }

    void queue_window_size(std::int16_t width, std::int16_t height)
    {
        if ((state_.input_mode & 0x0008) == 0 || (width == window_width_ && height == window_height_))
        {
            return;
        }
        window_width_ = width;
        window_height_ = height;
        consoleinput::input_event event{consoleinput::input_event_type::window};
        event.window.width = width;
        event.window.height = height;
        input_.push(event);
    }

    [[nodiscard]] bool take_viewport_changed() noexcept
    {
        return state_.take_viewport_changed();
    }

    [[nodiscard]] bool take_alternate_changed() noexcept
    {
        const auto changed = state_.alternate_changed;
        state_.alternate_changed = false;
        return changed;
    }

    [[nodiscard]] screen::screen_buffer &screen() noexcept
    {
        return screen_;
    }

    template <std::size_t N>
    void feed_input(const char16_t (&input)[N])
    {
        feed_input(std::u16string_view{input, N});
    }

    void feed_input(std::u16string_view input, bool raw_passthrough = false)
    {
        while (!input.empty())
        {
            const auto result = parser_.parse(input);
            input.remove_prefix(result.consumed);
            if (result.id == vtparser::vt_message_id::continue_)
            {
                break;
            }
            consume(result, true, raw_passthrough);
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
    [[nodiscard]] static bool is_terminal_response(vtparser::vt_message_id id) noexcept
    {
        switch (id)
        {
        case vtparser::vt_message_id::device_attributes:
        case vtparser::vt_message_id::cpr_response:
        case vtparser::vt_message_id::set_palette_color:
        case vtparser::vt_message_id::set_window_title:
        case vtparser::vt_message_id::unknown_sequence:
            return true;
        default:
            return false;
        }
    }

    [[nodiscard]] static std::u16string_view vt_input_sequence(consoleinput::virtual_key key) noexcept
    {
        switch (key)
        {
        case consoleinput::virtual_key::f1:
            return u"\x1bOP";
        case consoleinput::virtual_key::f2:
            return u"\x1bOQ";
        case consoleinput::virtual_key::f3:
            return u"\x1bOR";
        case consoleinput::virtual_key::f4:
            return u"\x1bOS";
        case consoleinput::virtual_key::f5:
            return u"\x1b[15~";
        case consoleinput::virtual_key::f6:
            return u"\x1b[17~";
        case consoleinput::virtual_key::f7:
            return u"\x1b[18~";
        case consoleinput::virtual_key::f8:
            return u"\x1b[19~";
        case consoleinput::virtual_key::f9:
            return u"\x1b[20~";
        case consoleinput::virtual_key::f10:
            return u"\x1b[21~";
        case consoleinput::virtual_key::f11:
            return u"\x1b[23~";
        case consoleinput::virtual_key::f12:
            return u"\x1b[24~";
        case consoleinput::virtual_key::up:
            return u"\x1b[A";
        case consoleinput::virtual_key::down:
            return u"\x1b[B";
        case consoleinput::virtual_key::right:
            return u"\x1b[C";
        case consoleinput::virtual_key::left:
            return u"\x1b[D";
        case consoleinput::virtual_key::home:
            return u"\x1b[H";
        case consoleinput::virtual_key::end:
            return u"\x1b[F";
        case consoleinput::virtual_key::insert:
            return u"\x1b[2~";
        case consoleinput::virtual_key::del:
            return u"\x1b[3~";
        case consoleinput::virtual_key::page_up:
            return u"\x1b[5~";
        case consoleinput::virtual_key::page_down:
            return u"\x1b[6~";
        default:
            return {};
        }
    }

    void consume(const vtparser::vt_parse_result &result, bool input_direction, bool raw_passthrough = false)
    {
        if (result.id == vtparser::vt_message_id::continue_)
        {
            return;
        }

        // Win32Input is a host-terminal protocol, not application VT input.
        // Even when the application enabled ENABLE_VIRTUAL_TERMINAL_INPUT,
        // the structured VK/Uc/ControlKeyState must become a KEY_EVENT;
        // otherwise characters are re-derived from the raw CSI text and
        // capitalization / modifiers are lost.
        if (input_direction && result.id == vtparser::vt_message_id::win32_input_key)
        {
            if (const auto key = consoleinput::key_from_message(result.id, result.message); key.has_value())
            {
                if (key->character == 0 && (raw_passthrough || screen_.alternate_active()))
                {
                    if (key->key_down)
                    {
                        for (const auto ch : vt_input_sequence(key->key))
                        {
                            input_.push(consoleinput::input_event{consoleinput::input_event_type::key,
                                                                  consoleinput::key_from_character(ch, true)});
                        }
                    }
                    return;
                }
                input_.push(consoleinput::input_event{consoleinput::input_event_type::key, *key});
            }
            return;
        }

        if (input_direction && raw_passthrough && !result.raw_sequence.empty())
        {
            for (const auto ch : result.raw_sequence)
            {
                input_.push(consoleinput::input_event{consoleinput::input_event_type::key,
                                                      consoleinput::key_from_character(ch, true)});
                input_.push(consoleinput::input_event{consoleinput::input_event_type::key,
                                                      consoleinput::key_from_character(ch, false)});
            }
            return;
        }

        if (input_direction && !raw_passthrough && is_terminal_response(result.id))
        {
            LOG("terminal response id=%d chars=%zu", static_cast<int>(result.id), result.raw_sequence.size());
            for (const auto ch : result.raw_sequence)
            {
                input_.push(consoleinput::input_event{consoleinput::input_event_type::key,
                                                      consoleinput::key_from_character(ch, true)});
                auto up = consoleinput::key_from_character(ch, true);
                up.character = 0;
                up.key_down = false;
                input_.push(consoleinput::input_event{consoleinput::input_event_type::key, up});
            }
            return;
        }

        if (const auto key = consoleinput::key_from_message(result.id, result.message); key.has_value())
        {
            input_.push(consoleinput::input_event{consoleinput::input_event_type::key, *key});
        }

        if (input_direction)
        {
            if (const auto mouse = consoleinput::mouse_from_message(result.id, result.message, &mouse_button_state_);
                mouse.has_value() && state_.mouse_tracking())
            {
                if (screen_.alternate_active())
                {
                    for (const auto ch : result.raw_sequence)
                    {
                        input_.push(consoleinput::input_event{consoleinput::input_event_type::key,
                                                              consoleinput::key_from_character(ch, true)});
                    }
                    return;
                }
                consoleinput::input_event event{consoleinput::input_event_type::mouse};
                event.mouse = *mouse;
                input_.push(event);
                return;
            }
        }

        if (input_direction)
        {
            consoleinput::virtual_key arrow_key{};
            bool has_arrow = false;
            switch (result.id)
            {
            case vtparser::vt_message_id::cursor_up:
                arrow_key = consoleinput::virtual_key::up;
                has_arrow = true;
                break;
            case vtparser::vt_message_id::cursor_down:
                arrow_key = consoleinput::virtual_key::down;
                has_arrow = true;
                break;
            case vtparser::vt_message_id::cursor_backward:
                arrow_key = consoleinput::virtual_key::left;
                has_arrow = true;
                break;
            case vtparser::vt_message_id::cursor_forward:
                arrow_key = consoleinput::virtual_key::right;
                has_arrow = true;
                break;
            case vtparser::vt_message_id::cursor_position:
                if (result.message.payload.position.row == 1 && result.message.payload.position.col == 1)
                {
                    arrow_key = consoleinput::virtual_key::home;
                    has_arrow = true;
                }
                break;
            case vtparser::vt_message_id::cursor_prev_line:
                arrow_key = consoleinput::virtual_key::end;
                has_arrow = true;
                break;
            default:
                break;
            }
            if (has_arrow)
            {
                consoleinput::key_event key;
                key.key = arrow_key;
                input_.push(consoleinput::input_event{consoleinput::input_event_type::key, key});
                return;
            }
        }

        if (input_direction)
        {
            char16_t control = 0;
            bool has_control = false;
            switch (result.id)
            {
            case vtparser::vt_message_id::char_control:
            case vtparser::vt_message_id::char_sub:
            case vtparser::vt_message_id::char_nul:
                control = static_cast<char16_t>(result.message.payload.count.value);
                has_control = true;
                break;
            case vtparser::vt_message_id::char_esc:
                control = 0x1B;
                has_control = true;
                break;
            case vtparser::vt_message_id::carriage_return:
            case vtparser::vt_message_id::line_feed:
                control = 0x0D;
                has_control = true;
                break;
            case vtparser::vt_message_id::cursor_forward_tab:
                control = 0x09;
                has_control = true;
                break;
            case vtparser::vt_message_id::char_backspace:
                control = 0x08;
                has_control = true;
                break;
            case vtparser::vt_message_id::char_del:
                control = 0x7F;
                has_control = true;
                break;
            default:
                break;
            }
            if (has_control)
            {
                input_.push(consoleinput::input_event{consoleinput::input_event_type::key,
                                                      consoleinput::key_from_character(control, true)});
                input_.push(consoleinput::input_event{consoleinput::input_event_type::key,
                                                      consoleinput::key_from_character(control, false)});
                return;
            }
        }

        if (input_direction &&
            (result.id == vtparser::vt_message_id::continue_text || result.id == vtparser::vt_message_id::text))
        {
            consoleinput::input_event event{};
            event.type = consoleinput::input_event_type::text;
            event.text = std::make_shared<const vtparser::raw_u16_string>(
                result.message.payload.text.begin(), result.message.payload.text.end());
            input_.push(std::move(event));
            return;
        }

        if ((result.id == vtparser::vt_message_id::apc_sequence ||
             result.id == vtparser::vt_message_id::dcs_sequence) &&
            applier_.apply_image(result.raw_sequence, screen_, state_))
        {
            return;
        }

        (void)vtstate::apply(result.id, result.message, screen_, state_, applier_);
    }

    screen::screen_buffer screen_;
    screen::console_state state_;
    vtparser::raw_u16_buffer raw_;
    vtparser::vt_parser parser_;
    vtstate::state_applier applier_;
    consoleinput::input_queue input_;
    std::uint32_t mouse_button_state_{};
    std::int16_t window_width_{};
    std::int16_t window_height_{};
};

} // namespace corehost::corehost2