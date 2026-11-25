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

#ifndef COREHOST_SCROLLBAR_THUMB_MASK_SIZE
#define COREHOST_SCROLLBAR_THUMB_MASK_SIZE 32
#endif
#ifndef COREHOST_SCROLLBAR_THUMB_MASK_SAMPLES
#define COREHOST_SCROLLBAR_THUMB_MASK_SAMPLES 4
#endif

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

    struct mask_point final
    {
        float x{};
        float y{};
    };

    static bool point_in_polygon(const std::array<mask_point, 64> &points, int count, float x, float y) noexcept
    {
        bool inside = false;
        for (int i = 0, j = count - 1; i < count; j = i++)
        {
            const auto &a = points[static_cast<std::size_t>(i)];
            const auto &b = points[static_cast<std::size_t>(j)];
            if (((a.y > y) != (b.y > y)) &&
                (x < (b.x - a.x) * (y - a.y) / (b.y - a.y) + a.x))
            {
                inside = !inside;
            }
        }
        return inside;
    }

    static std::vector<std::uint8_t> make_arrow_mask(int width, int height, bool down) noexcept
    {
        const auto w = static_cast<float>(width);
        const auto h = static_cast<float>(height);
        const auto radius = std::min(w, h) * 0.15f;
        constexpr int samples = 4;
        const mask_point vertices[3]{
            {w * 0.5f, h * 0.09f},
            {w * 0.92f, h * 0.91f},
            {w * 0.08f, h * 0.91f},
        };
        std::array<mask_point, 64> polygon{};
        int count = 0;

        for (int i = 0; i < 3; ++i)
        {
            const auto &previous = vertices[(i + 2) % 3];
            const auto &current = vertices[i];
            const auto &next = vertices[(i + 1) % 3];
            const auto previous_length = std::hypot(previous.x - current.x, previous.y - current.y);
            const auto next_length = std::hypot(next.x - current.x, next.y - current.y);
            const auto start = mask_point{current.x + (previous.x - current.x) * radius / previous_length,
                                          current.y + (previous.y - current.y) * radius / previous_length};
            const auto end = mask_point{current.x + (next.x - current.x) * radius / next_length,
                                        current.y + (next.y - current.y) * radius / next_length};
            polygon[static_cast<std::size_t>(count++)] = start;
            for (int step = 1; step <= 12; ++step)
            {
                const auto t = static_cast<float>(step) / 12.0f;
                const auto one_minus = 1.0f - t;
                polygon[static_cast<std::size_t>(count++)] = mask_point{
                    one_minus * one_minus * start.x + 2.0f * one_minus * t * current.x + t * t * end.x,
                    one_minus * one_minus * start.y + 2.0f * one_minus * t * current.y + t * t * end.y};
            }
        }

        std::vector<std::uint8_t> mask(static_cast<std::size_t>(width) * height, 0);
        for (int y = 0; y < height; ++y)
        {
            for (int x = 0; x < width; ++x)
            {
                int covered = 0;
                for (int sy = 0; sy < samples; ++sy)
                {
                    for (int sx = 0; sx < samples; ++sx)
                    {
                        const auto sample_x = static_cast<float>(x) + (static_cast<float>(sx) + 0.5f) / samples;
                        const auto sample_y = static_cast<float>(y) + (static_cast<float>(sy) + 0.5f) / samples;
                        covered += point_in_polygon(polygon, count, sample_x, sample_y) ? 1 : 0;
                    }
                }
                mask[static_cast<std::size_t>(y) * width + x] =
                    static_cast<std::uint8_t>(covered * 255 / (samples * samples));
            }
        }
        if (down)
        {
            for (int y = 0; y < height / 2; ++y)
            {
                for (int x = 0; x < width; ++x)
                {
                    std::swap(mask[static_cast<std::size_t>(y) * width + x],
                              mask[static_cast<std::size_t>(height - 1 - y) * width + x]);
                }
            }
        }
        return mask;
    }
    static constexpr int thumb_mask_size = COREHOST_SCROLLBAR_THUMB_MASK_SIZE;
    static constexpr int thumb_mask_samples = COREHOST_SCROLLBAR_THUMB_MASK_SAMPLES;

    static std::array<std::uint8_t, thumb_mask_size * thumb_mask_size> make_thumb_mask() noexcept
    {
        constexpr int size = thumb_mask_size;
        constexpr float radius = static_cast<float>(size) * 0.5f;
        constexpr int samples = thumb_mask_samples;
        std::array<std::uint8_t, size * size> mask{};
        for (int y = 0; y < size; ++y)
        {
            for (int x = 0; x < size; ++x)
            {
                int covered = 0;
                for (int sy = 0; sy < samples; ++sy)
                {
                    for (int sx = 0; sx < samples; ++sx)
                    {
                        const auto px = static_cast<float>(x) + (static_cast<float>(sx) + 0.5f) / samples;
                        const auto py = static_cast<float>(y) + (static_cast<float>(sy) + 0.5f) / samples;
                        const auto cx = std::clamp(px, radius, static_cast<float>(size) - radius);
                        const auto cy = std::clamp(py, radius, static_cast<float>(size) - radius);
                        covered += std::hypot(px - cx, py - cy) <= radius ? 1 : 0;
                    }
                }
                mask[static_cast<std::size_t>(y) * size + x] =
                    static_cast<std::uint8_t>(covered * 255 / (samples * samples));
            }
        }
        return mask;
    }

    void ensure_arrow_masks(int width, int height)
    {
        if (width == arrow_mask_width_ && height == arrow_mask_height_)
        {
            return;
        }
        arrow_mask_width_ = width;
        arrow_mask_height_ = height;
        arrow_up_mask_ = make_arrow_mask(width, height, false);
        arrow_down_mask_ = make_arrow_mask(width, height, true);
    }

    static void add_mask_quad(std::vector<d3d12ui::text_quad> &quads, float x, float y, float width, float height,
                              float u0, float v0, float u1, float v1, console_color color, float alpha)
    {
        d3d12ui::text_quad quad{
            .x = x,
            .y = y,
            .width = width,
            .height = height,
            .u0 = u0,
            .v0 = v0,
            .u1 = u1,
            .v1 = v1,
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
        const auto scroll_shift = -static_cast<float>(scroll_fraction_) * cell_height;

        for (std::size_t y = 0; y < display_rows_; ++y)
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
                        .y = static_cast<float>(y) * cell_height + scroll_shift,
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
                                  static_cast<float>(y) * cell_height + metrics.ascent + 1.0f + scroll_shift,
                                  cell_width * cell_span, 1.0f, line_color, sample, text_alpha);
                }
                if (has_attribute(style, screen::text_style_attribute::strikethrough))
                {
                    add_line_quad(quads_, static_cast<float>(x) * cell_width,
                                  static_cast<float>(y) * cell_height + metrics.ascent * 0.5f + scroll_shift,
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
                            .y = static_cast<float>(y) * cell_height + metrics.ascent + scroll_shift +
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
                    static_cast<float>(y) * cell_height + metrics.ascent + static_cast<float>(region->top) + scroll_shift;
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
            const auto scrollbar_width = scrollbar::default_width * scale;
            const auto client_width = client_width_px_ > 0.0f ? client_width_px_
                                                             : static_cast<float>(columns) * cell_width;
            const auto client_height = client_height_px_ > 0.0f ? client_height_px_
                                                               : static_cast<float>(rows) * cell_height;
            const auto fractional_top = static_cast<double>(history_size_) -
                                        scroll_pixels_ / static_cast<double>(cell_height);
            const auto top = std::max(0.0, fractional_top);
            const scrollbar::state scroll_state{history_size_ + rows, rows, top};
            const auto geometry = scrollbar::compute(client_width - scrollbar_width, 0.0f, scrollbar_width,
                                                     client_height, scroll_state,
                                                     scrollbar::arrow_height * scale);
            if (geometry.visible)
            {
                const auto expand = scrollbar_expand_;
                const console_color thumb_color{255, 255, 255};
                const auto thumb_width = (2.0f + 6.0f * expand) * scale;
                const auto thumb_x = client_width - 4.0f * scale - thumb_width;
                if (expand > 0.0f)
                {
                    add_line_quad(quads_, client_width - scrollbar_width, 0.0f, scrollbar_width, client_height,
                                  thumb_color, sample, 0.055f * expand);
                }
                static const auto thumb_mask = make_thumb_mask();
                const auto *thumb_region = atlas_.find_or_add_mask(0x53424D, static_cast<std::uint32_t>(thumb_mask_size), static_cast<std::uint32_t>(thumb_mask_size), thumb_mask);
                if (thumb_region == nullptr)
                {
                    add_line_quad(quads_, thumb_x, geometry.thumb_y, thumb_width, geometry.thumb_height, thumb_color,
                                  sample, 0.54f);
                }
                else
                {
                    const auto cap = std::min(4.0f * scale, geometry.thumb_height * 0.5f);
                    const auto u0 = static_cast<float>(thumb_region->x) * atlas_scale;
                    const auto u1 = static_cast<float>(thumb_region->x + thumb_mask_size) * atlas_scale;
                    const auto v0 = static_cast<float>(thumb_region->y) * atlas_scale;
                    const auto v1 = static_cast<float>(thumb_region->y + thumb_mask_size) * atlas_scale;
                    const auto vm = static_cast<float>(thumb_region->y + thumb_mask_size / 2) * atlas_scale;
                    add_mask_quad(quads_, thumb_x, geometry.thumb_y, thumb_width, cap, u0, v0, u1, vm, thumb_color,
                                  0.54f);
                    add_mask_quad(quads_, thumb_x, geometry.thumb_y + cap, thumb_width,
                                  std::max(0.0f, geometry.thumb_height - cap * 2.0f), u0, vm, u1, vm, thumb_color,
                                  0.54f);
                    add_mask_quad(quads_, thumb_x, geometry.thumb_y + geometry.thumb_height - cap, thumb_width, cap,
                                  u0, vm, u1, v1, thumb_color, 0.54f);
                }

                if (expand > 0.01f)
                {
                    ensure_arrow_masks(std::max(1, static_cast<int>(std::lround(10.0f * scale))),
                                       std::max(1, static_cast<int>(std::lround(9.0f * scale))));
                    const auto arrow_center = client_width - 8.0f * scale;
                    const auto *up = atlas_.find_or_add_mask(
                        0x534255u ^ (static_cast<std::uint32_t>(arrow_mask_width_) << 16) ^
                            static_cast<std::uint32_t>(arrow_mask_height_),
                        static_cast<std::uint32_t>(arrow_mask_width_),
                        static_cast<std::uint32_t>(arrow_mask_height_), arrow_up_mask_);
                    const auto *down = atlas_.find_or_add_mask(
                        0x534144u ^ (static_cast<std::uint32_t>(arrow_mask_width_) << 16) ^
                            static_cast<std::uint32_t>(arrow_mask_height_),
                        static_cast<std::uint32_t>(arrow_mask_width_),
                        static_cast<std::uint32_t>(arrow_mask_height_), arrow_down_mask_);
                    for (int i = 0; i < 2; ++i)
                    {
                        const auto *region = i == 0 ? up : down;
                        if (region == nullptr)
                        {
                            continue;
                        }
                        const auto u0 = static_cast<float>(region->x) * atlas_scale;
                        const auto v0 = static_cast<float>(region->y) * atlas_scale;
                        const auto arrow_width = static_cast<float>(arrow_mask_width_);
                        const auto arrow_height = static_cast<float>(arrow_mask_height_);
                        const auto arrow_y = i == 0 ? 8.5f * scale
                                                   : client_height - 8.5f * scale - arrow_height;
                        add_mask_quad(quads_, std::round(arrow_center - arrow_width * 0.5f),
                                      std::round(arrow_y), arrow_width, arrow_height, u0, v0,
                                      static_cast<float>(region->x + arrow_mask_width_) * atlas_scale,
                                      static_cast<float>(region->y + arrow_mask_height_) * atlas_scale, thumb_color,
                                      0.54f * expand);
                    }
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
            const auto row_height_px = static_cast<double>(metrics.ascent + metrics.descent + metrics.line_gap);
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
                    cell_state_.assign(columns * (rows + 1), screen::cell{});
                    resized = true;
                }

                console_.set_scroll_row_height(static_cast<float>(row_height_px));
                const auto history = console_.history_size();
                auto pixels = console_.scroll_pixels();
                if (pixels > 0.0 && history > history_size_)
                {
                    // Keep the viewed history row fixed when new output is appended.
                    console_.scroll_by_pixels(static_cast<double>(history - history_size_) *
                                              row_height_px);
                    pixels = console_.scroll_pixels();
                }
                scroll_changed = resized || std::abs(pixels - scroll_pixels_) > 0.01 ||
                                 history != history_size_;
                if (scroll_changed)
                {
                    const auto whole = static_cast<std::size_t>(pixels / row_height_px);
                    const auto fraction_pixels =
                        pixels - static_cast<double>(whole) * row_height_px;
                    const bool extra = fraction_pixels > 0.01;
                    console_.copy_scrolled_rows(whole + (extra ? 1U : 0U), scroll_rows_, styles_, extra ? 1U : 0U);
                    scroll_pixels_ = pixels;
                    scroll_fraction_ = extra ? 1.0 - fraction_pixels / row_height_px : 0.0;
                    history_size_ = history;
                    display_rows_ = scroll_rows_.size();
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
            bool scrollbar_animating = false;
            {
                const auto target = scrollbar_hovered_ ? 1.0f : 0.0f;
                if (scrollbar_expand_ != target)
                {
                    constexpr float step = 1.0f / 8.0f;
                    scrollbar_expand_ = target > scrollbar_expand_ ? std::min(target, scrollbar_expand_ + step)
                                                                   : std::max(target, scrollbar_expand_ - step);
                    scrollbar_animating = true;
                }
            }
            blink_on_ = blink_on;
            draw_cursor_ = cursor_.visible && focused_ && scroll_pixels_ == 0.0 &&
                           (cursor_.blinking ? blink_on_ : true);
            cursor_dirty = false;
            renderer_.resize(width, height);
            client_width_px_ = static_cast<float>(width);
            client_height_px_ = static_cast<float>(height);

            if (changed || cursor_changed || hover_changed || scrollbar_animating || focus_changed || blink_changed ||
                dpi_changed)
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

    std::vector<screen::cell> cell_state_ = std::vector<screen::cell>(columns * (rows + 1));
    screen::style_table styles_;
    console_palette palette_;
    screen::cursor_state cursor_{};
    bool focused_{true};
    bool scrollbar_hovered_{};
    float scrollbar_expand_{};
    bool blink_on_{true};
    bool draw_cursor_{true};
    screen::grapheme_store graphemes_;
    std::vector<d3d12ui::text_quad> quads_;
    std::vector<d3d12ui::text_quad> color_quads_;
    std::vector<screen::row_block> updates_;
    std::vector<screen::row_block> scroll_rows_;
    std::vector<std::uint8_t> arrow_up_mask_;
    std::vector<std::uint8_t> arrow_down_mask_;
    int arrow_mask_width_{};
    int arrow_mask_height_{};
    double scroll_pixels_{};
    double scroll_fraction_{};
    float client_width_px_{};
    float client_height_px_{};
    std::size_t display_rows_{};
    std::size_t history_size_{};
    win32::basic_thread thread_;
    win32::event stop_event_{win32::create_tag, true, false};
    std::atomic_bool stop_{};
};

} // namespace corehost::window
