#pragma once

#include "cell_atlas.hpp"
#include "color_cell_atlas.hpp"
#include "console.hpp"
#include "console_theme.hpp"
#include "scrollbar.hpp"
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

    void apply_scrolled_rows()
    {
        graphemes_.clear();
        for (std::size_t y = 0; y < rows; ++y)
        {
            const auto &row = scroll_rows_[y];
            for (std::size_t x = 0; x < columns; ++x)
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

    static void set_tint(d3d12ui::text_quad &quad, console_color color, float alpha = 1.0f) noexcept
    {
        quad.r = static_cast<float>(color.r) / 255.0f;
        quad.g = static_cast<float>(color.g) / 255.0f;
        quad.b = static_cast<float>(color.b) / 255.0f;
        quad.a = alpha;
    }

    static bool has_attribute(const screen::text_style &style, screen::text_style_attribute attribute) noexcept
    {
        return (style.attributes & screen::text_style_attribute_bit(attribute)) != 0;
    }

    static void add_line_quad(std::vector<d3d12ui::text_quad> &quads, float x, float y, float width, float height,
                              console_color color, float sample, float alpha) noexcept
    {
        d3d12ui::text_quad quad{
            .x = x,
            .y = y,
            .width = width,
            .height = height,
            .u0 = sample,
            .v0 = sample,
            .u1 = sample,
            .v1 = sample,
        };
        set_tint(quad, color, alpha);
        quads.push_back(quad);
    }

    static bool same_cursor(const screen::cursor_state &left, const screen::cursor_state &right) noexcept
    {
        return left.x == right.x && left.y == right.y && left.visible == right.visible &&
               left.size == right.size && left.style == right.style;
    }

    void build_quads()
    {
        atlas_.begin_frame();
        color_atlas_.begin_frame();
        quads_.clear();
        color_quads_.clear();
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
                auto foreground = foreground_color(style, palette_);
                const auto background = background_color(style, palette_);
                const auto sample = 0.5f / static_cast<float>(atlas_.size());
                const auto cell_span = cell.is_wide() ? 2.0f : 1.0f;
                const auto bold = has_attribute(style, screen::text_style_attribute::bold);
                const auto faint = has_attribute(style, screen::text_style_attribute::faint);
                auto text_alpha = 1.0f;
                if (faint)
                {
                    if (palette_.faint == faint_mode::alpha)
                    {
                        text_alpha = 0.5f;
                    }
                    else
                    {
                        foreground = blend_color(foreground, background, 0.5f);
                    }
                }

                if (background.r != palette_.default_background.r ||
                    background.g != palette_.default_background.g ||
                    background.b != palette_.default_background.b)
                {
                    d3d12ui::text_quad background_quad{
                        .x = static_cast<float>(x) * cell_width,
                        .y = static_cast<float>(y) * cell_height,
                        .width = cell_width * cell_span,
                        .height = cell_height,
                        .u0 = sample,
                        .v0 = sample,
                        .u1 = sample,
                        .v1 = sample,
                    };
                    set_tint(background_quad, background);
                    quads_.push_back(background_quad);
                }

                auto line_color = style.underline.kind == screen::color_kind::default_color
                                      ? foreground
                                      : resolve_color(style.underline, palette_, true);
                if (faint && palette_.faint == faint_mode::blend_background)
                {
                    line_color = blend_color(line_color, background, 0.5f);
                }
                if (has_attribute(style, screen::text_style_attribute::underline))
                {
                    add_line_quad(quads_, static_cast<float>(x) * cell_width,
                                  static_cast<float>(y) * cell_height + metrics.ascent + 1.0f,
                                  cell_width * cell_span, 1.0f, line_color, sample, text_alpha);
                }
                if (has_attribute(style, screen::text_style_attribute::strikethrough))
                {
                    add_line_quad(quads_, static_cast<float>(x) * cell_width,
                                  static_cast<float>(y) * cell_height + metrics.ascent * 0.5f,
                                  cell_width * cell_span, 1.0f, line_color, sample, text_alpha);
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

                if (cell.has_cluster())
                {
                    const auto color_scale = 1.0f / static_cast<float>(color_atlas_.size());
                    for (const auto &region : color_atlas_.find_or_add(text))
                    {
                        d3d12ui::text_quad color_quad{
                            .x = static_cast<float>(x) * cell_width + static_cast<float>(region.left),
                            .y = static_cast<float>(y) * cell_height + metrics.ascent +
                                 static_cast<float>(region.top),
                            .width = static_cast<float>(region.width),
                            .height = static_cast<float>(region.height),
                            .u0 = static_cast<float>(region.x) * color_scale,
                            .v0 = static_cast<float>(region.y) * color_scale,
                            .u1 = static_cast<float>(region.x + region.width) * color_scale,
                            .v1 = static_cast<float>(region.y + region.height) * color_scale,
                        };
                        set_tint(color_quad, region.kind == uitext::glyph_color_kind::color ? console_color{255, 255, 255} : foreground,
                                 text_alpha);
                        color_quads_.push_back(color_quad);
                    }
                    continue;
                }

                const auto *region = cell.is_scalar() && !bold
                                         ? atlas_.find_or_add(cell.primary())
                                         : atlas_.find_or_add_transient(text, bold);
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
                set_tint(text_quad, foreground, text_alpha);
                quads_.push_back(text_quad);
            }
        }

        if (draw_cursor_)
        {
            const auto cell_left = static_cast<float>(cursor_.x) * cell_width;
            const auto cell_top = static_cast<float>(cursor_.y) * cell_height;
            auto cursor_left = cell_left;
            auto cursor_top = cell_top;
            auto cursor_width = cell_width;
            auto cursor_height = cell_height;
            switch (cursor_.style)
            {
            case screen::cursor_style::underline:
                cursor_height = std::max(1.0f, cell_height * static_cast<float>(cursor_.size) / 100.0f);
                cursor_top = cell_top + cell_height - cursor_height;
                break;
            case screen::cursor_style::bar:
                cursor_width =
                    std::max(1.0f, std::floor(cell_width * static_cast<float>(cursor_.size) / 200.0f));
                break;
            case screen::cursor_style::block:
            default:
                break;
            }

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
                .width = cursor_width,
                .height = cursor_height,
                .u0 = sample,
                .v0 = sample,
                .u1 = sample,
                .v1 = sample,
            };
            set_tint(cursor_quad, cursor_color);
            quads_.push_back(cursor_quad);
        }

        if (focused_ && history_size_ != 0)
        {
            const auto sample = 0.5f / static_cast<float>(atlas_.size());
            const auto scale = static_cast<float>(dpi_) / 96.0f;
            const auto scrollbar_width = std::max(1.0f, 8.0f * scale);
            const auto client_width = static_cast<float>(columns) * cell_width;
            const auto client_height = static_cast<float>(rows) * cell_height;
            const auto top = history_size_ > scroll_offset_ ? history_size_ - scroll_offset_ : 0;
            const scrollbar::state scroll_state{history_size_ + rows, rows, top};
            const auto geometry = scrollbar::compute(client_width - scrollbar_width, 0.0f, scrollbar_width,
                                                     client_height, scroll_state);
            if (geometry.visible)
            {
                const auto track_color = blend_color(palette_.default_foreground, palette_.default_background, 0.75f);
                const auto thumb_color = blend_color(palette_.default_foreground, palette_.default_background, 0.35f);
                if (scrollbar_hovered_)
                {
                    add_line_quad(quads_, geometry.x, geometry.y, geometry.width, geometry.height, track_color, sample,
                                  0.45f);
                    add_line_quad(quads_, geometry.x + 1.0f, geometry.thumb_y, geometry.width - 2.0f,
                                  geometry.thumb_height, thumb_color, sample, 0.95f);
                }
                else
                {
                    const auto thin_width = std::max(1.0f, 2.0f * scale);
                    add_line_quad(quads_, client_width - thin_width, geometry.thumb_y, thin_width,
                                  geometry.thumb_height, thumb_color, sample, 0.95f);
                }
            }
        }

        if (atlas_.dirty())
        {
            renderer_.set_text_texture(atlas_.pixels(), atlas_.size(), atlas_.size());
            atlas_.clear_dirty();
        }
        renderer_.set_text_quads(quads_);

        if (color_atlas_.dirty())
        {
            renderer_.set_color_text_texture(color_atlas_.pixels(), color_atlas_.size(), color_atlas_.size());
            color_atlas_.clear_dirty();
        }
        renderer_.set_color_text_quads(color_quads_);
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
            const auto dpi = ::GetDpiForWindow(window_);
            const bool dpi_changed = dpi != 0 && dpi != dpi_;
            if (dpi_changed)
            {
                dpi_ = dpi;
                text_.set_size(16.0f * static_cast<float>(dpi_) / 96.0f);
                atlas_.reset();
                cursor_dirty = true;
            }
            const auto metrics = text_.metrics();
            const auto desired_columns =
                std::max<std::size_t>(1, static_cast<std::size_t>(static_cast<float>(width) / metrics.advance));
            const auto desired_rows = std::max<std::size_t>(
                1, static_cast<std::size_t>(static_cast<float>(height) / (metrics.ascent + metrics.descent + metrics.line_gap)));
            bool changed = false;
            bool scroll_changed = false;
            bool focused = false;
            bool scrollbar_hovered = false;
            bool title_changed = false;
            std::wstring window_title;
            screen::cursor_state cursor{};

            {
                std::unique_lock<win32::srwlock> lock{console_mutex_};
                bool resized = false;
                if (desired_columns != columns || desired_rows != rows)
                {
                    console_.resize(desired_columns, desired_rows);
                    columns = desired_columns;
                    rows = desired_rows;
                    cell_state_.assign(columns * rows, screen::cell{});
                    resized = true;
                }

                const auto history = console_.history_size();
                auto offset = console_.scroll_offset();
                if (offset != 0 && history > history_size_)
                {
                    // Keep the viewed history row fixed when new output is appended.
                    console_.scroll_by(static_cast<std::ptrdiff_t>(history - history_size_));
                    offset = console_.scroll_offset();
                }
                scroll_changed = resized || offset != scroll_offset_ || history != history_size_;
                if (scroll_changed)
                {
                    console_.copy_scrolled_rows(offset, scroll_rows_, styles_);
                    scroll_offset_ = offset;
                    history_size_ = history;
                    changed = true;
                }
                else
                {
                    changed = console_.copy_updates(updates_, styles_);
                }

                cursor = console_.state().cursor();
                focused = console_.has_focus();
                scrollbar_hovered = console_.scrollbar_hovered();
                if (console_.state().take_title_changed())
                {
                    const auto &title = console_.state().title;
                    window_title.assign(reinterpret_cast<const wchar_t *>(title.data()), title.size());
                    title_changed = true;
                }
            }

            if (title_changed)
            {
                ::SetWindowTextW(window_, window_title.c_str());
            }

            const bool cursor_changed = cursor_dirty || !same_cursor(cursor_, cursor);
            const bool hover_changed = scrollbar_hovered != scrollbar_hovered_;
            const bool focus_changed = focused != focused_;
            const auto now = ::GetTickCount64();
            const bool blink_on = focused && ((now / blink_interval_ms) % 2 == 0);
            const bool blink_changed = blink_on != blink_on_;

            cursor_ = cursor;
            focused_ = focused;
            scrollbar_hovered_ = scrollbar_hovered;
            blink_on_ = blink_on;
            draw_cursor_ = cursor_.visible && focused_ && scroll_offset_ == 0 &&
                           (cursor_.blinking ? blink_on_ : true);
            cursor_dirty = false;
            renderer_.resize(width, height);

            if (changed || cursor_changed || hover_changed || focus_changed || blink_changed || dpi_changed)
            {
                if (scroll_changed)
                {
                    apply_scrolled_rows();
                }
                else if (changed)
                {
                    apply_updates();
                }
                build_quads();
                renderer_.render();
            }
        }

        renderer_.reset();
    }

    std::size_t columns = 120;
    std::size_t rows = 30;

    console &console_;
    win32::srwlock &console_mutex_;
    HWND window_{};
    d3d12ui::d3d12_window_renderer renderer_;
    std::array<uitext::font_fallback_entry, 3> fallback_families_{
        uitext::font_fallback_entry{L"Cascadia Code"},
        uitext::font_fallback_entry{L"Cascadia Mono"},
        uitext::font_fallback_entry{L"Consolas"},
    };
    uitext::text_engine text_{fallback_families_, 16.0f};
    uitext::cell_atlas atlas_{text_};
    uitext::color_cell_atlas color_atlas_{text_};
    static constexpr ULONGLONG blink_interval_ms = 530;
    UINT dpi_ = 96;

    std::vector<screen::cell> cell_state_ = std::vector<screen::cell>(columns * rows);
    screen::style_table styles_;
    console_palette palette_;
    screen::cursor_state cursor_{};
    bool focused_{true};
    bool scrollbar_hovered_{};
    bool blink_on_{true};
    bool draw_cursor_{true};
    screen::grapheme_store graphemes_;
    std::vector<d3d12ui::text_quad> quads_;
    std::vector<d3d12ui::text_quad> color_quads_;
    std::vector<screen::row_block> updates_;
    std::vector<screen::row_block> scroll_rows_;
    std::size_t scroll_offset_{};
    std::size_t history_size_{};
    win32::basic_thread thread_;
    win32::event stop_event_{win32::create_tag, true, false};
    std::atomic_bool stop_{};
};

} // namespace corehost::window
