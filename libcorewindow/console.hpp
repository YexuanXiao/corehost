#pragma once

#include "console_state.hpp"
#include "input.hpp"
#include "screen_buffer.hpp"
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

    bool copy_updates(std::vector<screen::row_block> &rows)
    {
        rows.clear();
        if (!screen_.has_damage())
        {
            return false;
        }

        rows.resize(screen_.height());
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

  private:
    screen::screen_buffer screen_;
    screen::console_state state_;
    consoleinput::input_queue input_;
    win32::event input_event_{win32::create_tag, true, false};
};

} // namespace corehost::window
