#pragma once

#include "console_state.hpp"
#include "input.hpp"
#include "libunicode/convert.h"
#include "libunicode/word_segmenter.h"
#include "screen_buffer.hpp"
#include "win32/event.hpp"

#include <Windows.h>

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace corehost::window
{

// Whole-console shared state. One SRW lock covers screen, console state and
// input queue. The console IO thread owns screen writes; the render thread
// takes the same console lock only to copy damage before rendering.
class console final
{
  public:
    console(std::size_t width, std::size_t height, std::size_t history_limit = 1000)
        : screen_{width, height, history_limit}, state_{width, height}
    {
    }

    console(const console &) = delete;
    console &operator=(const console &) = delete;

    [[nodiscard]] screen::screen_buffer &buffer() noexcept
    {
        return screen_;
    }

    [[nodiscard]] screen::console_state &state() noexcept
    {
        return state_;
    }

    [[nodiscard]] consoleinput::input_queue &input() noexcept
    {
        return input_;
    }

    [[nodiscard]] win32::event &input_event() noexcept
    {
        return input_event_;
    }

    void set_focused(bool focused) noexcept
    {
        focused_ = focused;
    }

    [[nodiscard]] bool has_focus() const noexcept
    {
        return focused_;
    }

    void resize(std::size_t width, std::size_t height)
    {
        const auto cursor = state_.cursor();
        auto position = screen::coord{static_cast<std::int32_t>(cursor.x), static_cast<std::int32_t>(cursor.y)};
        screen_.resize(width, height, screen::default_cell_style, false, &position);
        state_.resize(width, height);
        state_.cursor().x = static_cast<std::size_t>(position.x);
        state_.cursor().y = static_cast<std::size_t>(position.y);
    }

    void mark_input_read() noexcept
    {
        input_read_ = true;
    }

    [[nodiscard]] bool has_read_input() const noexcept
    {
        return input_read_;
    }

    void push_input(consoleinput::input_event event)
    {
        input_.push(std::move(event));
        input_event_.set();
    }

    bool copy_updates(std::vector<screen::row_block> &rows, screen::style_table &styles)
    {
        rows.clear();
        if (!screen_.has_damage())
        {
            return false;
        }

        rows.resize(screen_.height());
        styles = screen_.styles();
        for (std::size_t y = 0; y < screen_.height(); ++y)
        {
            auto begin = screen_.damage_begin(y);
            auto end = screen_.damage_end(y);
            if (begin >= end)
            {
                continue;
            }

            while (begin > 0 && screen_.at(begin, y).is_continuation())
            {
                --begin;
            }
            if (end > 0 && end < screen_.width() && screen_.at(end - 1, y).is_wide())
            {
                ++end;
            }

            rows[y] = screen_.block(y);
            rows[y].state.dirty_begin = static_cast<std::uint32_t>(begin);
            rows[y].state.dirty_end = static_cast<std::uint32_t>(end);
        }

        screen_.clear_damage();
        return true;
    }

    [[nodiscard]] std::size_t history_size() const noexcept
    {
        return screen_.history_size();
    }

    [[nodiscard]] std::size_t history_limit() const noexcept
    {
        return screen_.history_limit();
    }

    void set_history_limit(std::size_t limit)
    {
        screen_.set_history_limit(limit);
        scroll_pixels_ = std::min(scroll_pixels_, static_cast<double>(screen_.history_size()) *
                                                     static_cast<double>(scroll_row_height()));
        state_.set_scroll_offset(std::min(state_.scroll_offset(), screen_.history_size()));
    }

    [[nodiscard]] std::size_t scroll_offset() const noexcept
    {
        return state_.scroll_offset();
    }

    void set_cell_metrics(float width, float height) noexcept
    {
        cell_width_ = width;
        cell_height_ = height;
    }

    [[nodiscard]] float cell_width() const noexcept
    {
        return cell_width_ > 0.0f ? cell_width_ : 1.0f;
    }

    [[nodiscard]] float cell_height() const noexcept
    {
        return cell_height_ > 0.0f ? cell_height_ : 1.0f;
    }

    [[nodiscard]] std::size_t cell_column(float x, std::size_t max_column) const noexcept
    {
        return std::min<std::size_t>(static_cast<std::size_t>(std::max(0.0f, x) / cell_width()), max_column);
    }

    [[nodiscard]] std::size_t cell_row(float y, std::size_t max_row) const noexcept
    {
        return std::min<std::size_t>(static_cast<std::size_t>(std::max(0.0f, y) / cell_height()), max_row);
    }

    [[nodiscard]] std::size_t snap_selection_column(std::size_t x, std::size_t y) const
    {
        while (x > 0 && cell_at_virtual(y, x).is_continuation())
        {
            --x;
        }
        return x;
    }

    void set_selection(std::size_t anchor_x, std::size_t anchor_y, std::size_t caret_x, std::size_t caret_y,
                       screen::selection_mode mode = screen::selection_mode::block)
    {
        set_view_selection(anchor_x, anchor_y, caret_x, caret_y, mode);
        if (anchor_y >= screen_.history_size() && caret_y >= screen_.history_size())
        {
            state_.set_selection(anchor_x, anchor_y - screen_.history_size(), caret_x,
                                 caret_y - screen_.history_size(), mode);
        }
        else
        {
            state_.clear_selection();
        }
    }

    void clear_selection() noexcept
    {
        view_selection_ = {};
        state_.clear_selection();
    }

    [[nodiscard]] std::size_t virtual_rows() const noexcept
    {
        return screen_.history_size() + screen_.height();
    }

    [[nodiscard]] screen::cell cell_at_virtual(std::size_t y, std::size_t x) const
    {
        if (y < screen_.history_size())
        {
            const auto line = screen_.history_line(y);
            return x < line.size() ? line[x] : screen::cell::blank(screen::default_cell_style);
        }
        return screen_.at(x, y - screen_.history_size());
    }

    [[nodiscard]] std::span<const char16_t> cluster_at_virtual(std::size_t y, const screen::cell &value) const
    {
        if (y < screen_.history_size())
        {
            return screen_.history_cluster_of(y, value);
        }
        return screen_.cluster_of(y - screen_.history_size(), value);
    }

    [[nodiscard]] bool alternate_active() const noexcept
    {
        return screen_.alternate_active();
    }

    [[nodiscard]] bool vt_mouse_tracking() const noexcept
    {
        return state_.mouse_tracking();
    }

    [[nodiscard]] bool mouse_reporting() const noexcept
    {
        return state_.mouse_tracking() || (state_.input_mode & 0x0010) != 0;
    }

    [[nodiscard]] bool has_selection() const noexcept
    {
        return view_selection_.active;
    }

    [[nodiscard]] const screen::selection_range &selection() const noexcept
    {
        return view_selection_;
    }

    void set_view_selection(std::size_t anchor_x, std::size_t anchor_y, std::size_t caret_x, std::size_t caret_y,
                            screen::selection_mode mode)
    {
        view_selection_.active = true;
        view_selection_.mode = mode;
        view_selection_.anchor_x = std::min(anchor_x, screen_.width() - 1);
        view_selection_.caret_x = std::min(caret_x, screen_.width() - 1);
        view_selection_.anchor_y = std::min(anchor_y, virtual_rows() - 1);
        view_selection_.caret_y = std::min(caret_y, virtual_rows() - 1);
    }

    void select_line(std::size_t y) noexcept
    {
        set_view_selection(0, y, screen_.width() - 1, y, screen::selection_mode::line);
    }

    void select_word(std::size_t x, std::size_t y)
    {
        x = std::min(x, screen_.width() - 1);
        y = std::min(y, virtual_rows() - 1);
        if (cell_at_virtual(y, x).is_continuation() && x > 0)
        {
            --x;
        }

        std::u32string text;
        std::vector<std::size_t> cells;
        for (std::size_t column = 0; column < screen_.width(); ++column)
        {
            const auto cell = cell_at_virtual(y, column);
            if (cell.is_continuation())
            {
                continue;
            }
            if (cell.is_blank())
            {
                text.push_back(U' ');
                cells.push_back(column);
                continue;
            }
            const auto cluster = cluster_at_virtual(y, cell);
            const auto converted = unicode::convert_to<char32_t>(
                std::u16string_view{cluster.data(), cluster.size()});
            for (const auto codepoint : converted)
            {
                text.push_back(codepoint);
                cells.push_back(column);
            }
        }

        if (text.empty())
        {
            return;
        }

        unicode::word_segmenter segmenter{std::u32string_view{text}};
        std::size_t offset = 0;
        while (segmenter)
        {
            const auto segment = *segmenter;
            const auto end = offset + segment.size();
            if (end > offset)
            {
                const auto start_cell = cells[offset];
                auto end_cell = cells[end - 1];
                if (cell_at_virtual(y, end_cell).is_wide() && end_cell + 1 < screen_.width())
                {
                    ++end_cell;
                }
                if (x >= start_cell && x <= end_cell)
                {
                    set_view_selection(start_cell, y, end_cell, y, screen::selection_mode::block);
                    return;
                }
            }
            offset = end;
            ++segmenter;
        }
    }

    [[nodiscard]] std::u16string selected_text()
    {
        std::u16string result;
        if (!view_selection_.active)
        {
            return result;
        }

        const auto left = std::min(view_selection_.anchor_x, view_selection_.caret_x);
        const auto right = std::max(view_selection_.anchor_x, view_selection_.caret_x);
        const auto top = std::min(view_selection_.anchor_y, view_selection_.caret_y);
        const auto bottom = std::max(view_selection_.anchor_y, view_selection_.caret_y);
        const auto width = screen_.width();

        for (auto y = top; y <= bottom; ++y)
        {
            if (y > top)
            {
                result.append(u"\r\n");
            }
            if (view_selection_.mode == screen::selection_mode::line)
            {
                std::u16string line;
                for (std::size_t column = 0; column < width; ++column)
                {
                    const auto cell = cell_at_virtual(y, column);
                    if (cell.is_continuation())
                    {
                        continue;
                    }
                    if (cell.is_blank())
                    {
                        line.push_back(u' ');
                        continue;
                    }
                    const auto cluster = cluster_at_virtual(y, cell);
                    line.append(cluster.data(), cluster.size());
                }
                while (!line.empty() && line.back() == u' ')
                {
                    line.pop_back();
                }
                result.append(line);
            }
            else
            {
                for (std::size_t x = left; x <= right && x < width; ++x)
                {
                    const auto cell = cell_at_virtual(y, x);
                    if (cell.is_continuation())
                    {
                        continue;
                    }
                    if (cell.is_blank())
                    {
                        result.push_back(u' ');
                        continue;
                    }
                    const auto cluster = cluster_at_virtual(y, cell);
                    if (cluster.empty())
                    {
                        result.push_back(cell.primary());
                    }
                    else
                    {
                        result.append(cluster.data(), cluster.size());
                    }
                }
            }
        }
        return result;
    }

    [[nodiscard]] double scroll_pixels() const noexcept
    {
        return scroll_pixels_;
    }

    [[nodiscard]] float scroll_row_height() const noexcept
    {
        return scroll_row_height_ > 0.0f ? scroll_row_height_ : 1.0f;
    }

    void set_scroll_row_height(float value) noexcept
    {
        scroll_row_height_ = value;
    }

    [[nodiscard]] bool scroll_locked() const noexcept
    {
        return state_.scroll_locked();
    }

    [[nodiscard]] bool scrollbar_hovered() const noexcept
    {
        return scrollbar_hovered_;
    }

    void set_scrollbar_hovered(bool value) noexcept
    {
        scrollbar_hovered_ = value;
    }

    void toggle_scroll_lock() noexcept
    {
        state_.set_scroll_locked(!state_.scroll_locked());
    }

    void scroll_by(double lines) noexcept
    {
        scroll_by_pixels(lines * static_cast<double>(scroll_row_height()));
    }

    void scroll_by_pixels(double pixels) noexcept
    {
        const auto max_pixels = static_cast<double>(screen_.history_size()) *
                                static_cast<double>(scroll_row_height());
        scroll_pixels_ = std::clamp(scroll_pixels_ + pixels, 0.0, max_pixels);
        state_.set_scroll_offset(static_cast<std::size_t>(scroll_pixels_ /
                                                          static_cast<double>(scroll_row_height())));
    }

    void scroll_to_bottom() noexcept
    {
        scroll_pixels_ = 0.0;
        state_.set_scroll_offset(0);
    }

    void scroll_to_top() noexcept
    {
        scroll_pixels_ = static_cast<double>(screen_.history_size()) *
                         static_cast<double>(scroll_row_height());
        state_.set_scroll_offset(screen_.history_size());
    }

    void snap_to_bottom_if_unlocked() noexcept
    {
        if (!state_.scroll_locked())
        {
            scroll_to_bottom();
        }
    }

    void copy_scrolled_rows(std::size_t offset, std::vector<screen::row_block> &rows, screen::style_table &styles,
                            std::size_t extra_rows = 0)
    {
        rows.clear();
        styles = screen_.styles();
        const auto height = screen_.height();
        const auto width = screen_.width();
        const auto count = height + extra_rows;
        rows.reserve(count);

        if (screen_.alternate_active() || offset == 0)
        {
            rows.resize(count);
            for (std::size_t y = 0; y < count; ++y)
            {
                if (y < height)
                {
                    rows[y] = screen_.block(y);
                }
                else
                {
                    rows.emplace_back(screen::cell_vector(width, screen::cell::blank(screen::default_cell_style)),
                                      screen::row_state{}, screen::grapheme_store{});
                }
            }
            return;
        }

        const auto history = screen_.history_size();
        offset = std::min(offset, history);
        const auto top = history - offset;
        for (std::size_t y = 0; y < count; ++y)
        {
            const auto virtual_row = top + y;
            if (virtual_row < history)
            {
                screen::row_block row;
                const auto line = screen_.history_line(virtual_row);
                row.cells.assign(line.begin(), line.end());
                row.state.used_columns = static_cast<std::uint32_t>(width);
                row.state.flags = screen_.history_flags(virtual_row);
                if (const auto exit_code = screen_.history_command_exit_code(virtual_row))
                {
                    row.state.command_exit_code = *exit_code;
                }
                row.graphemes = screen_.history_graphemes(virtual_row);
                rows.push_back(std::move(row));
            }
            else
            {
                const auto active_row = virtual_row - history;
                if (active_row < height)
                {
                    rows.push_back(screen_.block(active_row));
                }
                else
                {
                    rows.emplace_back(screen::cell_vector(width, screen::cell::blank(screen::default_cell_style)),
                                      screen::row_state{}, screen::grapheme_store{});
                }
            }
        }
    }

    void set_ime_composition(std::u16string_view text)
    {
        ime_composition_.assign(text);
    }

    [[nodiscard]] std::u16string_view ime_composition() const noexcept
    {
        return ime_composition_;
    }

  private:
    screen::screen_buffer screen_;
    std::u16string ime_composition_;
    screen::console_state state_;
    screen::selection_range view_selection_;
    consoleinput::input_queue input_;
    bool input_read_{};
    bool focused_{true};
    bool scrollbar_hovered_{};
    double scroll_pixels_{};
    float scroll_row_height_{1.0f};
    float cell_width_{1.0f};
    float cell_height_{1.0f};
    win32::event input_event_{win32::create_tag, true, false};
};

} // namespace corehost::window
