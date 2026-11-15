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
          payload_{server}, api_{buffer, state, &input, input_object, &processes_, &payload_}
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
    void push_raw(std::span<const char> bytes)
    {
        raw_in_.append(bytes.data(), bytes.size());
    }

    [[nodiscard]] bool raw_pending() const noexcept
    {
        return pending_active_ && pending_kind_ == pending_kind::raw_read;
    }

    void on_input_eof()
    {
        raw_eof_ = true;
        if (pending_active_ && pending_kind_ == pending_kind::raw_read && server_.valid())
        {
            const auto capacity = std::min<std::size_t>(pending_message_.descriptor.OutputSize,
                                                         sizeof(pending_message_.body));
            const auto written = std::min<std::size_t>(raw_in_.size(), capacity);
            std::copy_n(raw_in_.begin(), written, pending_message_.body);
            raw_in_.clear();
            coreio::prepare_completion(pending_message_, 0, written, pending_message_.body,
                                       written);
            coreio::complete_io(server_, pending_message_.complete);
            pending_active_ = false;
            pending_kind_ = pending_kind::none;
        }
    }

    std::size_t read_raw(std::span<char> output)
    {
        if (output.empty() || raw_in_.empty())
        {
            return 0;
        }

        const auto limit = std::min<std::size_t>(output.size(), raw_in_.size());
        std::size_t consumed = 0;
        bool complete = false;
        bool eof = false;
        while (consumed < limit)
        {
            const auto value = static_cast<unsigned char>(raw_in_[consumed]);
            if (value == '\r')
            {
                ++consumed;
                if (consumed < raw_in_.size() && raw_in_[consumed] == '\n')
                {
                    ++consumed;
                }
                complete = true;
                break;
            }
            if (value == '\n')
            {
                ++consumed;
                complete = true;
                break;
            }
            if (value == 3 && (state_.input_mode & 0x0001) != 0)
            {
                raw_alerted_ = true;
                complete = true;
                break;
            }
            if (value == 26 && (state_.input_mode & 0x0001) != 0)
            {
                eof = true;
                complete = true;
                break;
            }
            ++consumed;
        }

        if (!complete)
        {
            if (raw_in_.size() < output.size())
            {
                return 0;
            }
            consumed = limit;
        }

        std::string line = raw_in_.substr(0, consumed);
        raw_in_.erase(0, consumed);
        raw_eof_ = eof;

        if (!line.empty() && line.back() == '\n' &&
            (line.size() < 2 || line[line.size() - 2] != '\r'))
        {
            line.insert(line.size() - 1, 1, '\r');
        }
        else if (!line.empty() && line.back() == '\r' && (eof || complete))
        {
            line.push_back('\n');
        }

        const auto written = std::min<std::size_t>(line.size(), output.size());
        std::copy_n(line.begin(), written, output.begin());
        return written;
    }
    [[nodiscard]] bool raw_eof() const noexcept
    {
        return raw_eof_;
    }

    [[nodiscard]] bool raw_alerted() const noexcept
    {
        return raw_alerted_;
    }

    void clear_raw_alerted() noexcept
    {
        raw_alerted_ = false;
    }

    void clear_raw_eof() noexcept
    {
        raw_eof_ = false;
    }
    void take_ctrl_events(std::vector<std::pair<DWORD, DWORD>> &output)
    {
        api_.take_ctrl_events(output);
    }

    [[nodiscard]] coreio::process_registry &processes() noexcept
    {
        return processes_;
    }

    void set_input_object(std::uintptr_t object) noexcept
    {
        api_.set_input_object(object);
    }

    void set_input_handle(std::uintptr_t handle) noexcept
    {
        api_.set_input_handle(handle);
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
            raw_in_.clear();
            raw_eof_ = false;
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
        if (pending_kind_ == pending_kind::raw_read)
        {
            if (raw_in_.empty())
            {
                return false;
            }
        }
        else if (pending_kind_ == pending_kind::console_input)
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
            LONG status = 0;
            if (written == 0)
            {
                if (raw_alerted_)
                {
                    clear_raw_alerted();
                    status = coreio::error::status_alerted;
                }
                else if (!raw_eof_)
                {
                    return false;
                }
                else
                {
                    clear_raw_eof();
                }
            }
            coreio::prepare_completion(pending_message_, status, written, pending_message_.body, written);
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
    std::string raw_in_;
    bool raw_eof_{};
    bool raw_alerted_{};
    coreio::process_registry processes_;
    coreio::input_payload_reader payload_;
    consoleapi::console_api api_;
    coreio::io_msg pending_message_{};
    bool pending_active_{};
    pending_kind pending_kind_{};
};

} // namespace corehost::corehost2