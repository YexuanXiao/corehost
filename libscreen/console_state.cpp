#include "console_state.hpp"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace corehost::screen
{

void tab_stop_table::assign(std::size_t columns, bool value)
{
    values_.assign(columns, static_cast<std::uint8_t>(value ? 1 : 0));
}

void tab_stop_table::resize(std::size_t columns, bool value)
{
    values_.resize(columns, static_cast<std::uint8_t>(value ? 1 : 0));
}

std::size_t tab_stop_table::size() const noexcept
{
    return values_.size();
}

bool tab_stop_table::operator[](std::size_t column) const noexcept
{
    return column < values_.size() && values_[column] != 0;
}

void tab_stop_table::set(std::size_t column, bool value) noexcept
{
    if (column < values_.size())
    {
        values_[column] = static_cast<std::uint8_t>(value ? 1 : 0);
    }
}

void tab_stop_table::clear() noexcept
{
    std::fill(values_.begin(), values_.end(), std::uint8_t{0});
}

bool selection_range::contains(std::size_t x, std::size_t y) const noexcept
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

console_state::console_state(std::size_t width, std::size_t height) : width_{width}, height_{height}
{
    if (width == 0 || height == 0)
    {
        throw std::invalid_argument("console_state dimensions must be non-zero");
    }

    scroll_bottom_ = height_ - 1;
    reset_tab_stops();
}

void console_state::resize(std::size_t width, std::size_t height)
{
    if (width == 0 || height == 0)
    {
        throw std::invalid_argument("console_state dimensions must be non-zero");
    }

    width_ = width;
    height_ = height;
    cursor_.x = std::min(cursor_.x, width_ - 1);
    cursor_.y = std::min(cursor_.y, height_ - 1);
    tabs_.resize(width_, false);
    reset_scroll_region();
    clear_selection();
    clear_search_matches();
}

void console_state::set_cursor(std::size_t x, std::size_t y) noexcept
{
    cursor_.x = std::min(x, width_ - 1);
    cursor_.y = std::min(y, height_ - 1);
}

void console_state::move_cursor(std::ptrdiff_t dx, std::ptrdiff_t dy) noexcept
{
    const auto next_x = static_cast<std::ptrdiff_t>(cursor_.x) + dx;
    const auto next_y = static_cast<std::ptrdiff_t>(cursor_.y) + dy;
    cursor_.x = std::min<std::size_t>(width_ - 1, static_cast<std::size_t>(std::max<std::ptrdiff_t>(0, next_x)));
    cursor_.y = std::min<std::size_t>(height_ - 1, static_cast<std::size_t>(std::max<std::ptrdiff_t>(0, next_y)));
}

void console_state::clamp_cursor() noexcept
{
    cursor_.x = std::min(cursor_.x, width_ - 1);
    cursor_.y = std::min(cursor_.y, height_ - 1);
}

void console_state::set_scroll_region(std::size_t top, std::size_t bottom)
{
    if (top >= height_ || bottom >= height_ || top > bottom)
    {
        throw std::invalid_argument("invalid scroll region");
    }

    scroll_top_ = top;
    scroll_bottom_ = bottom;
}

void console_state::reset_scroll_region() noexcept
{
    scroll_top_ = 0;
    scroll_bottom_ = height_ - 1;
}

bool console_state::in_scroll_region(std::size_t y) const noexcept
{
    return y >= scroll_top_ && y <= scroll_bottom_;
}

void console_state::reset_tab_stops()
{
    tabs_.assign(width_, false);
    for (std::size_t column = 0; column < width_; column += 8)
    {
        tabs_.set(column, true);
    }
}

std::size_t console_state::next_tab_stop(std::size_t column) const noexcept
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

std::size_t console_state::previous_tab_stop(std::size_t column) const noexcept
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

void console_state::set_selection(std::size_t anchor_x, std::size_t anchor_y, std::size_t caret_x,
                                  std::size_t caret_y) noexcept
{
    selection_.active = true;
    selection_.anchor_x = std::min(anchor_x, width_ - 1);
    selection_.anchor_y = std::min(anchor_y, height_ - 1);
    selection_.caret_x = std::min(caret_x, width_ - 1);
    selection_.caret_y = std::min(caret_y, height_ - 1);
}

void console_state::clear_selection() noexcept
{
    selection_ = {};
}

bool console_state::has_selection() const noexcept
{
    return selection_.active;
}

bool console_state::is_selected(std::size_t x, std::size_t y) const noexcept
{
    return selection_.contains(x, y);
}

void console_state::set_search_matches(std::vector<rectangle> matches)
{
    search_matches_ = std::move(matches);
}

void console_state::clear_search_matches() noexcept
{
    search_matches_.clear();
}

std::span<const rectangle> console_state::search_matches() const noexcept
{
    return search_matches_;
}

bool console_state::is_search_match(std::size_t x, std::size_t y) const noexcept
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
