#pragma once

#include "console_api.hpp"
#include "terminal_writer.hpp"
#include "viewport.hpp"
#include "utility/log.hpp"
#include "apply.hpp"
#include "utf8.hpp"
#include "vt_parser.hpp"
#include "vtstate.hpp"

#include "screen_buffer.hpp"
#include "console_state.hpp"

#include "condrv_io.hpp"
#include "deque.hpp"
#include "input.hpp"

#include <bit>
#include <cstring>
#include <memory>
#include <string_view>

namespace corehost::corehost2
{

template <bool ProcessedInput>
[[nodiscard]] inline std::size_t find_raw_control(std::string_view text, std::size_t limit) noexcept
{
    constexpr std::uint64_t high = 0x8080808080808080ull;
    constexpr std::uint64_t one = 0x0101010101010101ull;
    constexpr std::uint64_t cr = 0x0D0D0D0D0D0D0D0Dull;
    constexpr std::uint64_t lf = 0x0A0A0A0A0A0A0A0Aull;
    constexpr std::uint64_t ctrl_c = 0x0303030303030303ull;
    constexpr std::uint64_t ctrl_z = 0x1A1A1A1A1A1A1A1Aull;

    const auto has_zero = [](std::uint64_t value) noexcept {
        return (value - one) & ~value & high;
    };

    std::size_t offset = 0;
    while (offset + sizeof(std::uint64_t) <= limit)
    {
        std::uint64_t word = 0;
        std::memcpy(&word, text.data() + offset, sizeof(word));

        auto matches = has_zero(word ^ cr) | has_zero(word ^ lf);
        if constexpr (ProcessedInput)
        {
            matches |= has_zero(word ^ ctrl_c) | has_zero(word ^ ctrl_z);
        }
        if (matches != 0)
        {
            return offset + (std::countr_zero(matches) >> 3);
        }
        offset += sizeof(std::uint64_t);
    }

    while (offset < limit)
    {
        const auto value = static_cast<unsigned char>(text[offset]);
        if (value == '\r' || value == '\n' ||
            (ProcessedInput && (value == 3 || value == 26)))
        {
            return offset;
        }
        ++offset;
    }
    return limit;
}


// Assembly object for the ConDrv server side. It owns only the console-facing
// handler; reading/completion batching stays in libcoreio.
class server_session final
{
  public:
    server_session(screen::screen_buffer &buffer, screen::console_state &state, consoleinput::input_queue &input,
                   std::uintptr_t input_object = 0, win32::handle_view server = {},
                   conptyio::terminal_writer *writer = nullptr) noexcept
        : server_{server}, screen_{buffer}, state_{state}, input_{input}, parser_{raw_}, applier_{}, payload_{server},
          writer_{writer}, api_{buffer, state, &input, input_object, &processes_, &payload_}
    {
    }

    [[nodiscard]] static bool should_forward_to_terminal(vtparser::vt_message_id id) noexcept
    {
        switch (id)
        {
        case vtparser::vt_message_id::use_alternate_buffer:
        case vtparser::vt_message_id::use_main_buffer:
        case vtparser::vt_message_id::device_attributes:
        case vtparser::vt_message_id::report_cursor_position:
        case vtparser::vt_message_id::set_palette_color:
        case vtparser::vt_message_id::set_private_mode:
        case vtparser::vt_message_id::unknown_sequence:
            return true;
        default:
            return false;
        }
    }

    void write_raw(std::span<const char> bytes)
    {
        const auto code_page = state_.output_code_page;
        const auto units =
            ::MultiByteToWideChar(code_page, 0, bytes.data(), static_cast<int>(bytes.size()), nullptr, 0);
        text_.resize(static_cast<std::size_t>(std::max(units, 0)));
        if (units > 0)
        {
            (void)::MultiByteToWideChar(code_page, 0, bytes.data(), static_cast<int>(bytes.size()),
                                        reinterpret_cast<LPWSTR>(text_.data()), units);
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
                if (result.id == vtparser::vt_message_id::resize_window)
                {
                    queue_window_size_event();
                }
#ifdef COREHOST2_TESTING
                const bool forward = writer_ != nullptr && should_forward_to_terminal(result.id);
#else
                const bool forward = should_forward_to_terminal(result.id);
#endif
                if (forward)
                {
                    LOG("forward id=%d chars=%zu", static_cast<int>(result.id), result.raw_sequence.size());
                    for (const auto ch : result.raw_sequence)
                    {
                        const char8_t byte = static_cast<char8_t>(ch & 0x7F);
                        writer_->buffer().append(std::span<const char8_t>{&byte, 1});
                    }
                }
                parser_.reset();
            }
        }
    }
    std::size_t write_text(std::u16string_view input)
    {
        std::size_t consumed = 0;
        while (!input.empty())
        {
            const auto result = parser_.parse(input);
            consumed += result.consumed;
            input.remove_prefix(result.consumed);
            if (result.id == vtparser::vt_message_id::continue_)
            {
                break;
            }
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
        return consumed;
    }

    void push_raw(std::span<const char> bytes)
    {
        raw_in_.append(bytes.data(), bytes.size());
        signal_input_event();
    }

    void set_input_event(win32::handle_view event) noexcept
    {
        input_event_ = event;
    }

    void signal_input_event() const noexcept
    {
        if (input_event_.valid())
        {
            (void)::SetEvent(input_event_.get());
        }
    }

    void queue_window_size_event()
    {
        if ((state_.input_mode & 0x0008) == 0)
        {
            return;
        }
        consoleinput::input_event event{};
        event.type = consoleinput::input_event_type::window;
        event.window.width = static_cast<std::int16_t>(state_.width());
        event.window.height = static_cast<std::int16_t>(state_.height());
        input_.push(event);
        signal_input_event();
    }

    void reset_input_event_if_empty() const noexcept
    {
        if (input_event_.valid() && input_.empty() && raw_in_.empty())
        {
            (void)::ResetEvent(input_event_.get());
        }
    }

    [[nodiscard]] bool raw_pending() const noexcept
    {
        return pending_active_ && pending_kind_ == pending_kind::raw_read;
    }

    [[nodiscard]] bool console_read_pending() const noexcept
    {
        return pending_active_ && pending_kind_ == pending_kind::console_read;
    }

    void on_input_eof()
    {
        raw_eof_ = true;
#ifdef COREHOST2_TESTING
        if (!pending_active_ || !server_.valid())
#else
        if (!pending_active_)
#endif
        {
            return;
        }
        if (pending_kind_ == pending_kind::raw_read)
        {
            const auto capacity =
                std::min<std::size_t>(pending_message_.descriptor.OutputSize, sizeof(pending_message_.body));
            const auto written = std::min<std::size_t>(raw_in_.size(), capacity);
            std::copy_n(raw_in_.begin(), written, pending_message_.body);
            raw_in_.clear();
            coreio::prepare_completion(pending_message_, 0, written, pending_message_.body, written);
        }
        else
        {
            coreio::prepare_completion(pending_message_, 0, 0, pending_message_.body, 0);
        }
        coreio::complete_io(server_, pending_message_.complete);
        pending_active_ = false;
        pending_kind_ = pending_kind::none;
        for (auto &queued : queued_console_inputs_)
        {
            coreio::prepare_completion(queued, 0, 0, queued.body, 0);
            coreio::complete_io(server_, queued.complete);
        }
        queued_console_inputs_.clear();
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

        const bool processed_input = (state_.input_mode & 0x0001) != 0;
        const auto found = processed_input
                               ? find_raw_control<true>(raw_in_, limit)
                               : find_raw_control<false>(raw_in_, limit);
        if (found != limit)
        {
            const auto value = static_cast<unsigned char>(raw_in_[found]);
            consumed = found;
            if (value == '\r')
            {
                ++consumed;
                if (consumed < raw_in_.size() && raw_in_[consumed] == '\n')
                {
                    ++consumed;
                }
                complete = true;
            }
            else if (value == '\n')
            {
                ++consumed;
                complete = true;
            }
            else if (value == 3)
            {
                raw_alerted_ = true;
                complete = true;
            }
            else if (value == 26)
            {
                eof = true;
                complete = true;
            }
        }

        if (!complete)
        {
            if (raw_in_.size() < output.size())
            {
                return 0;
            }
            consumed = limit;
        }

        const bool add_cr =
            consumed != 0 && raw_in_[consumed - 1] == '\n' && (consumed < 2 || raw_in_[consumed - 2] != '\r');
        const bool add_lf = consumed != 0 && raw_in_[consumed - 1] == '\r' && (eof || complete);

        if (!add_cr && !add_lf)
        {
            std::memcpy(output.data(), raw_in_.data(), consumed);
            raw_in_.erase(0, consumed);
            raw_eof_ = eof;
            return consumed;
        }

        std::size_t written = 0;
        if (add_cr)
        {
            const auto prefix = consumed - 1;
            written = std::min(prefix, output.size());
            std::memcpy(output.data(), raw_in_.data(), written);
            if (written < output.size())
            {
                output[written++] = '\r';
            }
            if (written < output.size())
            {
                output[written++] = '\n';
            }
        }
        else
        {
            written = std::min(consumed, output.size());
            std::memcpy(output.data(), raw_in_.data(), written);
            if (written < output.size())
            {
                output[written++] = '\n';
            }
        }

        raw_in_.erase(0, consumed);
        raw_eof_ = eof;
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

    void set_output_object(std::uintptr_t object) noexcept
    {
        api_.set_output_object(object);
    }

    void set_window_handle(std::uintptr_t window) noexcept
    {
        api_.set_window_handle(window);
    }

    bool write_console(coreio::io_msg &message)
    {
        if (message.descriptor.InputSize < sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_WRITECONSOLE_MSG))
        {
            coreio::prepare_completion(message, coreio::error::status_invalid_parameter);
            return true;
        }
        auto *request = reinterpret_cast<CONSOLE_WRITECONSOLE_MSG *>(message.body + sizeof(CONSOLE_MSG_HEADER));
        const auto payload = payload_.read(message, sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_WRITECONSOLE_MSG));
        std::size_t written = 0;
        if (request->Unicode)
        {
            const auto *data = reinterpret_cast<const char16_t *>(payload.data());
            const auto count = payload.size() / sizeof(char16_t);
            written = write_text(std::u16string_view{data, count});
            request->NumBytes = static_cast<ULONG>(written * sizeof(char16_t));
        }
        else
        {
            const auto code_page = state_.output_code_page;
            const auto *bytes_data = reinterpret_cast<const char *>(payload.data());
            const auto units =
                ::MultiByteToWideChar(code_page, 0, bytes_data, static_cast<int>(payload.size()), nullptr, 0);
            text_.resize(static_cast<std::size_t>(std::max(units, 0)));
            if (units > 0)
            {
                (void)::MultiByteToWideChar(code_page, 0, bytes_data, static_cast<int>(payload.size()),
                                            reinterpret_cast<LPWSTR>(text_.data()), units);
            }
            written = write_text(text_);
            const auto encoded = ::WideCharToMultiByte(code_page, 0, reinterpret_cast<const wchar_t *>(text_.data()),
                                                       static_cast<int>(written), nullptr, 0, nullptr, nullptr);
            request->NumBytes = static_cast<ULONG>(std::max(encoded, 0));
        }
        coreio::prepare_completion(message, 0, sizeof(CONSOLE_WRITECONSOLE_MSG),
                                   message.body + sizeof(CONSOLE_MSG_HEADER), sizeof(CONSOLE_WRITECONSOLE_MSG));
        return true;
    }
    [[nodiscard]] screen::screen_buffer &screen() noexcept
    {
        return screen_;
    }

#ifdef COREHOST2_TESTING
    [[nodiscard]] bool should_flush() const noexcept
    {
        return writer_ != nullptr && !writer_->empty();
    }
#endif

    void flush() noexcept
    {
    }

    conptyio::terminal_writer *writer_{};

    bool handle(coreio::io_msg &message)
    {
        if (message.descriptor.InputSize >= sizeof(CONSOLE_MSG_HEADER))
        {
            const auto *header = reinterpret_cast<const CONSOLE_MSG_HEADER *>(message.body);
            if (header->ApiNumber == ConsolepWriteConsole)
            {
                return write_console(message);
            }
        }
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
            reset_input_event_if_empty();
            return true;
        }
        const auto requested_kind = api_.take_pending_read_kind();
        if (pending_active_ && requested_kind == consoleapi::console_api::pending_read_kind::console_input)
        {
            queued_console_inputs_.push_back(message);
            return false;
        }
        pending_message_ = message;
        pending_active_ = true;
        pending_kind_ = requested_kind == consoleapi::console_api::pending_read_kind::console_input
                            ? pending_kind::console_input
                            : pending_kind::console_read;
        if (pending_kind_ == pending_kind::console_input && !initial_repaint_sent_ &&
#ifdef COREHOST2_TESTING
            writer_ != nullptr &&
#endif
            screen_.alternate_active())
        {
            LOG("render initial alternate viewport");
            initial_repaint_sent_ = true;
            const auto cursor = screen::coord{
                static_cast<std::int32_t>(state_.cursor().x),
                static_cast<std::int32_t>(state_.cursor().y),
            };
            vtserializer::render_viewport(writer_->buffer(), screen_, cursor, &state_);
            if (!state_.title.empty())
            {
                vtserializer::append_window_title(writer_->buffer(), state_.title);
            }
            writer_->flush();
        }
        return false;
    }

    bool cancel_pending()
    {
#ifdef COREHOST2_TESTING
        if (!pending_active_ || !server_.valid())
#else
        if (!pending_active_)
#endif
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
        for (auto &queued : queued_console_inputs_)
        {
            coreio::prepare_completion(queued, 0, 0, queued.body, 0);
            coreio::complete_io(server_, queued.complete);
        }
        queued_console_inputs_.clear();
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
#ifdef COREHOST2_TESTING
        if (!pending_active_ || !server_.valid())
#else
        if (!pending_active_)
#endif
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
            const auto capacity =
                std::min<std::size_t>(pending_message_.descriptor.OutputSize, sizeof(pending_message_.body));
            const auto written = read_raw(std::span<char>{reinterpret_cast<char *>(pending_message_.body), capacity});
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
        if (!queued_console_inputs_.empty())
        {
            pending_message_ = queued_console_inputs_.front();
            queued_console_inputs_.pop_front();
            pending_active_ = true;
            pending_kind_ = pending_kind::console_input;
            (void)complete_pending();
        }
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
    win32::handle_view input_event_;
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
    bizwen::deque<coreio::io_msg> queued_console_inputs_;
    bool pending_active_{};
    pending_kind pending_kind_{};
    bool initial_repaint_sent_{};
};

} // namespace corehost::corehost2