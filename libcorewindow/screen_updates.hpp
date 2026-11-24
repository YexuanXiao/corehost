#pragma once

#include "console_state.hpp"
#include "input.hpp"
#include "screen_buffer.hpp"
#include "win32/event.hpp"
#include "win32/srwlock.hpp"

#include <Windows.h>

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace corehost::window
{

struct screen_cell_update final
{
    screen::cell_style style{};
    screen::cell_width width{};
    screen::cell_flags flags{};
    screen::cell_hyperlink hyperlink{};
    std::size_t text_offset{};
    std::size_t text_length{};
};

struct screen_row_update final
{
    std::size_t y{};
    std::size_t begin{};
    std::size_t end{};
    std::vector<screen_cell_update> cells;
    std::u16string text;
};

// Shared console screen. The console IO thread is the only writer; the UI
// rendering thread asks for damage when it wants to render.
class console_screen final
{
  public:
    class write_lock final
    {
      public:
        explicit write_lock(console_screen &owner) noexcept : owner_{owner}
        {
            owner_.lock_.lock();
        }

        ~write_lock()
        {
            owner_.lock_.unlock();
        }

        write_lock(const write_lock &) = delete;
        write_lock &operator=(const write_lock &) = delete;

      private:
        console_screen &owner_;
    };

    console_screen(std::size_t width, std::size_t height) : screen_{width, height}, state_{width, height}
    {
    }

    console_screen(const console_screen &) = delete;
    console_screen &operator=(const console_screen &) = delete;

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

    void set_worker(HANDLE worker) noexcept
    {
        worker_ = worker;
    }

    void push_input(consoleinput::input_event event)
    {
        {
            write_lock lock{*this};
            input_.push(std::move(event));
            input_event_.set();
        }

        if (worker_ != nullptr)
        {
            (void)::CancelSynchronousIo(worker_);
        }
    }

    bool copy_updates(std::vector<screen_row_update> &updates)
    {
        write_lock lock{*this};
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
    win32::srwlock lock_;
    screen::screen_buffer screen_;
    screen::console_state state_;
    consoleinput::input_queue input_;
    win32::event input_event_{win32::create_tag, true, false};
    HANDLE worker_{};
};

} // namespace corehost::window
