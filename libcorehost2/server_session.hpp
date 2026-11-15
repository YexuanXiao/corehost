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
        const auto code_page = state_.output_code_page != 0 ? state_.output_code_page : CP_ACP;
        const auto units = ::MultiByteToWideChar(code_page, 0, bytes.data(),
                                               static_cast<int>(bytes.size()), nullptr, 0);
        text_.resize(static_cast<std::size_t>(std::max(units, 0)));
        if (units > 0)
        {
            (void)::MultiByteToWideChar(code_page, 0, bytes.data(),
                                        static_cast<int>(bytes.size()), reinterpret_cast<LPWSTR>(text_.data()), units);
        }
        auto input = std::u16string_view{text_};
        while (!input.empty())
        {
            const auto result = parser_.parse(input);
            input.remove_prefix(result.consumed);
            if (result.id != vtparser::vt_message_id::continue_)
            {
                if ((result.id == vtparser::vt_message_id::apc_sequence ||
                     result.id == vtparser::vt_message_id::dcs_sequence) &&
                    applier_.apply_image(result.raw_sequence, screen_, state_))
                {
                    parser_.reset();
                    continue;
                }
                (void)vtstate::apply(result.id, result.message, screen_, state_, applier_);
                parser_.reset();
            }
        }
    }
    std::size_t read_raw(std::span<char> output)
    {
        if (!raw_line_active_)
        {
            raw_line_.clear();
            raw_line_active_ = true;
            raw_eof_ = false;
        }

        std::array<consoleinput::input_event, 32> events{};
        const auto count = input_.read(
            std::span<consoleinput::input_event>{events.data(), events.size()});
        bool complete = false;
        for (std::size_t i = 0; i < count && !complete; ++i)
        {
            if (events[i].type != consoleinput::input_event_type::key || !events[i].key.key_down)
            {
                continue;
            }

            const auto character = events[i].key.character;
            if (character == u'\r' || character == u'\n')
            {
                raw_line_.push_back('\r');
                raw_line_.push_back('\n');
                complete = true;
                break;
            }
            if (character == 26 && (state_.input_mode & 0x0001) != 0)
            {
                raw_eof_ = true;
                complete = true;
                break;
            }
            if (character == 0)
            {
                append_key_sequence(events[i].key.key, raw_line_);
            }
            else
            {
                append_encoded_character(character, raw_line_);
            }

            if (raw_line_.size() >= output.size())
            {
                complete = true;
            }
        }

        if (!complete && raw_line_.size() < output.size())
        {
            return 0;
        }

        const auto written = std::min<std::size_t>(raw_line_.size(), output.size());
        std::copy_n(raw_line_.begin(), written, output.begin());
        raw_line_.clear();
        raw_line_active_ = false;
        return written;
    }

    [[nodiscard]] bool raw_eof() const noexcept
    {
        return raw_eof_;
    }

    void clear_raw_eof() noexcept
    {
        raw_eof_ = false;
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
            if (api_.take_cancel_pending())
            {
                (void)cancel_pending();
            }
            else if (pending_active_)
            {
                (void)complete_pending();
            }
            return true;
        }
        pending_message_ = message;
        pending_active_ = true;
        pending_kind_ =
            api_.take_pending_read_kind() == consoleapi::console_api::pending_read_kind::console_input
                ? pending_kind::console_input
                : pending_kind::console_read;
        return false;
    }

    bool cancel_pending()
    {
        if (!pending_active_ || !server_.valid())
        {
            return false;
        }
        if (pending_kind_ == pending_kind::raw_read)
        {
            coreio::prepare_completion(pending_message_, 0, 0, pending_message_.body, 0);
        }
        else if (pending_kind_ == pending_kind::console_input)
        {
            if (!api_.resume_console_input(pending_message_))
            {
                return false;
            }
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
    bool suspend_raw(coreio::io_msg &message)
    {
        pending_message_ = message;
        pending_active_ = true;
        pending_kind_ = pending_kind::raw_read;
        return false;
    }

    bool complete_pending()
    {
        if (!pending_active_ || !server_.valid())
        {
            return false;
        }
        if (pending_kind_ == pending_kind::console_input)
        {
            if (input_.empty())
            {
                return false;
            }
        }
        else if (!input_.has_key())
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
        else if (pending_kind_ == pending_kind::console_input)
        {
            if (!api_.resume_console_input(pending_message_))
            {
                return false;
            }
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
    static void append_encoded_character(char16_t character, std::string &output)
    {
        if (character < 0x80)
        {
            output.push_back(static_cast<char>(character));
            return;
        }
        char bytes[4]{};
        const auto written = ::WideCharToMultiByte(CP_UTF8, 0,
                                                   reinterpret_cast<const wchar_t *>(&character), 1,
                                                   bytes, static_cast<int>(sizeof(bytes)), nullptr,
                                                   nullptr);
        if (written > 0)
        {
            output.append(bytes, static_cast<std::size_t>(written));
        }
    }

    static void append_key_sequence(consoleinput::virtual_key key, std::string &output)
    {
        switch (key)
        {
        case consoleinput::virtual_key::up:
            output.append("\x1b[A");
            break;
        case consoleinput::virtual_key::down:
            output.append("\x1b[B");
            break;
        case consoleinput::virtual_key::right:
            output.append("\x1b[C");
            break;
        case consoleinput::virtual_key::left:
            output.append("\x1b[D");
            break;
        case consoleinput::virtual_key::home:
            output.append("\x1b[H");
            break;
        case consoleinput::virtual_key::end:
            output.append("\x1b[F");
            break;
        case consoleinput::virtual_key::insert:
            output.append("\x1b[2~");
            break;
        case consoleinput::virtual_key::del:
            output.append("\x1b[3~");
            break;
        case consoleinput::virtual_key::page_up:
            output.append("\x1b[5~");
            break;
        case consoleinput::virtual_key::page_down:
            output.append("\x1b[6~");
            break;
        default:
            break;
        }
    }

    enum class pending_kind : std::uint8_t
    {
        none,
        console_read,
        console_input,
        raw_read,
    };

    win32::handle_view server_;
    screen::screen_buffer &screen_;
    screen::console_state &state_;
    consoleinput::input_queue &input_;
    vtparser::raw_u16_buffer raw_;
    vtparser::vt_parser parser_;
    vtstate::state_applier applier_;
    std::u16string text_;
    std::string raw_line_;
    bool raw_line_active_{};
    bool raw_eof_{};
    coreio::process_registry processes_;
    consoleapi::console_api api_;
    coreio::io_msg pending_message_{};
    bool pending_active_{};
    pending_kind pending_kind_{};
};

} // namespace corehost::corehost2