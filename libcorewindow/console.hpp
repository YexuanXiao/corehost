#pragma once

#include "console_state.hpp"
#include "input.hpp"
#include "screen_buffer.hpp"
#include "screen_updates.hpp"
#include "win32/event.hpp"

#include <Windows.h>

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
    console(std::size_t width, std::size_t height) : screen_{width, height}, state_{width, height}
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

    void push_input(consoleinput::input_event event)
    {
        input_.push(std::move(event));
        input_event_.set();
    }

    bool copy_updates(std::vector<screen_row_update> &updates)
    {
        updates.clear();
        if (!screen_.has_damage())
        {
            return false;
        }

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

            screen_row_update row{};
            row.y = y;
            row.begin = begin;
            row.end = end;
            row.cells.reserve(end - begin);

            for (std::size_t x = begin; x < end; ++x)
            {
                std::u16string glyph;
                screen_.glyph_at_into(x, y, glyph);
                const auto offset = row.text.size();
                row.text.append(glyph);
                const auto &value = screen_.at(x, y);
                row.cells.push_back(screen_cell_update{
                    .style = value.style(),
                    .width = value.width(),
                    .flags = value.flags(),
                    .hyperlink = value.hyperlink(),
                    .text_offset = offset,
                    .text_length = glyph.size(),
                });
            }

            updates.push_back(std::move(row));
        }

        screen_.clear_damage();
        return !updates.empty();
    }

  private:
    screen::screen_buffer screen_;
    screen::console_state state_;
    consoleinput::input_queue input_;
    win32::event input_event_{win32::create_tag, true, false};
};

} // namespace corehost::window
