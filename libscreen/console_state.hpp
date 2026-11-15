#pragma once

#include "cell.hpp"
#include "screen_buffer.hpp"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>
#include <vector>

namespace corehost::screen
{

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

struct selection_range final
{
    bool active{};
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

    bool wrap_at_eol{true};
    bool insert_mode{false};
    bool origin_mode{false};
    cell_style default_style{default_cell_style};
    cell_hyperlink default_hyperlink{no_cell_hyperlink};

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
    return x >= left && x <= right && y >= top && y <= bottom;
}

inline console_state::console_state(std::size_t width, std::size_t height)
    : width_{width},
      height_{height}
{
    assert(width != 0 && height != 0);

    scroll_bottom_ = height_ - 1;
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
    reset_scroll_region();
    clear_selection();
    clear_search_matches();
}

inline void console_state::set_cursor(std::size_t x, std::size_t y) noexcept
{
    cursor_.x = std::min(x, width_ - 1);
    cursor_.y = std::min(y, height_ - 1);
}

inline void console_state::move_cursor(std::ptrdiff_t dx, std::ptrdiff_t dy) noexcept
{
    const auto next_x = static_cast<std::ptrdiff_t>(cursor_.x) + dx;
    const auto next_y = static_cast<std::ptrdiff_t>(cursor_.y) + dy;
    cursor_.x = std::min<std::size_t>(width_ - 1,
                                      static_cast<std::size_t>(std::max<std::ptrdiff_t>(0, next_x)));
    cursor_.y = std::min<std::size_t>(height_ - 1,
                                      static_cast<std::size_t>(std::max<std::ptrdiff_t>(0, next_y)));
}

inline void console_state::clamp_cursor() noexcept
{
    cursor_.x = std::min(cursor_.x, width_ - 1);
    cursor_.y = std::min(cursor_.y, height_ - 1);
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

inline void console_state::set_selection(std::size_t anchor_x,
                                         std::size_t anchor_y,
                                         std::size_t caret_x,
                                         std::size_t caret_y) noexcept
{
    selection_.active = true;
    selection_.anchor_x = std::min(anchor_x, width_ - 1);
    selection_.anchor_y = std::min(anchor_y, height_ - 1);
    selection_.caret_x = std::min(caret_x, width_ - 1);
    selection_.caret_y = std::min(caret_y, height_ - 1);
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
        if (x >= match.x && x < match.x + match.width &&
            y >= match.y && y < match.y + match.height)
        {
            return true;
        }
    }
    return false;
}

} // namespace corehost::screen
