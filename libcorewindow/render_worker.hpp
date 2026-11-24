#pragma once

#include "cell_atlas.hpp"
#include "console.hpp"
#include "d3d12_window_renderer.hpp"
#include "text_engine.hpp"
#include "win32/event.hpp"
#include "win32/srwlock.hpp"
#include "win32/thread.hpp"

#include <Windows.h>

#include <array>
#include <atomic>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace corehost::window
{

// Owns the renderer, text engine and atlas. It samples console damage on
// compositor clock ticks and presents only when something changed.
class render_worker final
{
  public:
    render_worker(console &console, win32::srwlock &console_mutex)
        : console_{console}, console_mutex_{console_mutex}
    {
    }

    ~render_worker()
    {
        stop();
    }

    render_worker(const render_worker &) = delete;
    render_worker &operator=(const render_worker &) = delete;

    void start(HWND window)
    {
        assert(window != nullptr);
        assert(!thread_.valid());
        window_ = window;
        thread_ = win32::basic_thread{&render_worker::thread_entry, this, {.name = L"corehost-window-render"}};
    }

    void stop()
    {
        if (!thread_.valid())
        {
            return;
        }

        stop_.store(true, std::memory_order_relaxed);
        stop_event_.set();
        thread_.join();
        thread_ = {};
    }

  private:
    static DWORD WINAPI thread_entry(LPVOID self)
    {
        static_cast<render_worker *>(self)->run();
        return 0;
    }

    static std::array<uitext::font_fallback_entry, 3> fallback_families()
    {
        return {
            uitext::font_fallback_entry{L"Cascadia Code"},
            uitext::font_fallback_entry{L"Cascadia Mono"},
            uitext::font_fallback_entry{L"Consolas"},
        };
    }

    void apply_screen_updates()
    {
        for (const auto &row : updates_)
        {
            if (row.y >= rows)
            {
                continue;
            }

            auto x = row.begin;
            for (const auto &cell : row.cells)
            {
                if (x >= columns)
                {
                    break;
                }
                const auto index = row.y * columns + x;
                cells_[index] = row.text.substr(cell.text_offset, cell.text_length);
                cell_state_[index] = cell;
                ++x;
            }
        }
    }

    void build_quads()
    {
        quads_.clear();
        const auto metrics = text_.metrics();
        const auto cell_width = metrics.advance;
        const auto cell_height = metrics.ascent + metrics.descent + metrics.line_gap;
        const auto atlas_scale = 1.0f / static_cast<float>(atlas_.size());

        for (std::size_t y = 0; y < rows; ++y)
        {
            for (std::size_t x = 0; x < columns; ++x)
            {
                const auto index = y * columns + x;
                if (screen::cell_width_ops::is_tail(cell_state_[index].width))
                {
                    continue;
                }

                const auto &text = cells_[index];
                const auto *region = atlas_.find_or_add(text);
                if (region == nullptr)
                {
                    continue;
                }

                const auto left = static_cast<float>(x) * cell_width + static_cast<float>(region->left);
                const auto top =
                    static_cast<float>(y) * cell_height + metrics.ascent + static_cast<float>(region->top);
                quads_.push_back(d3d12ui::text_quad{
                    .x = left,
                    .y = top,
                    .width = static_cast<float>(region->width),
                    .height = static_cast<float>(region->height),
                    .u0 = static_cast<float>(region->x) * atlas_scale,
                    .v0 = static_cast<float>(region->y) * atlas_scale,
                    .u1 = static_cast<float>(region->x + region->width) * atlas_scale,
                    .v1 = static_cast<float>(region->y + region->height) * atlas_scale,
                });
            }
        }

        if (atlas_.dirty())
        {
            renderer_.set_text_texture(atlas_.pixels(), atlas_.size(), atlas_.size());
            atlas_.clear_dirty();
        }
        renderer_.set_text_quads(quads_);
    }

    void run()
    {
        renderer_.initialize(window_);
        renderer_.render();

        for (;;)
        {
            if (stop_.load(std::memory_order_relaxed))
            {
                break;
            }

            HANDLE waits[] = {stop_event_.get()};
            const auto wait = ::DCompositionWaitForCompositorClock(1, waits, INFINITE);
            if (wait == WAIT_OBJECT_0)
            {
                break;
            }
            if (wait == WAIT_FAILED)
            {
                win32::throw_last_error();
            }

            RECT client{};
            if (::GetClientRect(window_, &client) == FALSE)
            {
                win32::throw_last_error();
            }
            const auto width = static_cast<UINT>(client.right - client.left);
            const auto height = static_cast<UINT>(client.bottom - client.top);
            bool changed = false;

            {
                std::unique_lock<win32::srwlock> lock{console_mutex_};
                changed = console_.copy_updates(updates_);
            }

            renderer_.resize(width, height);

            if (changed)
            {
                apply_screen_updates();
                build_quads();
                renderer_.render();
            }
        }

        renderer_.reset();
    }

    static constexpr std::size_t columns = 120;
    static constexpr std::size_t rows = 30;

    console &console_;
    win32::srwlock &console_mutex_;
    HWND window_{};
    d3d12ui::d3d12_window_renderer renderer_;
    uitext::text_engine text_{fallback_families(), 16.0f};
    uitext::cell_atlas atlas_{text_};
    std::vector<std::u16string> cells_ = std::vector<std::u16string>(columns * rows);
    std::vector<screen_cell_update> cell_state_ = std::vector<screen_cell_update>(columns * rows);
    std::vector<d3d12ui::text_quad> quads_;
    std::vector<screen_row_update> updates_;
    win32::basic_thread thread_;
    win32::event stop_event_{win32::create_tag, true, false};
    std::atomic_bool stop_{};
};

} // namespace corehost::window
