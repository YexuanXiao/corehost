#pragma once

#include "console_api.hpp"
#include "apply.hpp"
#include "utf8.hpp"
#include "vt_parser.hpp"
#include "vtstate.hpp"

#include "screen_buffer.hpp"
#include "console_state.hpp"

#include "condrv_io.hpp"
#include "input.hpp"
#include <memory>

namespace corehost::corehost2
{

// Assembly object for the ConDrv server side. It owns only the console-facing
// handler; reading/completion batching stays in libcoreio.
class server_session final
{
  public:
    server_session(screen::screen_buffer &buffer, screen::console_state &state,
                   consoleinput::input_queue &input,
                   std::uintptr_t input_object = 0,
                   win32::handle_view server = {}) noexcept
        : server_{server}, screen_{buffer}, state_{state}, input_{input}, parser_{raw_}, applier_{},
          api_{buffer, state, &input, input_object, &processes_}
    {
    }

    void write_raw(std::span<const char> bytes)
    {
        vtparser::decode_utf8(std::span<const char8_t>{reinterpret_cast<const char8_t *>(bytes.data()),
                                                         bytes.size()},
                             text_, utf8_pending_);

        std::array<char32_t, 256> codepoints{};
        std::size_t count = 0;
        const auto flush = [&] {
            auto input = std::span<const char32_t>{codepoints.data(), count};
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
            count = 0;
        };

        for (std::size_t i = 0; i < text_.size(); ++i)
        {
            char32_t value = text_[i];
            if (value >= 0xD800 && value <= 0xDBFF && i + 1 < text_.size())
            {
                const auto low = static_cast<char32_t>(text_[i + 1]);
                if (low >= 0xDC00 && low <= 0xDFFF)
                {
                    value = 0x10000 + ((value - 0xD800) << 10) + (low - 0xDC00);
                    ++i;
                }
            }
            codepoints[count++] = value;
            if (count == codepoints.size())
            {
                flush();
            }
        }
        if (count != 0)
        {
            flush();
        }
    }

    std::size_t read_raw(std::span<char> output)
    {
        std::array<consoleinput::input_event, 32> events{};
        const auto count = input_.read(
            std::span<consoleinput::input_event>{events.data(), events.size()});
        std::size_t written = 0;
        for (std::size_t i = 0; i < count && written < output.size(); ++i)
        {
            if (events[i].type != consoleinput::input_event_type::key || !events[i].key.key_down)
            {
                continue;
            }
            const auto character = events[i].key.character;
            if (character < 0x80)
            {
                output[written++] = static_cast<char>(character);
            }
            else
            {
                output[written++] = static_cast<char>('?');
            }
        }
        return written;
    }

    [[nodiscard]] coreio::process_registry &processes() noexcept
    {
        return processes_;
    }

    void set_input_object(std::uintptr_t object) noexcept
    {
        api_.set_input_object(object);
    }

    void set_alternate_object(std::uintptr_t object) noexcept
    {
        api_.set_alternate_object(object);
    }

    void set_window_handle(std::uintptr_t window) noexcept
    {
        api_.set_window_handle(window);
    }

    bool operator()(coreio::io_msg &message)
    {
        return handle(message);
    }

    bool handle(coreio::io_msg &message)
    {
        if (api_.handle(message))
        {
            return true;
        }
        pending_message_ = message;
        pending_active_ = true;
        pending_kind_ = pending_kind::console_read;
        return false;
    }

    bool suspend_raw(coreio::io_msg &message)
    {
        pending_message_ = message;
        pending_active_ = true;
        pending_kind_ = pending_kind::raw_read;
        return false;
    }

    bool complete_pending()
    {
        if (!pending_active_ || !input_.has_key() || !server_.valid())
        {
            return false;
        }
        if (pending_kind_ == pending_kind::raw_read)
        {
            const auto capacity = std::min<std::size_t>(pending_message_.descriptor.OutputSize,
                                                         sizeof(pending_message_.body));
            const auto written = read_raw(std::span<char>{
                reinterpret_cast<char *>(pending_message_.body), capacity});
            if (written == 0)
            {
                return false;
            }
            coreio::prepare_completion(pending_message_, 0, written, pending_message_.body, written);
        }
        else if (!api_.resume_read(pending_message_))
        {
            return false;
        }
        coreio::complete_io(server_, pending_message_.complete);
        pending_active_ = false;
        pending_kind_ = pending_kind::none;
        return true;
    }

  private:
    enum class pending_kind : std::uint8_t
    {
        none,
        console_read,
        raw_read,
    };

    win32::handle_view server_;
    screen::screen_buffer &screen_;
    screen::console_state &state_;
    consoleinput::input_queue &input_;
    vtparser::raw_u32_buffer raw_;
    vtparser::vt_parser parser_;
    vtstate::state_applier applier_;
    std::u16string text_;
    std::string utf8_pending_;
    coreio::process_registry processes_;
    consoleapi::console_api api_;
    coreio::io_msg pending_message_{};
    bool pending_active_{};
    pending_kind pending_kind_{};
};

} // namespace corehost::corehost2