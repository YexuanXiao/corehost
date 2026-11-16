#pragma once

#include "cell.hpp"
#include "deque.hpp"
#include "screen_buffer.hpp"

#include <Windows.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>
#include <vector>
#include <bitset>

namespace corehost::screen
{

// Mirrors libcorehost's console_state constructor: the default console code
// page is UTF-8 when the build opts into it, otherwise the system OEM code page.
[[nodiscard]] inline std::uint32_t initial_console_code_page()
{
#ifdef COREHOST_DEFAULT_UTF8
    return CP_UTF8;
#elif defined(COREHOST_RUN_AS_UTF8)
    wchar_t buffer[6]{};
    DWORD size = sizeof(buffer);
    const auto result = ::RegGetValueW(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Control\\Nls\\CodePage",
                                       L"OEMCP", RRF_RT_REG_SZ, nullptr, buffer, &size);
    if (result != ERROR_SUCCESS || size < 2 || size > sizeof(buffer))
    {
        return CP_UTF8;
    }
    char narrow[6]{};
    const auto length = size / sizeof(wchar_t) - 1;
    for (std::size_t index = 0; index < length; ++index)
    {
        if (buffer[index] > 0xFF)
        {
            return CP_UTF8;
        }
        narrow[index] = static_cast<char>(buffer[index]);
    }
    UINT code_page = CP_UTF8;
    const auto parsed = std::from_chars(narrow, narrow + length, code_page);
    if (parsed.ec != std::errc{})
    {
        return CP_UTF8;
    }
    return code_page;
#else
    return ::GetOEMCP();
#endif
}

enum class cursor_style : std::uint8_t
{
    block = 0,
    underline = 1,
    bar = 2,
};

struct cursor_state final
{
    std::size_t x{};
    std::size_t y{};
    bool visible{true};
    bool blinking{true};
    std::uint32_t size{25};
    cursor_style style{cursor_style::block};
};

class tab_stop_table final
{
  public:
    void assign(std::size_t columns, bool value);
    void resize(std::size_t columns, bool value);
    [[nodiscard]] std::size_t size() const noexcept;
    [[nodiscard]] bool operator[](std::size_t column) const noexcept;
    void set(std::size_t column, bool value) noexcept;
    void clear() noexcept;

  private:
    std::vector<std::uint8_t> values_;
};

enum class selection_mode : std::uint8_t
{
    block = 0,
    line = 1,
};

struct selection_range final
{
    bool active{};
    selection_mode mode{selection_mode::block};
    std::size_t anchor_x{};
    std::size_t anchor_y{};
    std::size_t caret_x{};
    std::size_t caret_y{};

    [[nodiscard]] bool contains(std::size_t x, std::size_t y) const noexcept;
};

// Console-level state shared by VT and Console API paths. This deliberately
// remains separate from page/cell storage: cursor, tabs, margins, selection and
// search matches are screen state, not persistent cell content.
class console_state final
{
  public:
    console_state(std::size_t width, std::size_t height);

    [[nodiscard]] std::size_t width() const noexcept
    {
        return width_;
    }

    [[nodiscard]] std::size_t height() const noexcept
    {
        return height_;
    }

    void resize(std::size_t width, std::size_t height);

    [[nodiscard]] cursor_state &cursor() noexcept
    {
        return cursor_;
    }

    [[nodiscard]] const cursor_state &cursor() const noexcept
    {
        return cursor_;
    }

    void set_cursor(std::size_t x, std::size_t y) noexcept;
    void move_cursor(std::ptrdiff_t dx, std::ptrdiff_t dy) noexcept;
    void clamp_cursor() noexcept;
    void save_cursor() noexcept;
    [[nodiscard]] bool restore_cursor() noexcept;
    void save_alt_cursor() noexcept;
    [[nodiscard]] bool restore_alt_cursor() noexcept;

    bool wrap_at_eol{true};
    bool insert_mode{false};
    bool origin_mode{false};
    bool dec_line_drawing_mode{false};
    cell_style default_style{default_cell_style};
    cell_hyperlink default_hyperlink{no_cell_hyperlink};
    std::uint16_t default_attributes{0x07};
    std::uint16_t popup_attributes{0x07};

    // Console-mode state kept platform-neutral; libinterop converts to WORD/DWORD.
    std::uint32_t input_mode{};
    std::uint32_t output_mode{};
    std::uint32_t input_private_mode{};
    std::uint32_t input_code_page{};
    std::uint16_t language_id{0x0409};
    std::uint32_t history_buffer_size{50};
    std::uint32_t history_buffers{4};
    std::uint32_t history_flags{};
    struct alias_entry
    {
        std::u16string source;
        std::u16string target;
        std::u16string executable;
    };
    std::vector<alias_entry> aliases;
    bizwen::deque<std::u16string> command_history;
    std::size_t history_browse_index{no_history_selection};
    std::u16string history_saved_input;
    std::size_t command_history_limit{50};
    std::uint32_t display_mode{};
    bool line_drawing_charset{};
    bool keypad_application{};
    bool keypad_changed{};
    bool alternate_changed{};
    bool title_changed{};
    bool scrollback_cleared{};
    bool cursor_changed{};
    bool cursor_shape_changed{};
    bool viewport_changed{};
    bool win32_input_mode{true};
    bool win32_input_changed{};
    bool echo_line_feed{};
    std::bitset<2048> private_modes;
    std::vector<std::pair<std::uint16_t, bool>> private_mode_changes;
    std::uint32_t output_code_page{};
    std::uint32_t font_index{};
    coord font_size{8, 12};
    std::uint32_t font_family{};
    std::uint32_t font_weight{400};
    std::u16string face_name{u"Consolas"};
    std::array<std::uint32_t, 16> color_table{};
    std::u16string title;
    std::u16string original_title;
    coord max_window_size{};
    text_measurement measurement{text_measurement::console};
    bool ambiguous_is_wide{false};

    struct saved_cursor_state final
    {
        bool valid{};
        cursor_state cursor;
        cell_style style{default_cell_style};
    };

    saved_cursor_state saved_cursor;
    saved_cursor_state alt_saved_cursor;

    [[nodiscard]] std::size_t scroll_top() const noexcept
    {
        return scroll_top_;
    }

    [[nodiscard]] std::size_t scroll_bottom() const noexcept
    {
        return scroll_bottom_;
    }

    void set_scroll_region(std::size_t top, std::size_t bottom);
    void reset_scroll_region() noexcept;
    [[nodiscard]] bool in_scroll_region(std::size_t y) const noexcept;

    [[nodiscard]] tab_stop_table &tabs() noexcept
    {
        return tabs_;
    }

    [[nodiscard]] const tab_stop_table &tabs() const noexcept
    {
        return tabs_;
    }

    void reset_tab_stops();
    [[nodiscard]] std::size_t next_tab_stop(std::size_t column) const noexcept;
    [[nodiscard]] std::size_t previous_tab_stop(std::size_t column) const noexcept;

    [[nodiscard]] const selection_range &selection() const noexcept
    {
        return selection_;
    }

    void set_selection(std::size_t anchor_x, std::size_t anchor_y, std::size_t caret_x, std::size_t caret_y) noexcept;
    void set_selection(std::size_t anchor_x, std::size_t anchor_y, std::size_t caret_x, std::size_t caret_y,
                       selection_mode mode) noexcept;
    [[nodiscard]] std::u16string selected_text(const screen_buffer &buffer) const;
    void set_keypad_application(bool value) noexcept
    {
        if (keypad_application == value)
        {
            return;
        }
        keypad_application = value;
        keypad_changed = true;
    }

    [[nodiscard]] bool take_keypad_changed() noexcept
    {
        const auto changed = keypad_changed;
        keypad_changed = false;
        return changed;
    }

    [[nodiscard]] bool mouse_tracking() const noexcept
    {
        return private_modes.test(1000) || private_modes.test(1002) || private_modes.test(1003);
    }

    void set_private_mode(std::uint16_t mode, bool enabled)
    {
        if (mode >= private_modes.size() || private_modes.test(mode) == enabled)
        {
            return;
        }
        private_modes.set(mode, enabled);
        private_mode_changes.emplace_back(mode, enabled);
    }

    [[nodiscard]] std::vector<std::pair<std::uint16_t, bool>> take_private_modes()
    {
        return std::move(private_mode_changes);
    }

    static constexpr std::size_t no_history_selection = static_cast<std::size_t>(-1);

    [[nodiscard]] bool take_cursor_changed() noexcept
    {
        const auto changed = cursor_changed;
        cursor_changed = false;
        return changed;
    }

    [[nodiscard]] bool take_cursor_shape_changed() noexcept
    {
        const auto changed = cursor_shape_changed;
        cursor_shape_changed = false;
        return changed;
    }

    [[nodiscard]] bool take_echo_line_feed() noexcept
    {
        const auto changed = echo_line_feed;
        echo_line_feed = false;
        return changed;
    }

    [[nodiscard]] bool take_win32_input_change(bool &enabled) noexcept
    {
        if (!win32_input_changed)
        {
            return false;
        }
        win32_input_changed = false;
        enabled = win32_input_mode;
        return true;
    }

    [[nodiscard]] bool take_viewport_changed() noexcept
    {
        const auto changed = viewport_changed;
        viewport_changed = false;
        return changed;
    }

    [[nodiscard]] bool take_scrollback_cleared() noexcept
    {
        const auto cleared = scrollback_cleared;
        scrollback_cleared = false;
        return cleared;
    }

    [[nodiscard]] bool take_title_changed() noexcept
    {
        const auto changed = title_changed;
        title_changed = false;
        return changed;
    }

    void add_command(std::u16string_view command);
    void reset_history_browse() noexcept
    {
        history_browse_index = no_history_selection;
        history_saved_input.clear();
    }

    void break_history_browse() noexcept
    {
        if (history_browse_index != no_history_selection)
        {
            reset_history_browse();
        }
    }

    [[nodiscard]] bool browse_history_up(std::u16string_view current_input, std::u16string &selected)
    {
        if (command_history.empty())
        {
            return false;
        }
        if (history_browse_index == no_history_selection)
        {
            history_saved_input.assign(current_input);
            history_browse_index = command_history.size() - 1;
        }
        else if (history_browse_index != 0)
        {
            --history_browse_index;
        }
        selected.assign(command_history[history_browse_index]);
        return true;
    }

    [[nodiscard]] bool browse_history_down(std::u16string &selected)
    {
        if (history_browse_index == no_history_selection)
        {
            return false;
        }
        if (history_browse_index + 1 < command_history.size())
        {
            ++history_browse_index;
            selected.assign(command_history[history_browse_index]);
        }
        else
        {
            selected.assign(history_saved_input);
            reset_history_browse();
        }
        return true;
    }
    void expunge_commands() noexcept
    {
        command_history.clear();
    }

    void set_command_limit(std::size_t limit)
    {
        command_history_limit = limit;
        while (command_history.size() > limit)
        {
            command_history.pop_front();
        }
    }

    [[nodiscard]] std::size_t command_history_length() const noexcept
    {
        std::size_t length = 0;
        for (const auto &command : command_history)
        {
            length += command.size() + 1;
        }
        return length;
    }

    void clear_selection() noexcept;
    [[nodiscard]] bool has_selection() const noexcept;
    [[nodiscard]] bool is_selected(std::size_t x, std::size_t y) const noexcept;

    void set_search_matches(std::vector<rectangle> matches);
    void clear_search_matches() noexcept;
    [[nodiscard]] std::span<const rectangle> search_matches() const noexcept;
    [[nodiscard]] bool is_search_match(std::size_t x, std::size_t y) const noexcept;

  private:
    std::size_t width_{};
    std::size_t height_{};
    cursor_state cursor_;
    std::size_t scroll_top_{};
    std::size_t scroll_bottom_{};
    tab_stop_table tabs_;
    selection_range selection_;
    std::vector<rectangle> search_matches_;
};

inline void tab_stop_table::assign(std::size_t columns, bool value)
{
    values_.assign(columns, static_cast<std::uint8_t>(value ? 1 : 0));
}

inline void tab_stop_table::resize(std::size_t columns, bool value)
{
    values_.resize(columns, static_cast<std::uint8_t>(value ? 1 : 0));
}

inline std::size_t tab_stop_table::size() const noexcept
{
    return values_.size();
}

inline bool tab_stop_table::operator[](std::size_t column) const noexcept
{
    assert(column < values_.size());
    return values_[column] != 0;
}

inline void tab_stop_table::set(std::size_t column, bool value) noexcept
{
    assert(column < values_.size());
    values_[column] = static_cast<std::uint8_t>(value ? 1 : 0);
}

inline void tab_stop_table::clear() noexcept
{
    std::fill(values_.begin(), values_.end(), std::uint8_t{0});
}

inline bool selection_range::contains(std::size_t x, std::size_t y) const noexcept
{
    if (!active)
    {
        return false;
    }

    const auto left = std::min(anchor_x, caret_x);
    const auto right = std::max(anchor_x, caret_x);
    const auto top = std::min(anchor_y, caret_y);
    const auto bottom = std::max(anchor_y, caret_y);
    if (mode == selection_mode::line)
    {
        return y >= top && y <= bottom;
    }
    return x >= left && x <= right && y >= top && y <= bottom;
}

inline console_state::console_state(std::size_t width, std::size_t height) : width_{width}, height_{height}
{
    assert(width != 0 && height != 0);

    input_code_page = initial_console_code_page();
    output_code_page = input_code_page;
    scroll_bottom_ = height_ - 1;
    max_window_size = coord{static_cast<std::int32_t>(width_), static_cast<std::int32_t>(height_)};
    input_mode = 0x0001 | 0x0002 | 0x0004 | 0x0010;
    input_private_mode = 0x0020 | 0x0040 | 0x0100;
    output_mode = 0x0001 | 0x0002;
    reset_tab_stops();
}

inline void console_state::resize(std::size_t width, std::size_t height)
{
    assert(width != 0 && height != 0);

    width_ = width;
    height_ = height;
    cursor_.x = std::min(cursor_.x, width_ - 1);
    cursor_.y = std::min(cursor_.y, height_ - 1);
    tabs_.resize(width_, false);
    max_window_size = coord{static_cast<std::int32_t>(width_), static_cast<std::int32_t>(height_)};
    reset_scroll_region();
    clear_selection();
    clear_search_matches();
}

inline void console_state::set_cursor(std::size_t x, std::size_t y) noexcept
{
    const auto next_x = std::min(x, width_ - 1);
    const auto next_y = std::min(y, height_ - 1);
    if (next_x != cursor_.x || next_y != cursor_.y)
    {
        cursor_changed = true;
    }
    cursor_.x = next_x;
    cursor_.y = next_y;
}

inline void console_state::move_cursor(std::ptrdiff_t dx, std::ptrdiff_t dy) noexcept
{
    const auto next_x = static_cast<std::ptrdiff_t>(cursor_.x) + dx;
    const auto next_y = static_cast<std::ptrdiff_t>(cursor_.y) + dy;
    const auto clamped_x =
        std::min<std::size_t>(width_ - 1, static_cast<std::size_t>(std::max<std::ptrdiff_t>(0, next_x)));
    const auto clamped_y =
        std::min<std::size_t>(height_ - 1, static_cast<std::size_t>(std::max<std::ptrdiff_t>(0, next_y)));
    if (clamped_x != cursor_.x || clamped_y != cursor_.y)
    {
        cursor_changed = true;
    }
    cursor_.x = clamped_x;
    cursor_.y = clamped_y;
}

inline void console_state::clamp_cursor() noexcept
{
    const auto next_x = std::min(cursor_.x, width_ - 1);
    const auto next_y = std::min(cursor_.y, height_ - 1);
    if (next_x != cursor_.x || next_y != cursor_.y)
    {
        cursor_changed = true;
    }
    cursor_.x = next_x;
    cursor_.y = next_y;
}

inline void console_state::save_cursor() noexcept
{
    saved_cursor.valid = true;
    saved_cursor.cursor = cursor_;
    saved_cursor.style = default_style;
}

inline bool console_state::restore_cursor() noexcept
{
    if (!saved_cursor.valid)
    {
        return false;
    }
    cursor_ = saved_cursor.cursor;
    default_style = saved_cursor.style;
    clamp_cursor();
    return true;
}

inline void console_state::save_alt_cursor() noexcept
{
    alt_saved_cursor.valid = true;
    alt_saved_cursor.cursor = cursor_;
    alt_saved_cursor.style = default_style;
}

inline bool console_state::restore_alt_cursor() noexcept
{
    if (!alt_saved_cursor.valid)
    {
        return false;
    }
    cursor_ = alt_saved_cursor.cursor;
    default_style = alt_saved_cursor.style;
    clamp_cursor();
    return true;
}

inline void console_state::set_scroll_region(std::size_t top, std::size_t bottom)
{
    assert(top < height_ && bottom < height_ && top <= bottom);

    scroll_top_ = top;
    scroll_bottom_ = bottom;
}

inline void console_state::reset_scroll_region() noexcept
{
    scroll_top_ = 0;
    scroll_bottom_ = height_ - 1;
}

inline bool console_state::in_scroll_region(std::size_t y) const noexcept
{
    return y >= scroll_top_ && y <= scroll_bottom_;
}

inline void console_state::reset_tab_stops()
{
    tabs_.assign(width_, false);
    for (std::size_t column = 0; column < width_; column += 8)
    {
        tabs_.set(column, true);
    }
}

inline std::size_t console_state::next_tab_stop(std::size_t column) const noexcept
{
    for (auto next = column + 1; next < width_; ++next)
    {
        if (tabs_[next])
        {
            return next;
        }
    }
    return column;
}

inline std::size_t console_state::previous_tab_stop(std::size_t column) const noexcept
{
    for (auto next = std::min(column, width_); next > 0; --next)
    {
        if (tabs_[next - 1])
        {
            return next - 1;
        }
    }
    return 0;
}

inline void console_state::set_selection(std::size_t anchor_x, std::size_t anchor_y, std::size_t caret_x,
                                         std::size_t caret_y) noexcept
{
    set_selection(anchor_x, anchor_y, caret_x, caret_y, selection_mode::block);
}

inline void console_state::set_selection(std::size_t anchor_x, std::size_t anchor_y, std::size_t caret_x,
                                         std::size_t caret_y, selection_mode mode) noexcept
{
    selection_.active = true;
    selection_.mode = mode;
    selection_.anchor_x = std::min(anchor_x, width_ - 1);
    selection_.anchor_y = std::min(anchor_y, height_ - 1);
    selection_.caret_x = std::min(caret_x, width_ - 1);
    selection_.caret_y = std::min(caret_y, height_ - 1);
}

inline std::u16string console_state::selected_text(const screen_buffer &buffer) const
{
    if (!selection_.active)
    {
        return {};
    }

    const auto left = std::min(selection_.anchor_x, selection_.caret_x);
    const auto right = std::max(selection_.anchor_x, selection_.caret_x);
    const auto top = std::min(selection_.anchor_y, selection_.caret_y);
    const auto bottom = std::max(selection_.anchor_y, selection_.caret_y);
    std::u16string result;

    for (auto y = top; y <= bottom; ++y)
    {
        if (y >= buffer.height())
        {
            break;
        }
        if (selection_.mode == selection_mode::line)
        {
            auto line = buffer.line_text(y);
            while (!line.empty() && line.back() == u' ')
            {
                line.pop_back();
            }
            result.append(line);
        }
        else
        {
            for (auto x = left; x <= right && x < buffer.width(); ++x)
            {
                if (!buffer.is_trailing(x, y))
                {
                    result.append(buffer.glyph_at(x, y));
                }
            }
        }
        if (y != bottom)
        {
            result.push_back(u'\n');
        }
    }
    return result;
}

inline void console_state::add_command(std::u16string_view command)
{
    if (command.empty())
    {
        return;
    }
    command_history.emplace_back(command);
    if (command_history.size() > command_history_limit)
    {
        command_history.pop_front();
    }
}

inline void console_state::clear_selection() noexcept
{
    selection_ = {};
}

inline bool console_state::has_selection() const noexcept
{
    return selection_.active;
}

inline bool console_state::is_selected(std::size_t x, std::size_t y) const noexcept
{
    return selection_.contains(x, y);
}

inline void console_state::set_search_matches(std::vector<rectangle> matches)
{
    search_matches_ = std::move(matches);
}

inline void console_state::clear_search_matches() noexcept
{
    search_matches_.clear();
}

inline std::span<const rectangle> console_state::search_matches() const noexcept
{
    return search_matches_;
}

inline bool console_state::is_search_match(std::size_t x, std::size_t y) const noexcept
{
    for (const auto &match : search_matches_)
    {
        if (x >= match.x && x < match.x + match.width && y >= match.y && y < match.y + match.height)
        {
            return true;
        }
    }
    return false;
}

} // namespace corehost::screen
