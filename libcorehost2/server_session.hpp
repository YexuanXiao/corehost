#pragma once

#include "console_api.hpp"
#include "line_editor.hpp"
#include "terminal_writer.hpp"
#include "viewport.hpp"
#include "render_state.hpp"
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

#include <algorithm>
#include <array>
#include <bit>
#include <cstring>
#include <deque>
#include <memory>
#include <string>
#include <string_view>

namespace corehost::corehost2
{

[[nodiscard]] inline bool has_lone_line_feed(std::u16string_view text) noexcept
{
    for (std::size_t i = 0; i < text.size(); ++i)
    {
        if (text[i] == u'\n' && (i == 0 || text[i - 1] != u'\r'))
            return true;
    }
    return false;
}

inline void normalize_line_feeds_into(std::u16string_view text, std::u16string &normalized)
{
    normalized.clear();
    normalized.reserve(text.size() + 8);
    for (std::size_t i = 0; i < text.size(); ++i)
    {
        if (text[i] == u'\n' && (i == 0 || text[i - 1] != u'\r'))
            normalized.push_back(u'\r');
        normalized.push_back(text[i]);
    }
}

[[nodiscard]] inline std::u16string normalize_line_feeds(std::u16string_view text)
{
    std::u16string normalized;
    normalize_line_feeds_into(text, normalized);
    return normalized;
}

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

    // The direct path writes text without renderer output. It is only valid
    // when no style transition must be serialized and terminal wrapping agrees
    // with the screen buffer.
    [[nodiscard]] bool can_stream_direct_output() const noexcept
    {
        return writer_ != nullptr && (state_.output_mode & 0x0001) != 0 &&
               state_.default_style == state_.plain_style;
    }

    void decode_console_text_into(std::span<const char> text, std::u16string &result) const
    {
        result.clear();
        if (text.empty())
            return;
        const auto code_page = state_.input_code_page != 0 ? state_.input_code_page : CP_UTF8;
        const auto count = ::MultiByteToWideChar(code_page, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
        result.resize(static_cast<std::size_t>(std::max(count, 0)), u'\0');
        if (!result.empty())
        {
            (void)::MultiByteToWideChar(code_page, 0, text.data(), static_cast<int>(text.size()),
                                        reinterpret_cast<LPWSTR>(result.data()), static_cast<int>(result.size()));
        }
    }

    void encode_console_text_into(std::u16string_view text, std::string &result) const
    {
        result.clear();
        if (text.empty())
            return;
        const auto code_page = state_.input_code_page != 0 ? state_.input_code_page : CP_UTF8;
        const auto size = ::WideCharToMultiByte(code_page, 0, reinterpret_cast<const wchar_t *>(text.data()),
                                                static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
        result.resize(static_cast<std::size_t>(std::max(size, 0)), '\0');
        if (!result.empty())
        {
            (void)::WideCharToMultiByte(code_page, 0, reinterpret_cast<const wchar_t *>(text.data()),
                                        static_cast<int>(text.size()), result.data(), static_cast<int>(result.size()),
                                        nullptr, nullptr);
        }
    }

    static void encode_utf8_into(std::u16string_view text, std::string &result)
    {
        result.clear();
        if (text.empty())
            return;
        const auto size = ::WideCharToMultiByte(CP_UTF8, 0, reinterpret_cast<const wchar_t *>(text.data()),
                                                static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
        result.resize(static_cast<std::size_t>(std::max(size, 0)), '\0');
        if (!result.empty())
        {
            (void)::WideCharToMultiByte(CP_UTF8, 0, reinterpret_cast<const wchar_t *>(text.data()),
                                        static_cast<int>(text.size()), result.data(),
                                        static_cast<int>(result.size()), nullptr, nullptr);
        }
    }

    [[nodiscard]] std::string encode_console_text(std::u16string_view text) const
    {
        std::string result;
        encode_console_text_into(text, result);
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

    [[nodiscard]] static std::string encode_mouse_event(const consoleinput::mouse_event &mouse)
    {
        unsigned button = 3;
        if ((mouse.button_state & FROM_LEFT_1ST_BUTTON_PRESSED) != 0)
            button = 0;
        else if ((mouse.button_state & FROM_LEFT_2ND_BUTTON_PRESSED) != 0)
            button = 1;
        else if ((mouse.button_state & RIGHTMOST_BUTTON_PRESSED) != 0)
            button = 2;

        unsigned code = button;
        char final = 'M';
        if ((mouse.event_flags & MOUSE_WHEELED) != 0)
        {
            const auto delta = static_cast<std::int16_t>((mouse.button_state >> 16) & 0xFFFF);
            code = 64 + (delta < 0 ? 1 : 0);
        }
        else if ((mouse.event_flags & MOUSE_HWHEELED) != 0)
        {
            const auto delta = static_cast<std::int16_t>((mouse.button_state >> 16) & 0xFFFF);
            code = 128 + (delta < 0 ? 1 : 0);
        }
        else if ((mouse.event_flags & MOUSE_MOVED) != 0)
        {
            code += 32;
        }
        else if (mouse.button_state == 0)
        {
            final = 'm';
        }

        std::string result;
        result.reserve(24);
        result.append("\x1b[<");
        result.append(std::to_string(code));
        result.push_back(';');
        result.append(std::to_string(mouse.x + 1));
        result.push_back(';');
        result.append(std::to_string(mouse.y + 1));
        result.push_back(final);
        return result;
    }

    void process_raw_input_events()
    {
        if (!raw_mode_)
            return;

        const bool line_input = (state_.input_mode & ENABLE_LINE_INPUT) != 0;
        std::array<consoleinput::input_event, 128> events{};
        for (;;)
        {
            const auto count = input_.read_raw(events);
            if (count == 0)
                break;

            std::string bytes;
            std::u16string text_batch;
            const auto flush_text = [&] {
                if (text_batch.empty())
                    return;
                encode_console_text_into(text_batch, console_scratch_);
                bytes.append(console_scratch_);
                text_batch.clear();
            };
            for (std::size_t i = 0; i < count; ++i)
            {
                const auto &event = events[i];
                if (event.type == consoleinput::input_event_type::text)
                {
                    if (!event.text || event.text->empty())
                        continue;
                    if (line_input)
                    {
                        feed_line_text(*event.text);
                        continue;
                    }
                    encode_console_text_into(*event.text, console_scratch_);
                    raw_in_.append(console_scratch_);
                    continue;
                }
                if (event.type == consoleinput::input_event_type::mouse)
                {
                    if (!line_input && state_.mouse_tracking())
                    {
                        flush_text();
                        bytes.append(encode_mouse_event(event.mouse));
                    }
                    continue;
                }
                if (event.type != consoleinput::input_event_type::key || !event.key.key_down)
                    continue;

                if (line_input)
                {
                    feed_line_key(event.key);
                    continue;
                }

                if (event.key.character != 0)
                {
                    text_batch.push_back(event.key.character);
                    continue;
                }

                flush_text();
                const auto sequence = terminal_key_sequence(event.key.key);
                if (!sequence.empty())
                    bytes.append(sequence);
            }
            flush_text();

            if (!bytes.empty())
                raw_in_.append(bytes);
        }
        signal_input_event();
    }

    void echo_editor_append(std::u16string_view text)
    {
        encode_utf8_into(text, utf8_scratch_);
        echo_live(std::span<const char>{utf8_scratch_.data(), utf8_scratch_.size()});
    }

    [[nodiscard]] static screen::coord echo_position(screen::coord origin, std::int32_t width,
                                                    std::size_t index) noexcept
    {
        const auto first = static_cast<std::size_t>(std::max(width - origin.x, 1));
        if (index < first)
            return screen::coord{origin.x + static_cast<std::int32_t>(index), origin.y};
        const auto rest = index - first;
        return screen::coord{static_cast<std::int32_t>(rest % static_cast<std::size_t>(width)),
                             origin.y + 1 + static_cast<std::int32_t>(rest / static_cast<std::size_t>(width))};
    }

    void redraw_line_echo()
    {
        if (!writer_ || (state_.input_mode & ENABLE_ECHO_INPUT) == 0)
            return;
        if (!line_start_valid_)
        {
            const auto cursor = state_.cursor();
            line_start_x_ = cursor.x;
            line_start_y_ = cursor.y;
            line_start_valid_ = true;
        }
        if (line_start_x_ < 0 || line_start_y_ < 0 ||
            static_cast<std::size_t>(line_start_x_) >= screen_.width() ||
            static_cast<std::size_t>(line_start_y_) >= screen_.height())
        {
            return;
        }

        const auto text = raw_line_editor_.text();
        const auto origin = screen::coord{line_start_x_, line_start_y_};
        const auto text_columns = static_cast<std::size_t>(
            std::max(screen::text_width_for(state_.measurement, text, state_.ambiguous_is_wide), 0));
        const auto old_length = std::max(line_echo_length_, text_columns);
        for (std::size_t i = 0; i < old_length; ++i)
        {
            const auto position = echo_position(origin, static_cast<std::int32_t>(screen_.width()), i);
            if (position.x < 0 || position.y < 0 ||
                static_cast<std::size_t>(position.x) >= screen_.width() ||
                static_cast<std::size_t>(position.y) >= screen_.height())
            {
                continue;
            }
            (void)screen_.write_ascii(static_cast<std::size_t>(position.x),
                                      static_cast<std::size_t>(position.y), u" ", state_.default_style,
                                      screen::no_cell_hyperlink);
        }

        state_.set_cursor(static_cast<std::size_t>(line_start_x_), static_cast<std::size_t>(line_start_y_));
        (void)write_text(text);
        line_echo_length_ = text_columns;
        const auto cursor_units = std::min(raw_line_editor_.cursor(), text.size());
        const auto cursor_columns = static_cast<std::size_t>(std::max(
            screen::text_width_for(state_.measurement, text.substr(0, cursor_units), state_.ambiguous_is_wide), 0));
        const auto cursor = echo_position(origin, static_cast<std::int32_t>(screen_.width()), cursor_columns);
        state_.set_cursor(static_cast<std::size_t>(std::max(cursor.x, 0)),
                          static_cast<std::size_t>(std::max(cursor.y, 0)));
        render_pending_screen(screen_, state_, writer_->buffer());
        (void)state_.take_echo_line_feed();
        writer_->flush();
        line_edited_ = true;
        live_echoed_ = true;
    }

    void complete_line_editor()
    {
        auto line = encode_console_text(raw_line_editor_.text());
        line.append("\r\n", 2);
        raw_completed_lines_.push_back(std::move(line));

        const bool needs_redraw = line_edited_ || !live_echoed_;
        raw_line_editor_.consume_completed_line();
        if (needs_redraw && (state_.input_mode & ENABLE_ECHO_INPUT) != 0)
        {
            line_edited_ = true;
            echo_raw(std::span<const char>{raw_completed_lines_.back().data(),
                                            raw_completed_lines_.back().size()});
        }
        else
        {
            line_edited_ = false;
            line_start_valid_ = false;
            if ((state_.input_mode & ENABLE_ECHO_INPUT) != 0)
            {
                constexpr char8_t newline[] = u8"\r";
                echo_live(std::span<const char>{reinterpret_cast<const char *>(newline), 1});
            }
        }
    }

    void feed_line_text(std::u16string_view text)
    {
        std::u16string batch;
        batch.reserve(text.size());
        const auto flush = [&] {
            if (batch.empty())
                return;
            const auto change = raw_line_editor_.append(batch);
            if (change == consoleinput::line_editor::change::append)
                echo_editor_append(batch);
            else if (change == consoleinput::line_editor::change::redraw)
                redraw_line_echo();
            batch.clear();
        };
        for (const auto unit : text)
        {
            if (unit == u'\r' || unit == u'\n')
            {
                flush();
                complete_line_editor();
            }
            else if (unit == u'\b' || unit == u'\x7F')
            {
                flush();
                if (raw_line_editor_.backspace() != consoleinput::line_editor::change::none)
                    redraw_line_echo();
            }
            else
            {
                batch.push_back(unit);
            }
        }
        flush();
    }

    void feed_line_key(const consoleinput::key_event &key)
    {
        if (key.character != 0)
        {
            const auto unit = key.character;
            if ((state_.input_mode & ENABLE_PROCESSED_INPUT) != 0 && unit == u'\x03')
            {
                raw_line_editor_.reset();
                line_edited_ = false;
                line_start_valid_ = false;
                raw_alerted_ = true;
                return;
            }
            if ((state_.input_mode & ENABLE_PROCESSED_INPUT) != 0 && unit == u'\x1A')
            {
                raw_line_editor_.reset();
                line_edited_ = false;
                line_start_valid_ = false;
                raw_eof_ = true;
                return;
            }
            if (unit == u'\r' || unit == u'\n')
            {
                complete_line_editor();
            }
            else if (unit == u'\b' || unit == u'\x7F')
            {
                if (raw_line_editor_.backspace() != consoleinput::line_editor::change::none)
                    redraw_line_echo();
            }
            else
            {
                const auto change = raw_line_editor_.insert(unit);
                if (change == consoleinput::line_editor::change::append)
                    echo_editor_append(std::u16string_view{&unit, 1});
                else
                    redraw_line_echo();
            }
            return;
        }

        auto change = consoleinput::line_editor::change::none;
        switch (key.key)
        {
        case consoleinput::virtual_key::left:
            change = raw_line_editor_.left();
            break;
        case consoleinput::virtual_key::right:
            change = raw_line_editor_.right();
            break;
        case consoleinput::virtual_key::home:
            change = raw_line_editor_.home();
            break;
        case consoleinput::virtual_key::end:
            change = raw_line_editor_.end();
            break;
        case consoleinput::virtual_key::insert:
            change = raw_line_editor_.toggle_insert();
            break;
        case consoleinput::virtual_key::del:
            change = raw_line_editor_.del();
            break;
        default:
            break;
        }
        if (change != consoleinput::line_editor::change::none)
            redraw_line_echo();
    }

    void echo_live(std::span<const char> bytes)
    {
        if (!writer_ || !raw_mode_ || bytes.empty() || (state_.input_mode & ENABLE_LINE_INPUT) == 0 ||
            (state_.input_mode & ENABLE_ECHO_INPUT) == 0)
        {
            return;
        }
        if (!line_start_valid_)
        {
            const auto cursor = state_.cursor();
            line_start_x_ = cursor.x;
            line_start_y_ = cursor.y;
            line_start_valid_ = true;
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
        commit_direct_output(screen_, state_);
        (void)state_.take_echo_line_feed();
        writer_->buffer().append(
            std::span<const char8_t>{reinterpret_cast<const char8_t *>(echo_bytes_.data()), echo_bytes_.size()});
        live_echoed_ = true;
        if (echo_bytes_.find("\r\n") != std::string::npos)
        {
            line_start_valid_ = false;
            line_echo_length_ = 0;
        }
        else
        {
            line_echo_length_ = static_cast<std::size_t>(std::max(
                screen::text_width_for(state_.measurement, raw_line_editor_.text(), state_.ambiguous_is_wide), 0));
        }
    }

    void echo_raw(std::span<const char> bytes)
    {
        if (live_echoed_)
        {
            if (line_edited_ && line_start_valid_)
            {
                const auto origin = screen::coord{line_start_x_, line_start_y_};
                const auto clear_columns = std::max<std::size_t>(line_echo_length_, 1);
                for (std::size_t i = 0; i < clear_columns; ++i)
                {
                    const auto position =
                        echo_position(origin, static_cast<std::int32_t>(screen_.width()), i);
                    if (position.x < 0 || position.y < 0 ||
                        static_cast<std::size_t>(position.x) >= screen_.width() ||
                        static_cast<std::size_t>(position.y) >= screen_.height())
                    {
                        continue;
                    }
                    (void)screen_.write_ascii(static_cast<std::size_t>(position.x),
                                              static_cast<std::size_t>(position.y), u" ", state_.default_style,
                                              screen::no_cell_hyperlink);
                }
                state_.set_cursor(static_cast<std::size_t>(line_start_x_), static_cast<std::size_t>(line_start_y_));
                live_echoed_ = false;
                line_edited_ = false;
                line_start_valid_ = false;
            }
            else
            {
                live_echoed_ = false;
                line_start_valid_ = false;
                line_edited_ = false;
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
        if ((state_.input_mode & ENABLE_LINE_INPUT) != 0)
        {
            decode_console_text_into(bytes, decoded_text_);
            const auto &decoded = decoded_text_;
            echo_text_.clear();
            echo_text_.reserve(decoded.size() + 2);
            for (std::size_t i = 0; i < decoded.size(); ++i)
            {
                const auto unit = decoded[i];
                if (unit == u'\r')
                {
                    echo_text_.append(u"\r\n", 2);
                    if (i + 1 < decoded.size() && decoded[i + 1] == u'\n')
                        ++i;
                }
                else if (unit == u'\n')
                {
                    echo_text_.append(u"\r\n", 2);
                }
                else
                {
                    echo_text_.push_back(unit);
                }
            }
            (void)write_text(echo_text_);
            render_pending_screen(screen_, state_, writer_->buffer());
            (void)state_.take_echo_line_feed();
        }
        else
        {
            echo_decoder_.decode(
                std::span<const char8_t>{reinterpret_cast<const char8_t *>(bytes.data()), bytes.size()}, echo_text_);
            (void)write_text(echo_text_);
            commit_direct_output(screen_, state_);
            (void)state_.take_echo_line_feed();
            writer_->buffer().append(
                std::span<const char8_t>{reinterpret_cast<const char8_t *>(bytes.data()), bytes.size()});
        }
        if ((state_.input_mode & ENABLE_LINE_INPUT) != 0)
        {
            live_echoed_ = false;
            line_edited_ = false;
            line_start_valid_ = false;
            line_echo_length_ = 0;
        }
        // The client is completed immediately after this call. Flush the echo
        // first so client output cannot be interleaved before the echoed line.
        if (writer_)
            writer_->flush();
    }

    std::size_t read_raw(std::span<char> output)
    {
        raw_mode_ = true;
        process_raw_input_events();
        if (output.empty())
            return 0;
        if ((state_.input_mode & ENABLE_LINE_INPUT) != 0)
        {
            if (raw_completed_lines_.empty())
                return 0;
            auto &line = raw_completed_lines_.front();
            const auto written = std::min(output.size(), line.size() - raw_completed_offset_);
            std::memcpy(output.data(), line.data() + raw_completed_offset_, written);
            raw_completed_offset_ += written;
            if (raw_completed_offset_ == line.size())
            {
                raw_completed_lines_.pop_front();
                raw_completed_offset_ = 0;
            }
            return written;
        }
        if (raw_in_.empty())
            return 0;

        if ((state_.input_mode & ENABLE_LINE_INPUT) != 0)
            return 0;
        const auto limit = std::min<std::size_t>(output.size(), raw_in_.size());
        std::size_t consumed = 0;
        bool complete = false;
        bool eof = false;

        const bool processed_input = (state_.input_mode & 0x0001) != 0;
        const auto found = processed_input ? find_raw_control<true>(raw_in_, limit)
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
        std::u16string_view written_text;
        const auto write_cursor = state_.cursor();
        if (request->Unicode)
        {
            const auto *data = reinterpret_cast<const char16_t *>(payload.data());
            const auto count = payload.size() / sizeof(char16_t);
            written_text = std::u16string_view{data, count};
            written = write_text(written_text);
            request->NumBytes = static_cast<ULONG>(written * sizeof(char16_t));
            written_text = written_text.substr(0, written);
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
            written_text = std::u16string_view{text_.data(), written};
            const auto encoded = ::WideCharToMultiByte(code_page, 0, reinterpret_cast<const wchar_t *>(text_.data()),
                                                       static_cast<int>(written), nullptr, 0, nullptr, nullptr);
            request->NumBytes = static_cast<ULONG>(std::max(encoded, 0));
        }
        if (can_stream_direct_output())
        {
            const auto origin = screen_.viewport_origin();
            const auto target_x = static_cast<std::int32_t>(write_cursor.x) - origin.x;
            const auto target_y = static_cast<std::int32_t>(write_cursor.y) - origin.y;
            if (!state_.terminal_cursor_at(target_x, target_y))
            {
                if (state_.terminal_cursor_on_row(target_y) && target_x == 0)
                {
                    writer_->buffer().append_text(u8"\r");
                }
                else
                {
                    vtserializer::append_cup(writer_->buffer(), static_cast<unsigned>(std::max(target_y, 0)),
                                             static_cast<unsigned>(std::max(target_x, 0)));
                }
            }
            const bool has_lone_lf = has_lone_line_feed(written_text);
            if (has_lone_lf)
                normalize_line_feeds_into(written_text, direct_text_);
            encode_utf8_into(has_lone_lf ? std::u16string_view{direct_text_} : written_text, direct_utf8_);
            writer_->buffer().append(std::span<const char8_t>{
                reinterpret_cast<const char8_t *>(direct_utf8_.data()), direct_utf8_.size()});
            commit_direct_output(screen_, state_);
            (void)state_.take_echo_line_feed();
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
            render_full_viewport(screen_, state_, writer_->buffer());
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
            const bool editor_ready = (state_.input_mode & ENABLE_LINE_INPUT) != 0 &&
                                      !raw_completed_lines_.empty();
            if (raw_in_.empty() && !editor_ready)
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
            if (written != 0 && (state_.input_mode & ENABLE_LINE_INPUT) == 0)
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
    std::u16string decoded_text_;
    std::u16string direct_text_;
    std::string echo_bytes_;
    std::string console_scratch_;
    std::string utf8_scratch_;
    std::string direct_utf8_;
    vtparser::utf8_decoder echo_decoder_;
    std::string raw_in_;
    consoleinput::line_editor raw_line_editor_;
    std::deque<std::string> raw_completed_lines_;
    std::size_t raw_completed_offset_{};
    bool raw_mode_{};
    bool live_echoed_{};
    bool line_edited_{};
    bool line_start_valid_{};
    std::size_t line_echo_length_{};
    int line_start_x_{};
    int line_start_y_{};
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