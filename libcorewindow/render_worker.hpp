#pragma once

#include "cell_atlas.hpp"
#include "console.hpp"
#include "console_theme.hpp"
#include "d3d12_window_renderer.hpp"
#include "text_engine.hpp"
#include "win32/event.hpp"
#include "win32/srwlock.hpp"
#include "win32/thread.hpp"

#include <Windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace corehost::window
{

// Owns the renderer, text engine and atlas. It samples console damage on
// compositor clock ticks and presents only when something changed.
class render_worker final
{
  public:
    render_worker(console &console, win32::srwlock &console_mutex,
                  console_palette palette = console_palette::campbell())
        : console_{console}, console_mutex_{console_mutex}, palette_{palette}
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

    void apply_updates()
    {
        for (std::size_t y = 0; y < rows && y < updates_.size(); ++y)
        {
            const auto &row = updates_[y];
            if (row.cells.empty())
            {
                continue;
            }

            const auto begin = std::min<std::size_t>(row.state.dirty_begin, columns);
            const auto end = std::min<std::size_t>(row.state.dirty_end, columns);
            for (auto x = begin; x < end; ++x)
            {
                auto value = row.cells[x];
                if (value.has_cluster())
                {
                    const auto source =
                        row.graphemes.view(screen::grapheme_store::ref{value.offset(), value.length()});
                    if (graphemes_.can_append(source.size()))
                    {
                        const auto ref = graphemes_.append(source);
                        value = value.with_cluster_ref(ref.offset, ref.length);
                    }
                }

                cell_state_[y * columns + x] = value;
            }
        }
    }

    static void set_tint(d3d12ui::text_quad &quad, console_color color) noexcept
    {
        quad.r = static_cast<float>(color.r) / 255.0f;
        quad.g = static_cast<float>(color.g) / 255.0f;
        quad.b = static_cast<float>(color.b) / 255.0f;
        quad.a = 1.0f;
    }

    static bool same_cursor(const screen::cursor_state &left, const screen::cursor_state &right) noexcept
    {
        return left.x == right.x && left.y == right.y && left.visible == right.visible &&
               left.size == right.size && left.style == right.style;
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
                const auto &cell = cell_state_[index];
                if (cell.is_continuation())
                {
                    continue;
                }

                const auto &style = styles_.at(cell.style());
                const auto foreground = foreground_color(style, palette_);
                const auto background = background_color(style, palette_);
                const auto sample = 0.5f / static_cast<float>(atlas_.size());
                if (background.r != palette_.default_background.r ||
                    background.g != palette_.default_background.g ||
                    background.b != palette_.default_background.b)
                {
                    d3d12ui::text_quad background_quad{
                        .x = static_cast<float>(x) * cell_width,
                        .y = static_cast<float>(y) * cell_height,
                        .width = cell_width,
                        .height = cell_height,
                        .u0 = sample,
                        .v0 = sample,
                        .u1 = sample,
                        .v1 = sample,
                    };
                    set_tint(background_quad, background);
                    quads_.push_back(background_quad);
                }

                std::u16string_view text;
                std::array<char16_t, 1> scalar{};
                if (cell.has_cluster())
                {
                    const auto cluster = graphemes_.view(screen::grapheme_store::ref{cell.offset(), cell.length()});
                    text = std::u16string_view{cluster.data(), cluster.size()};
                }
                else if (cell.is_blank())
                {
                    continue;
                }
                else
                {
                    scalar[0] = cell.primary();
                    text = std::u16string_view{scalar.data(), scalar.size()};
                }

                const auto *region = atlas_.find_or_add(text);
                if (region == nullptr)
                {
                    continue;
                }

                const auto left = static_cast<float>(x) * cell_width + static_cast<float>(region->left);
                const auto top =
                    static_cast<float>(y) * cell_height + metrics.ascent + static_cast<float>(region->top);
                d3d12ui::text_quad text_quad{
                    .x = left,
                    .y = top,
                    .width = static_cast<float>(region->width),
                    .height = static_cast<float>(region->height),
                    .u0 = static_cast<float>(region->x) * atlas_scale,
                    .v0 = static_cast<float>(region->y) * atlas_scale,
                    .u1 = static_cast<float>(region->x + region->width) * atlas_scale,
                    .v1 = static_cast<float>(region->y + region->height) * atlas_scale,
                };
                set_tint(text_quad, foreground);
                quads_.push_back(text_quad);
            }
        }

        if (cursor_.visible)
        {
            const auto cursor_height = std::max(1.0f, cell_height * static_cast<float>(cursor_.size) / 100.0f);
            const auto cursor_left = static_cast<float>(cursor_.x) * cell_width;
            const auto cursor_top = static_cast<float>(cursor_.y) * cell_height + cell_height - cursor_height;
            const auto sample = 0.5f / static_cast<float>(atlas_.size());
            console_color cursor_color = palette_.default_foreground;
            if (cursor_.x < columns && cursor_.y < rows)
            {
                cursor_color = foreground_color(styles_.at(cell_state_[cursor_.y * columns + cursor_.x].style()),
                                                palette_);
            }

            d3d12ui::text_quad cursor_quad{
                .x = cursor_left,
                .y = cursor_top,
                .width = cell_width,
                .height = cursor_height,
                .u0 = sample,
                .v0 = sample,
                .u1 = sample,
                .v1 = sample,
            };
            set_tint(cursor_quad, cursor_color);
            quads_.push_back(cursor_quad);
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
        const auto &clear = palette_.default_background;
        renderer_.set_clear_color(static_cast<float>(clear.r) / 255.0f, static_cast<float>(clear.g) / 255.0f,
                                  static_cast<float>(clear.b) / 255.0f, 1.0f);
        renderer_.render();
        bool cursor_dirty = true;

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
            screen::cursor_state cursor{};

            {
                std::unique_lock<win32::srwlock> lock{console_mutex_};
                changed = console_.copy_updates(updates_, styles_);
                cursor = console_.state().cursor();
            }

            const bool cursor_changed = cursor_dirty || !same_cursor(cursor_, cursor);
            cursor_ = cursor;
            cursor_dirty = false;
            renderer_.resize(width, height);

            if (changed || cursor_changed)
            {
                if (changed)
                {
                    apply_updates();
                }
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
    std::vector<screen::cell> cell_state_ = std::vector<screen::cell>(columns * rows);
    screen::style_table styles_;
    console_palette palette_;
    screen::cursor_state cursor_{};
    screen::grapheme_store graphemes_;
    std::vector<d3d12ui::text_quad> quads_;
    std::vector<screen::row_block> updates_;
    win32::basic_thread thread_;
    win32::event stop_event_{win32::create_tag, true, false};
    std::atomic_bool stop_{};
};

} // namespace corehost::window
