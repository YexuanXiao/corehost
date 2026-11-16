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

#include <array>
#include <bit>
#include <cstring>
#include <memory>
#include <string>
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
        text_.resize(bytes.size());
        const auto units = ::MultiByteToWideChar(code_page, 0, bytes.data(), static_cast<int>(bytes.size()),
                                                 reinterpret_cast<LPWSTR>(text_.data()),
                                                 static_cast<int>(text_.size()));
        text_.resize(static_cast<std::size_t>(std::max(units, 0)));
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

    void note_raw_bytes(std::span<const char> bytes) noexcept
    {
        if (raw_line_complete_)
            return;
        const bool processed = (state_.input_mode & ENABLE_PROCESSED_INPUT) != 0;
        for (const auto value : bytes)
        {
            const auto byte = static_cast<unsigned char>(value);
            if (byte == '\r' || byte == '\n' || (processed && (byte == 3 || byte == 0x1A)))
            {
                raw_line_complete_ = true;
                return;
            }
        }
    }

    void push_raw(std::span<const char> bytes)
    {
        raw_in_.append(bytes.data(), bytes.size());
        note_raw_bytes(bytes);
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

    [[nodiscard]] bool raw_input_mode() const noexcept
    {
        return raw_mode_;
    }

    static void apply_line_edits(std::string &text)
    {
        std::string edited;
        edited.reserve(text.size());
        std::size_t cursor = 0;
        bool insert_mode = false;

        const auto previous = [&](std::size_t at) {
            if (at == 0)
                return std::size_t{0};
            --at;
            while (at != 0 && (static_cast<unsigned char>(edited[at]) & 0xC0) == 0x80)
                --at;
            return at;
        };
        const auto next = [&](std::size_t at) {
            if (at >= edited.size())
                return edited.size();
            ++at;
            while (at < edited.size() && (static_cast<unsigned char>(edited[at]) & 0xC0) == 0x80)
                ++at;
            return at;
        };
        const auto erase_before = [&] {
            const auto begin = previous(cursor);
            if (begin < cursor)
            {
                edited.erase(begin, cursor - begin);
                cursor = begin;
            }
        };
        const auto erase_at = [&] {
            const auto end = next(cursor);
            if (end > cursor)
                edited.erase(cursor, end - cursor);
        };

        for (std::size_t i = 0; i < text.size(); ++i)
        {
            const auto byte = static_cast<unsigned char>(text[i]);
            if (byte < 0x20 && byte != 0x1B)
            {
                edited.push_back(static_cast<char>(byte));
                cursor = edited.size();
                continue;
            }
            if (byte == 0x1B && i + 1 < text.size() && text[i + 1] == '[')
            {
                std::size_t end = i + 2;
                while (end < text.size())
                {
                    const auto value = static_cast<unsigned char>(text[end]);
                    if (value >= 0x40 && value <= 0x7E)
                        break;
                    ++end;
                }
                if (end >= text.size())
                    break;
                const char final = text[end];
                unsigned parameter = 0;
                bool has_parameter = false;
                for (std::size_t p = i + 2; p < end; ++p)
                {
                    if (text[p] >= '0' && text[p] <= '9')
                    {
                        parameter = parameter * 10 + static_cast<unsigned>(text[p] - '0');
                        has_parameter = true;
                    }
                    else if (text[p] != ';')
                    {
                        break;
                    }
                }
                switch (final)
                {
                case 'D':
                    cursor = previous(cursor);
                    break;
                case 'C':
                    cursor = next(cursor);
                    break;
                case 'H':
                    cursor = 0;
                    break;
                case 'F':
                    cursor = edited.size();
                    break;
                case '~':
                    if (!has_parameter || parameter == 3)
                        erase_at();
                    else if (parameter == 1 || parameter == 7)
                        cursor = 0;
                    else if (parameter == 2)
                        insert_mode = !insert_mode;
                    else if (parameter == 4 || parameter == 8)
                        cursor = edited.size();
                    break;
                default:
                    break;
                }
                i = end;
                continue;
            }
            if (byte == 0x7F || byte == '\b')
            {
                erase_before();
                continue;
            }
            if (byte >= 0x80 || (byte >= 0x20 && byte != 0x7F))
            {
                if (insert_mode && cursor < edited.size() && byte < 0x80)
                {
                    edited[cursor] = static_cast<char>(byte);
                    cursor = next(cursor);
                }
                else
                {
                    edited.insert(edited.begin() + static_cast<std::ptrdiff_t>(cursor), static_cast<char>(byte));
                    ++cursor;
                }
            }
        }
        text = std::move(edited);
    }

    [[nodiscard]] std::string encode_console_text(std::u16string_view text) const
    {
        const auto code_page = state_.input_code_page != 0 ? state_.input_code_page : CP_UTF8;
        if (text.empty())
            return {};
        const auto size = ::WideCharToMultiByte(code_page, 0, reinterpret_cast<const wchar_t *>(text.data()),
                                                static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
        std::string result(static_cast<std::size_t>(std::max(size, 0)), '\0');
        if (!result.empty())
        {
            (void)::WideCharToMultiByte(code_page, 0, reinterpret_cast<const wchar_t *>(text.data()),
                                        static_cast<int>(text.size()), result.data(), static_cast<int>(result.size()),
                                        nullptr, nullptr);
        }
        return result;
    }

    [[nodiscard]] static std::string encode_utf8(std::u16string_view text)
    {
        if (text.empty())
            return {};
        const auto size = ::WideCharToMultiByte(CP_UTF8, 0, reinterpret_cast<const wchar_t *>(text.data()),
                                                static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
        std::string result(static_cast<std::size_t>(std::max(size, 0)), '\0');
        if (!result.empty())
        {
            (void)::WideCharToMultiByte(CP_UTF8, 0, reinterpret_cast<const wchar_t *>(text.data()),
                                        static_cast<int>(text.size()), result.data(),
                                        static_cast<int>(result.size()), nullptr, nullptr);
        }
        return result;
    }

    [[nodiscard]] static std::string_view terminal_key_sequence(consoleinput::virtual_key key) noexcept
    {
        switch (key)
        {
        case consoleinput::virtual_key::up:
            return "\x1b[A";
        case consoleinput::virtual_key::down:
            return "\x1b[B";
        case consoleinput::virtual_key::right:
            return "\x1b[C";
        case consoleinput::virtual_key::left:
            return "\x1b[D";
        case consoleinput::virtual_key::home:
            return "\x1b[H";
        case consoleinput::virtual_key::end:
            return "\x1b[F";
        case consoleinput::virtual_key::insert:
            return "\x1b[2~";
        case consoleinput::virtual_key::del:
            return "\x1b[3~";
        case consoleinput::virtual_key::page_up:
            return "\x1b[5~";
        case consoleinput::virtual_key::page_down:
            return "\x1b[6~";
        case consoleinput::virtual_key::f1:
            return "\x1bOP";
        case consoleinput::virtual_key::f2:
            return "\x1bOQ";
        case consoleinput::virtual_key::f3:
            return "\x1bOR";
        case consoleinput::virtual_key::f4:
            return "\x1bOS";
        case consoleinput::virtual_key::f5:
            return "\x1b[15~";
        case consoleinput::virtual_key::f6:
            return "\x1b[17~";
        case consoleinput::virtual_key::f7:
            return "\x1b[18~";
        case consoleinput::virtual_key::f8:
            return "\x1b[19~";
        case consoleinput::virtual_key::f9:
            return "\x1b[20~";
        case consoleinput::virtual_key::f10:
            return "\x1b[21~";
        case consoleinput::virtual_key::f11:
            return "\x1b[23~";
        case consoleinput::virtual_key::f12:
            return "\x1b[24~";
        default:
            return {};
        }
    }

    void consume_raw_text(std::u16string_view text)
    {
        if (!raw_mode_ || text.empty())
            return;
        const bool line_input = (state_.input_mode & ENABLE_LINE_INPUT) != 0;
        const auto bytes = encode_console_text(text);
        raw_in_.append(bytes);
        note_raw_bytes(std::span<const char>{bytes.data(), bytes.size()});
        if (line_input)
        {
            const auto echo = encode_utf8(text);
            echo_live(std::span<const char>{echo.data(), echo.size()});
        }
        signal_input_event();
    }

    void process_raw_input_events()
    {
        if (!raw_mode_)
            return;

        const bool line_input = (state_.input_mode & ENABLE_LINE_INPUT) != 0;
        std::array<consoleinput::input_event, 128> events{};
        for (;;)
        {
            const auto count = input_.read(events);
            if (count == 0)
                break;

            std::string bytes;
            std::string echo;
            std::u16string text_batch;
            bool edited = false;
            const auto flush_text = [&] {
                if (text_batch.empty())
                    return;
                bytes.append(encode_console_text(text_batch));
                if (line_input)
                    echo.append(encode_utf8(text_batch));
                text_batch.clear();
            };
            for (std::size_t i = 0; i < count; ++i)
            {
                const auto &event = events[i];
                if (event.type != consoleinput::input_event_type::key || !event.key.key_down)
                    continue;

                if (event.key.character != 0)
                {
                    const auto character = event.key.character;
                    if (line_input && (character == u'\r' || character == u'\n'))
                    {
                        flush_text();
                        bytes.push_back('\r');
                        echo.push_back('\r');
                    }
                    else if (line_input && (character == u'\b' || character == u'\x7F'))
                    {
                        flush_text();
                        bytes.push_back('\x7F');
                        edited = true;
                    }
                    else
                    {
                        text_batch.push_back(character);
                    }
                    continue;
                }

                flush_text();
                const auto sequence = terminal_key_sequence(event.key.key);
                if (!sequence.empty())
                {
                    bytes.append(sequence);
                    if (event.key.key == consoleinput::virtual_key::left ||
                        event.key.key == consoleinput::virtual_key::right ||
                        event.key.key == consoleinput::virtual_key::home ||
                        event.key.key == consoleinput::virtual_key::end ||
                        event.key.key == consoleinput::virtual_key::insert ||
                        event.key.key == consoleinput::virtual_key::del)
                    {
                        edited = true;
                    }
                }
            }
            flush_text();

            if (!bytes.empty())
            {
                raw_in_.append(bytes);
                note_raw_bytes(std::span<const char>{bytes.data(), bytes.size()});
            }
            if (line_input)
            {
                if (edited || line_edited_)
                {
                    line_edited_ = true;
                }
                else if (!echo.empty())
                {
                    echo_live(echo);
                }
            }
        }
        signal_input_event();
    }

    void echo_live(std::span<const char> bytes)
    {
        if (!writer_ || !raw_mode_ || bytes.empty() || (state_.input_mode & ENABLE_LINE_INPUT) == 0 ||
            (state_.input_mode & ENABLE_ECHO_INPUT) == 0)
        {
            return;
        }
        for (const auto value : bytes)
        {
            const auto byte = static_cast<unsigned char>(value);
            if (byte == '\b' || byte == 0x7F || byte == 0x1B)
            {
                line_edited_ = true;
                return;
            }
        }
        if (!line_start_valid_)
        {
            const auto cursor = state_.cursor();
            line_start_x_ = cursor.x;
            line_start_y_ = cursor.y;
            line_start_valid_ = true;
        }
        if (line_edited_)
        {
            for (const auto value : bytes)
            {
                const auto byte = static_cast<unsigned char>(value);
                if (byte == '\r' || byte == '\n')
                    return;
            }
        }
        echo_bytes_.clear();
        for (std::size_t i = 0; i < bytes.size(); ++i)
        {
            const auto byte = static_cast<unsigned char>(bytes[i]);
            if (byte == '\r')
            {
                echo_bytes_.append("\r\n", 2);
                if (i + 1 < bytes.size() && bytes[i + 1] == '\n')
                    ++i;
            }
            else if (byte == '\n')
            {
                echo_bytes_.append("\r\n", 2);
            }
            else
            {
                echo_bytes_.push_back(bytes[i]);
            }
        }
        echo_decoder_.decode(
            std::span<const char8_t>{reinterpret_cast<const char8_t *>(echo_bytes_.data()), echo_bytes_.size()},
            echo_text_);
        (void)write_text(echo_text_);
        screen_.clear_damage();
        (void)state_.take_cursor_changed();
        (void)state_.take_echo_line_feed();
        writer_->buffer().append(
            std::span<const char8_t>{reinterpret_cast<const char8_t *>(echo_bytes_.data()), echo_bytes_.size()});
        live_echoed_ = true;
        if (!line_edited_ && echo_bytes_.find("\r\n") != std::string::npos)
        {
            line_start_valid_ = false;
        }
    }

    void echo_raw(std::span<const char> bytes)
    {
        if (live_echoed_)
        {
            if (line_edited_ && line_start_valid_)
            {
                state_.set_cursor(static_cast<std::size_t>(line_start_x_), static_cast<std::size_t>(line_start_y_));
                for (auto x = line_start_x_; x < static_cast<int>(screen_.width()); ++x)
                {
                    (void)screen_.write_ascii(static_cast<std::size_t>(x), static_cast<std::size_t>(line_start_y_), u" ",
                                              state_.default_style, screen::no_cell_hyperlink);
                }
                screen_.clear_damage();
                (void)state_.take_cursor_changed();
                writer_->buffer().append_text(u8"\r");
                if (line_start_x_ != 0)
                {
                    const auto move = "\x1b[" + std::to_string(line_start_x_) + "C";
                    writer_->buffer().append(std::span<const char8_t>{
                        reinterpret_cast<const char8_t *>(move.data()), move.size()});
                }
                writer_->buffer().append_text(u8"\x1b[K");
                live_echoed_ = false;
                line_edited_ = false;
                line_start_valid_ = false;
            }
            else
            {
                if (raw_line_remaining_ == 0)
                {
                    live_echoed_ = false;
                    line_start_valid_ = false;
                    line_edited_ = false;
                }
                return;
            }
        }
        if (!writer_ || bytes.empty() || (state_.input_mode & ENABLE_ECHO_INPUT) == 0)
        {
            return;
        }
        // ECHO must update both the terminal output and the console screen
        // buffer. Apply the text to the screen state, then emit the original
        // bytes and drop the generated damage so a later screen API redraw does
        // not discard the echoed text and the terminal does not see a duplicate
        // screen render.
        echo_decoder_.decode(
            std::span<const char8_t>{reinterpret_cast<const char8_t *>(bytes.data()), bytes.size()}, echo_text_);
        (void)write_text(echo_text_);
        screen_.clear_damage();
        (void)state_.take_cursor_changed();
        (void)state_.take_echo_line_feed();
        writer_->buffer().append(
            std::span<const char8_t>{reinterpret_cast<const char8_t *>(bytes.data()), bytes.size()});
        if ((state_.input_mode & ENABLE_LINE_INPUT) != 0 && raw_line_remaining_ == 0)
        {
            live_echoed_ = false;
            line_edited_ = false;
            line_start_valid_ = false;
        }
    }

    std::size_t read_raw(std::span<char> output)
    {
        raw_mode_ = true;
        process_raw_input_events();
        if (output.empty() || raw_in_.empty())
        {
            return 0;
        }

        const bool line_input = (state_.input_mode & ENABLE_LINE_INPUT) != 0;
        if (line_input && raw_line_remaining_ == 0)
        {
            // Editing is stateful; apply it only once a complete line is in the
            // raw buffer, not on every intermediate input chunk.
            if (!raw_line_complete_)
            {
                return 0;
            }
            apply_line_edits(raw_in_);
            if (raw_in_.empty())
            {
                raw_line_complete_ = false;
                return 0;
            }
        }

        if (line_input && raw_line_remaining_ != 0)
        {
            const auto written = std::min(output.size(), raw_line_remaining_);
            std::memcpy(output.data(), raw_in_.data(), written);
            raw_in_.erase(0, written);
            raw_line_remaining_ -= written;
            if (raw_line_remaining_ == 0)
                raw_line_complete_ = false;
            return written;
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
            if (line_input || raw_in_.size() < output.size())
            {
                return 0;
            }
            consumed = limit;
        }

        if (line_input && consumed > output.size())
        {
            const auto written = output.size();
            std::memcpy(output.data(), raw_in_.data(), written);
            raw_in_.erase(0, written);
            raw_line_remaining_ = consumed - written;
            return written;
        }

        const bool add_cr =
            consumed != 0 && raw_in_[consumed - 1] == '\n' && (consumed < 2 || raw_in_[consumed - 2] != '\r');
        const bool add_lf = consumed != 0 && raw_in_[consumed - 1] == '\r' && (eof || complete);

        if (!add_cr && !add_lf)
        {
            std::memcpy(output.data(), raw_in_.data(), consumed);
            raw_in_.erase(0, consumed);
            if (line_input && raw_in_.empty())
                raw_line_complete_ = false;
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
        if (line_input && raw_in_.empty())
            raw_line_complete_ = false;
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

    void set_output_handle(std::uintptr_t handle) noexcept
    {
        api_.set_output_handle(handle);
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
        raw_mode_ = false;
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
            if (written != 0)
            {
                echo_raw(std::span<const char>{reinterpret_cast<const char *>(pending_message_.body), written});
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
    std::u16string echo_text_;
    std::string echo_bytes_;
    vtparser::utf8_decoder echo_decoder_;
    std::string raw_in_;
    bool raw_mode_{};
    bool live_echoed_{};
    bool raw_line_complete_{};
    bool line_edited_{};
    bool line_start_valid_{};
    int line_start_x_{};
    int line_start_y_{};
    std::size_t raw_line_remaining_{};
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