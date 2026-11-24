#pragma once

#include "console_state.hpp"
#include "input.hpp"
#include "screen_buffer.hpp"
#include "win32/event.hpp"

#include <Windows.h>

#include <algorithm>
#include <cassert>
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
        state_.set_scroll_offset(std::min(state_.scroll_offset(), screen_.history_size()));
    }

    [[nodiscard]] std::size_t scroll_offset() const noexcept
    {
        return state_.scroll_offset();
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

    void scroll_by(std::ptrdiff_t lines) noexcept
    {
        const auto history = screen_.history_size();
        const auto current = static_cast<std::ptrdiff_t>(std::min(state_.scroll_offset(), history));
        const auto next = std::clamp<std::ptrdiff_t>(current + lines, 0, static_cast<std::ptrdiff_t>(history));
        state_.set_scroll_offset(static_cast<std::size_t>(next));
    }

    void scroll_to_bottom() noexcept
    {
        state_.set_scroll_offset(0);
    }

    void scroll_to_top() noexcept
    {
        state_.set_scroll_offset(screen_.history_size());
    }

    void snap_to_bottom_if_unlocked() noexcept
    {
        if (!state_.scroll_locked())
        {
            scroll_to_bottom();
        }
    }

    void copy_scrolled_rows(std::size_t offset, std::vector<screen::row_block> &rows, screen::style_table &styles)
    {
        rows.clear();
        styles = screen_.styles();
        const auto height = screen_.height();
        const auto width = screen_.width();
        rows.reserve(height);

        if (screen_.alternate_active() || offset == 0)
        {
            rows.resize(height);
            for (std::size_t y = 0; y < height; ++y)
            {
                rows[y] = screen_.block(y);
            }
            return;
        }

        const auto history = screen_.history_size();
        offset = std::min(offset, history);
        const auto top = history - offset;
        for (std::size_t y = 0; y < height; ++y)
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

  private:
    screen::screen_buffer screen_;
    screen::console_state state_;
    consoleinput::input_queue input_;
    bool input_read_{};
    bool focused_{true};
    bool scrollbar_hovered_{};
    win32::event input_event_{win32::create_tag, true, false};
};

} // namespace corehost::window
