#include "screen_buffer.hpp"

#include <algorithm>
#include <stdexcept>
#include <unordered_map>
#include <utility>

namespace corehost::screen
{

namespace
{
using cluster_remap = std::unordered_map<cluster_offset, grapheme_store::ref>;

bool remap_cell(cell &value, const grapheme_store &source, grapheme_store &destination, cluster_remap &map)
{
    if (!value.has_cluster())
    {
        return true;
    }

    const auto found = map.find(value.offset());
    if (found != map.end())
    {
        value = value.with_cluster_ref(found->second.offset, found->second.length);
        return true;
    }

    const auto source_view = source.view(grapheme_store::ref{value.offset(), value.length()});
    if (source_view.empty() || !destination.can_append(source_view.size()))
    {
        return false;
    }

    const auto ref = destination.append(source_view);
    map.emplace(value.offset(), ref);
    value = value.with_cluster_ref(ref.offset, ref.length);
    return true;
}

bool remap_page(page &target, const grapheme_store &source, grapheme_store &destination, cluster_remap &map)
{
    for (std::size_t y = 0; y < target.height(); ++y)
    {
        for (auto &value : target.row_cells(y))
        {
            if (!remap_cell(value, source, destination, map))
            {
                return false;
            }
        }
    }
    return true;
}

bool remap_line(std::vector<cell> &line, const grapheme_store &source, grapheme_store &destination, cluster_remap &map)
{
    for (auto &value : line)
    {
        if (!remap_cell(value, source, destination, map))
        {
            return false;
        }
    }
    return true;
}
} // namespace

page::page(std::size_t width, std::size_t height, cell_style fill_style)
    : width_{width}, height_{height}, cells_(width * height, cell::blank(fill_style)), rows_(height)
{
    mark_all_dirty();
}

void page::mark_dirty(std::size_t y, std::size_t begin, std::size_t end) noexcept
{
    if (y >= rows_.size() || begin >= end)
    {
        return;
    }

    begin = std::min(begin, width_);
    end = std::min(end, width_);
    if (begin >= end)
    {
        return;
    }

    auto &state = rows_[y];
    if (!state.is_dirty())
    {
        state.dirty_begin = begin;
        state.dirty_end = end;
        return;
    }

    state.dirty_begin = std::min(state.dirty_begin, begin);
    state.dirty_end = std::max(state.dirty_end, end);
}

void page::mark_used(std::size_t y, std::size_t end) noexcept
{
    if (y < rows_.size())
    {
        rows_[y].used_columns = std::max(rows_[y].used_columns, std::min(end, width_));
    }
}

void page::clear_damage() noexcept
{
    for (auto &state : rows_)
    {
        state.dirty_begin = 0;
        state.dirty_end = 0;
    }
}

void page::mark_all_dirty() noexcept
{
    for (auto &state : rows_)
    {
        state.dirty_begin = 0;
        state.dirty_end = width_;
    }
}

screen_buffer::screen_buffer(std::size_t width, std::size_t height, std::size_t history_limit)
    : width_{width}, height_{height}, history_limit_{history_limit}, main_(width, height)
{
    if (width == 0 || height == 0)
    {
        throw std::invalid_argument("screen_buffer dimensions must be non-zero");
    }
}

cell screen_buffer::at(std::size_t x, std::size_t y) const noexcept
{
    return active_page().at(x, y);
}

std::span<cell> screen_buffer::row_cells(std::size_t y) noexcept
{
    return active_page().row_cells(y);
}

std::span<const cell> screen_buffer::row_cells(std::size_t y) const noexcept
{
    return active_page().row_cells(y);
}

row_state &screen_buffer::row(std::size_t y) noexcept
{
    return active_page().row(y);
}

const row_state &screen_buffer::row(std::size_t y) const noexcept
{
    return active_page().row(y);
}

void screen_buffer::clear_wide_neighborhood(page &target, std::size_t x, std::size_t y)
{
    const auto old = target.at(x, y);
    if (old.is_continuation() && x > 0)
    {
        const auto &head = target.at(x - 1, y);
        target.at(x - 1, y) = cell::blank(head.style(), head.hyperlink());
        target.mark_dirty(y, x - 1, x);
    }
    else if (old.is_wide() && x + 1 < width_ && target.at(x + 1, y).is_continuation())
    {
        target.at(x + 1, y) = cell::blank(old.style(), old.hyperlink());
        target.mark_dirty(y, x + 1, x + 2);
    }
}

bool screen_buffer::write_cell(std::size_t x, std::size_t y, cell value)
{
    if (x >= width_ || y >= height_)
    {
        return false;
    }

    if (value.is_wide() && x + 1 >= width_)
    {
        return false;
    }

    auto &target = active_page();
    clear_wide_neighborhood(target, x, y);
    if (value.is_wide())
    {
        clear_wide_neighborhood(target, x + 1, y);
    }

    target.at(x, y) = value;
    target.mark_dirty(y, x, x + 1);
    target.mark_used(y, x + 1);

    if (value.is_wide())
    {
        target.at(x + 1, y) = cell::tail(value.style(), value.hyperlink());
        target.mark_dirty(y, x + 1, x + 2);
        target.mark_used(y, x + 2);
    }

    return true;
}

bool screen_buffer::write_scalar(std::size_t x, std::size_t y, char16_t primary, cell_style style, cell_info info,
                                 cell_hyperlink hyperlink)
{
    if (cell_info_ops::is_width_tail(info))
    {
        return write_cell(x, y, cell::tail(style, hyperlink));
    }

    return write_cell(x, y, cell::scalar(primary, style, info, hyperlink));
}

bool screen_buffer::write_cluster(std::size_t x, std::size_t y, std::span<const char16_t> cluster, cell_style style,
                                  cell_info info, cell_hyperlink hyperlink)
{
    if (x >= width_ || y >= height_ || cluster.empty() || !graphemes_.can_append(cluster.size()))
    {
        return false;
    }

    if (cell_info_ops::is_width_wide(info) && x + 1 >= width_)
    {
        return false;
    }

    const auto ref = graphemes_.append(cluster);
    return write_cell(x, y, cell::complex(ref.offset, ref.length, style, info, hyperlink));
}

void screen_buffer::fill_rect(rectangle area, cell value)
{
    const auto x_end = std::min(width_, area.x + area.width);
    const auto y_end = std::min(height_, area.y + area.height);
    if (area.x >= x_end || area.y >= y_end)
    {
        return;
    }

    auto &target = active_page();
    for (auto y = area.y; y < y_end; ++y)
    {
        for (auto x = area.x; x < x_end; ++x)
        {
            target.at(x, y) = value;
        }
        target.mark_dirty(y, area.x, x_end);
        target.mark_used(y, x_end);
    }
}

void screen_buffer::clear(cell_style style)
{
    fill_rect(rectangle{0, 0, width_, height_}, cell::blank(style));
}

void screen_buffer::scroll_up(std::size_t lines, cell_style fill_style)
{
    if (lines == 0 || height_ == 0)
    {
        return;
    }

    auto &target = active_page();
    lines = std::min(lines, height_);

    if (!alternate_active_ && history_limit_ != 0)
    {
        for (std::size_t y = 0; y < lines; ++y)
        {
            history_.emplace_back(target.row_cells(y).begin(), target.row_cells(y).end());
        }
        trim_history();
    }

    for (std::size_t y = 0; y + lines < height_; ++y)
    {
        std::copy(target.row_cells(y + lines).begin(), target.row_cells(y + lines).end(), target.row_cells(y).begin());
        target.row(y).used_columns = target.row(y + lines).used_columns;
    }

    for (std::size_t y = height_ - lines; y < height_; ++y)
    {
        for (auto &value : target.row_cells(y))
        {
            value = cell::blank(fill_style);
        }
        target.row(y).used_columns = 0;
    }

    target.mark_all_dirty();
}

void screen_buffer::scroll_region_up(rectangle area, std::size_t lines, cell_style fill_style)
{
    const auto x_end = std::min(width_, area.x + area.width);
    const auto y_end = std::min(height_, area.y + area.height);
    if (area.x >= x_end || area.y >= y_end || lines == 0)
    {
        return;
    }

    const auto region_height = y_end - area.y;
    lines = std::min(lines, region_height);
    auto &target = active_page();

    for (std::size_t y = area.y; y + lines < y_end; ++y)
    {
        std::copy(target.row_cells(y + lines).begin() + static_cast<std::ptrdiff_t>(area.x),
                  target.row_cells(y + lines).begin() + static_cast<std::ptrdiff_t>(x_end),
                  target.row_cells(y).begin() + static_cast<std::ptrdiff_t>(area.x));
    }

    for (std::size_t y = y_end - lines; y < y_end; ++y)
    {
        for (auto x = area.x; x < x_end; ++x)
        {
            target.at(x, y) = cell::blank(fill_style);
        }
    }

    for (std::size_t y = area.y; y < y_end; ++y)
    {
        target.mark_dirty(y, area.x, x_end);
        target.mark_used(y, x_end);
    }
}

void screen_buffer::resize(std::size_t width, std::size_t height, cell_style fill_style)
{
    if (width == 0 || height == 0)
    {
        throw std::invalid_argument("screen_buffer dimensions must be non-zero");
    }

    page resized_main(width, height, fill_style);
    const auto copy_width = std::min(width, width_);
    const auto copy_height = std::min(height, height_);
    for (std::size_t y = 0; y < copy_height; ++y)
    {
        std::copy_n(main_.row_cells(y).begin(), copy_width, resized_main.row_cells(y).begin());
        resized_main.row(y).used_columns = std::min(main_.row(y).used_columns, width);
    }

    if (alternate_)
    {
        page resized_alt(width, height, fill_style);
        for (std::size_t y = 0; y < copy_height; ++y)
        {
            std::copy_n(alternate_->row_cells(y).begin(), copy_width, resized_alt.row_cells(y).begin());
            resized_alt.row(y).used_columns = std::min(alternate_->row(y).used_columns, width);
        }
        *alternate_ = std::move(resized_alt);
    }

    main_ = std::move(resized_main);
    width_ = width;
    height_ = height;
    clear_history();
}

void screen_buffer::set_alternate(bool enabled)
{
    if (enabled && !alternate_)
    {
        alternate_.emplace(width_, height_, default_cell_style);
    }
    alternate_active_ = enabled;
}

void screen_buffer::clear_history()
{
    history_.clear();
}

std::span<const cell> screen_buffer::history_line(std::size_t index) const
{
    if (index >= history_.size())
    {
        return {};
    }
    return std::span<const cell>{history_[index]};
}

void screen_buffer::trim_history()
{
    while (history_.size() > history_limit_)
    {
        history_.pop_front();
    }
}

std::span<const char16_t> screen_buffer::cluster_of(const cell &value) const noexcept
{
    if (!value.has_cluster())
    {
        return {};
    }
    return graphemes_.view(grapheme_store::ref{value.offset(), value.length()});
}

bool screen_buffer::rebuild_graphemes()
{
    grapheme_store rebuilt;
    cluster_remap map;
    map.reserve(graphemes_.size() / 4 + 1);

    if (!remap_page(main_, graphemes_, rebuilt, map))
    {
        return false;
    }

    if (alternate_ && !remap_page(*alternate_, graphemes_, rebuilt, map))
    {
        return false;
    }

    for (auto &line : history_)
    {
        if (!remap_line(line, graphemes_, rebuilt, map))
        {
            return false;
        }
    }

    graphemes_ = std::move(rebuilt);
    return true;
}

std::size_t screen_buffer::damage_begin(std::size_t y) const noexcept
{
    return active_page().row(y).dirty_begin;
}

std::size_t screen_buffer::damage_end(std::size_t y) const noexcept
{
    return active_page().row(y).dirty_end;
}

void screen_buffer::clear_damage() noexcept
{
    active_page().clear_damage();
}

page &screen_buffer::active_page() noexcept
{
    if (alternate_active_ && alternate_)
    {
        return *alternate_;
    }
    return main_;
}

const page &screen_buffer::active_page() const noexcept
{
    if (alternate_active_ && alternate_)
    {
        return *alternate_;
    }
    return main_;
}

} // namespace corehost::screen
