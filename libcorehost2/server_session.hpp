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
        text_.clear();
        error_.clear();
        vtparser::decode_utf8(std::span<const char8_t>{reinterpret_cast<const char8_t *>(bytes.data()),
                                                         bytes.size()},
                             text_, error_);
        auto input = std::span<const char32_t>{text_};
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
        pending_ = std::make_unique<coreio::io_msg>(message);
        return false;
    }

    bool complete_pending()
    {
        if (!pending_ || input_.empty() || !server_.valid())
        {
            return false;
        }
        auto message = std::move(pending_);
        if (!api_.resume_read(*message))
        {
            pending_ = std::move(message);
            return false;
        }
        coreio::complete_io(server_, message->complete);
        return true;
    }

  private:
    win32::handle_view server_;
    screen::screen_buffer &screen_;
    screen::console_state &state_;
    consoleinput::input_queue &input_;
    vtparser::raw_u32_buffer raw_;
    vtparser::vt_parser parser_;
    vtstate::state_applier applier_;
    std::u32string text_;
    std::string error_;
    coreio::process_registry processes_;
    consoleapi::console_api api_;
    std::unique_ptr<coreio::io_msg> pending_;
};

} // namespace corehost::corehost2