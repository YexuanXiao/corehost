#pragma once

#include "cell_atlas.hpp"
#include "utility/log.hpp"
#include "color_cell_atlas.hpp"
#include "console.hpp"
#include "console_config.hpp"
#include "console_theme.hpp"
#include "scrollbar.hpp"
#include "d3d12_window_renderer.hpp"
#include "text_engine.hpp"
#include "win32/event.hpp"
#include "win32/handle.hpp"
#include "win32/srwlock.hpp"
#include "win32/thread.hpp"

#include <Windows.h>

#include <algorithm>
#include <functional>
#include <array>
#include <flat_map>
#include <flat_set>
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
    render_worker(console &console, win32::srwlock &console_mutex, const corehost_config &config,
                  win32::handle_view damage_event)
        : columns{console.buffer().width()}, rows{console.buffer().height()}, console_{console}, console_mutex_{console_mutex},
          config_{config}, scheme_{select_scheme(config.color, config.window.dark_mode)},
          fallback_families_{make_fallback_families(config.font)},
          fallback_count_{make_fallback_count(config.font)},
          text_{std::span<const uitext::font_fallback_entry>{fallback_families_}.first(fallback_count_),
                config.font.size, config.font.ligatures, config.font.variation, config.font.variation_bold,
                config.font.variation_italic, config.font.variation_bold_italic, config.font.anti_alias,
                config.font.rendering_mode},
          damage_event_{damage_event}
    {
    }

    ~render_worker()
    {
        stop();
    }

    render_worker(const render_worker &) = delete;
    render_worker &operator=(const render_worker &) = delete;

    using text_changed_callback = void (*)(void *);
    void set_text_changed_callback(text_changed_callback callback, void *data) noexcept
    {
        text_changed_callback_ = callback;
        text_changed_callback_data_ = data;
    }

    void request_redraw() noexcept
    {
        redraw_requested_.store(true, std::memory_order_relaxed);
        (void)::SetEvent(wake_event_.get());
    }

    void request_atlas_upload() noexcept
    {
        atlas_upload_requested_.store(true, std::memory_order_relaxed);
        (void)::SetEvent(wake_event_.get());
    }

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
            if (y < 3)
            {
                LOG2("apply row y=%zu cells=%zu begin=%zu end=%zu", y, row.cells.size(), begin, end);
            }
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
                if (y < 4 && x < 4 && value.primary() != 0x20)
                {
                    const auto &style = styles_.at(value.style());
                    LOG2("assigned y=%zu x=%zu primary=%04x fgkind=%d fg=%u bgkind=%d bg=%u", y, x,
                         value.primary(), static_cast<int>(style.foreground.kind), style.foreground.value,
                         static_cast<int>(style.background.kind), style.background.value);
                }
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

    // Scrollbar masks are fixed at 32x32 with no supersampling. This is a
    // deliberate performance/quality tradeoff: generation happens once, the
    // thumb uses 9-slice, and arrows are scaled to their 10x9 DIP visual size.
    // No per-DPI mask regeneration or runtime configuration.

    static std::vector<std::uint8_t> make_arrow_mask(bool down) noexcept
    {
        constexpr int width = scrollbar::mask_size;
        constexpr int height = scrollbar::mask_size;
        const auto w = static_cast<float>(width);
        const auto h = static_cast<float>(height);
        // Rounded corners: 20% of the short side. The arrow is later scaled
        // from the fixed 32x32 mask to its 14x14 DIP visual size.
        const auto radius = std::min(w, h) * scrollbar::arrow_radius_ratio;
        const mask_point vertices[3]{
            {w * 0.5f, h * 0.06f},
            {w * scrollbar::arrow_side, h * scrollbar::arrow_side},
            {w * scrollbar::arrow_top, h * scrollbar::arrow_side},
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
                const auto px = static_cast<float>(x) + 0.5f;
                const auto py = static_cast<float>(y) + 0.5f;
                mask[static_cast<std::size_t>(y) * width + x] =
                    point_in_polygon(polygon, count, px, py) ? 255 : 0;
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
    static std::array<std::uint8_t, scrollbar::mask_size * scrollbar::mask_size> make_thumb_mask() noexcept
    {
        constexpr int size = scrollbar::mask_size;
        constexpr float radius = static_cast<float>(size) * scrollbar::thumb_cap_ratio;
        constexpr int samples = scrollbar::mask_samples;
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

    static bool same_selection(const screen::selection_range &left, const screen::selection_range &right) noexcept
    {
        return left.active == right.active && left.anchor_x == right.anchor_x &&
               left.anchor_y == right.anchor_y && left.caret_x == right.caret_x &&
               left.caret_y == right.caret_y && left.mode == right.mode;
    }

    static bool same_cursor(const screen::cursor_state &left, const screen::cursor_state &right) noexcept
    {
        return left.x == right.x && left.y == right.y && left.visible == right.visible &&
               left.size == right.size && left.style == right.style;
    }

    std::size_t row_nonspace(std::size_t y) const noexcept
    {
        std::size_t count = 0;
        for (std::size_t x = 0; x < columns; ++x)
        {
            const auto &cell = cell_state_[y * columns + x];
            if (cell.primary() != 0 && cell.primary() != u' ')
            {
                ++count;
            }
        }
        return count;
    }

    void build_quads()
    {
        has_blinking_text_ = false;
        atlas_.begin_frame();
        color_atlas_.begin_frame();
        quads_.clear();
        color_quads_.clear();
        image_quads_.clear();
        const auto metrics = text_.metrics();
        const auto cell_width = metrics.advance;
        const auto cell_height = metrics.ascent + metrics.descent + metrics.line_gap;
        const auto atlas_scale = 1.0f / static_cast<float>(atlas_.size());
        const auto scroll_shift = -static_cast<float>(scroll_fraction_) * cell_height;

        std::flat_set<std::uint32_t> drawn_images;
        std::flat_set<std::uint32_t> visible_images;
        std::vector<screen::cell> composition_cells;
        if (!composition_.empty())
        {
            composition_cells.reserve(composition_.size());
            for (const auto character : composition_)
            {
                auto width = screen::char_width_for_mode<screen::text_measurement::graphemes, true>(character);
                if (width <= 0)
                {
                    continue;
                }
                if (width > 2)
                {
                    width = 2;
                }
                composition_cells.push_back(
                    screen::cell::scalar(character, screen::default_cell_style,
                                         width == 2 ? screen::cell_width::wide : screen::cell_width::narrow));
                if (width == 2)
                {
                    composition_cells.push_back(screen::cell::tail(screen::default_cell_style));
                }
            }
        }

        for (std::size_t y = 0; y < rows; ++y)
        {
            for (std::size_t x = 0; x < columns; ++x)
            {
                const auto index = y * columns + x;
                const screen::cell *cell_pointer = &cell_state_[index];
                bool composition_cell = false;
                if (!composition_cells.empty() && y == composition_y_ && x >= composition_x_)
                {
                    const auto offset = x - composition_x_;
                    if (offset < composition_cells.size())
                    {
                        cell_pointer = &composition_cells[offset];
                        composition_cell = true;
                    }
                }
                const auto &cell = *cell_pointer;
                if (cell.is_continuation())
                {
                    continue;
                }

                const auto &style = styles_.at(cell.style());
                auto foreground = foreground_color(style, palette(), config_.text.bold_is_bright);
                const auto background = background_color(style, palette());
                const auto sample = 0.5f / static_cast<float>(atlas_.size());
                const auto cell_span = cell.is_wide() ? 2.0f : 1.0f;
                const auto bold = has_attribute(style, screen::text_style_attribute::bold);
                const auto faint = has_attribute(style, screen::text_style_attribute::faint);
                auto text_alpha = 1.0f;
                if (faint)
                {
                    if (palette().faint == faint_mode::alpha)
                    {
                        text_alpha = 0.5f;
                    }
                    else
                    {
                        foreground = blend_color(foreground, background, 0.5f);
                    }
                }

                if (background.r != palette().default_background.r ||
                    background.g != palette().default_background.g ||
                    background.b != palette().default_background.b)
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

                if (composition_cell)
                {
                    const auto composition_color =
                        blend_color(palette().default_foreground, palette().default_background, 0.25f);
                    add_line_quad(quads_, static_cast<float>(x) * cell_width,
                                  static_cast<float>(y) * cell_height + scroll_shift, cell_width * cell_span,
                                  cell_height, composition_color, sample, 1.0f);
                }

                if (selection_.active)
                {
                    const auto view_top = static_cast<double>(history_size_) -
                                          scroll_pixels_ / static_cast<double>(cell_height);
                    const auto virtual_row =
                        static_cast<std::size_t>(std::max(0.0, std::floor(view_top + static_cast<double>(y))));
                    if (console_.selection_contains(x, virtual_row))
                    {
                        const auto selection_color =
                            blend_color(palette().default_foreground, palette().default_background, 0.35f);
                        add_line_quad(quads_, static_cast<float>(x) * cell_width,
                                      static_cast<float>(y) * cell_height + scroll_shift, cell_width * cell_span,
                                      cell_height, selection_color, sample, 1.0f);
                    }
                }

                auto line_color = style.underline.kind == screen::color_kind::default_color
                                      ? foreground
                                      : resolve_color(style.underline, palette(), true);
                if (faint && palette().faint == faint_mode::blend_background)
                {
                    line_color = blend_color(line_color, background, 0.5f);
                }
                if (has_attribute(style, screen::text_style_attribute::underline) &&
                    config_.text.underline != text_underline_style::none)
                {
                    const auto left = static_cast<float>(x) * cell_width;
                    const auto width = cell_width * cell_span;
                    const auto top = static_cast<float>(y) * cell_height + metrics.ascent + 1.0f + scroll_shift;
                    const auto solid = [&](float line_top) {
                        add_line_quad(quads_, left, line_top, width, 1.0f, line_color, sample, text_alpha);
                    };
                    switch (config_.text.underline)
                    {
                    case text_underline_style::double_underline:
                        solid(top);
                        solid(top + 2.0f);
                        break;
                    case text_underline_style::dotted:
                        for (float offset = 0.0f; offset < width; offset += 4.0f)
                        {
                            add_line_quad(quads_, left + offset, top, std::min(2.0f, width - offset), 1.0f,
                                          line_color, sample, text_alpha);
                        }
                        break;
                    case text_underline_style::dashed:
                        for (float offset = 0.0f; offset < width; offset += 7.0f)
                        {
                            add_line_quad(quads_, left + offset, top, std::min(4.0f, width - offset), 1.0f,
                                          line_color, sample, text_alpha);
                        }
                        break;
                    case text_underline_style::single:
                    case text_underline_style::curl:
                    default:
                        solid(top);
                        break;
                    }
                }
                if (has_attribute(style, screen::text_style_attribute::strikethrough) &&
                    config_.text.strikethrough != text_strikethrough_style::none)
                {
                    const auto left = static_cast<float>(x) * cell_width;
                    const auto width = cell_width * cell_span;
                    const auto top = static_cast<float>(y) * cell_height + metrics.ascent * 0.5f + scroll_shift;
                    add_line_quad(quads_, left, top, width, 1.0f, line_color, sample, text_alpha);
                    if (config_.text.strikethrough == text_strikethrough_style::double_underline)
                    {
                        add_line_quad(quads_, left, top + 2.0f, width, 1.0f, line_color, sample, text_alpha);
                    }
                }

                if (hovered_hyperlink_ != screen::no_cell_hyperlink && cell.hyperlink() == hovered_hyperlink_)
                {
                    add_line_quad(quads_, static_cast<float>(x) * cell_width,
                                  static_cast<float>(y) * cell_height + metrics.ascent + 2.0f + scroll_shift,
                                  cell_width * cell_span, 1.0f, foreground, sample, 1.0f);
                }

                if (cell.has_image())
                {
                    LOG2("image cell x=%zu y=%zu", x, y);
                    const auto *fragment = console_.buffer().image_of(cell);
                    const auto *pixels = fragment ? console_.buffer().images().pixels(fragment->image_id) : nullptr;
                    LOG2("image data fragment=%p pixels=%p id=%u", fragment, pixels,
                         fragment ? fragment->image_id : 0);
                    if (config_.graphics.sixel && fragment != nullptr && pixels != nullptr && pixels->width != 0 && pixels->height != 0)
                    {
                        const auto image_id = fragment->image_id;
                        visible_images.insert(image_id);
                        if (drawn_images.insert(image_id).second)
                        {
                            const auto generation = console_.buffer().images().generation(image_id);
                            const auto cached = image_generations_.find(image_id);
                            if (cached == image_generations_.end() || cached->second != generation)
                            {
                                if (renderer_.set_image_texture(image_id, pixels->rgba, pixels->width, pixels->height))
                                {
                                    LOG2("image texture upload id=%u w=%u h=%u generation=%llu", image_id,
                                         pixels->width, pixels->height, generation);
                                    image_generations_.insert_or_assign(image_id, generation);
                                }
                            }
                            if (image_generations_.contains(image_id))
                            {
                                const auto image_columns =
                                    std::max<std::size_t>((fragment->source_width + 7) / 8, 1);
                                const auto image_rows =
                                    std::max<std::size_t>((fragment->source_height + 15) / 16, 1);
                                const d3d12ui::text_quad quad{
                                    .x = static_cast<float>(x) * cell_width,
                                    .y = static_cast<float>(y) * cell_height + scroll_shift,
                                    .width = cell_width * static_cast<float>(image_columns),
                                    .height = cell_height * static_cast<float>(image_rows),
                                    .u0 = 0.0f,
                                    .v0 = 0.0f,
                                    .u1 = 1.0f,
                                    .v1 = 1.0f,
                                };
                                image_quads_.push_back(d3d12ui::image_quad{image_id, quad});
                            }
                        }
                    }
                    continue;
                }

                if (config_.text.blink_enable && has_attribute(style, screen::text_style_attribute::blink))
                {
                    has_blinking_text_ = true;
                    if (!text_blink_on_)
                    {
                        continue;
                    }
                }

                std::u16string_view text;
                char16_t scalar{};
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
                    scalar = cell.primary();
                    text = std::u16string_view{&scalar, 1};
                }

                if (config_.font.color_emoji && cell.has_cluster())
                {
                    const auto color_regions = color_atlas_.find_or_add(text);
                    if (!color_regions.empty())
                    {
                        const auto color_scale = 1.0f / static_cast<float>(color_atlas_.size());
                        for (const auto &region : color_regions)
                        {
                            d3d12ui::text_quad color_quad{
                                .x = std::round(static_cast<float>(x) * cell_width + static_cast<float>(region.left)),
                                .y = std::round(static_cast<float>(y) * cell_height + metrics.ascent + scroll_shift +
                                                static_cast<float>(region.top)),
                                .width = static_cast<float>(region.width),
                                .height = static_cast<float>(region.height),
                                .u0 = static_cast<float>(region.x) * color_scale,
                                .v0 = static_cast<float>(region.y) * color_scale,
                                .u1 = static_cast<float>(region.x + region.width) * color_scale,
                                .v1 = static_cast<float>(region.y + region.height) * color_scale,
                            };
                            set_tint(color_quad, region.kind == uitext::glyph_color_kind::color
                                                     ? console_color{255, 255, 255}
                                                     : foreground,
                                     text_alpha);
                            color_quads_.push_back(color_quad);
                        }
                        continue;
                    }
                }

                const auto *region = cell.is_scalar() && !bold
                                         ? atlas_.find_or_add(cell.primary())
                                         : atlas_.find_or_add_transient(text, bold);
                if (y == 0 && x == 1)
                {
                    const auto pixel = region ? atlas_.pixels()[static_cast<std::size_t>(region->y) * atlas_.size() +
                                                               region->x]
                                              : 0;
                    LOG2("build cell x=1 primary=%04x region=%p rxy=%u,%u wh=%u,%u pixel=%u", cell.primary(),
                         region, region ? region->x : 0, region ? region->y : 0, region ? region->width : 0,
                         region ? region->height : 0, pixel);
                }
                if (region == nullptr)
                {
                    continue;
                }

                const auto left =
                    std::round(static_cast<float>(x) * cell_width + static_cast<float>(region->left));
                const auto top = std::round(static_cast<float>(y) * cell_height + metrics.ascent +
                                            static_cast<float>(region->top) + scroll_shift);
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
                if (y == 0 && x == 1)
                {
                    LOG2("text quad x=%.1f y=%.1f w=%.1f h=%.1f u=%.3f v=%.3f fg=%u,%u,%u", text_quad.x,
                         text_quad.y, text_quad.width, text_quad.height, text_quad.u0, text_quad.v0, foreground.r,
                         foreground.g, foreground.b);
                }
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
            auto cursor_style = cursor_.style;
            switch (config_.window.cursor_shape)
            {
            case cursor_style::beam:
                cursor_style = screen::cursor_style::bar;
                break;
            case cursor_style::block:
                cursor_style = screen::cursor_style::block;
                break;
            case cursor_style::underline:
            case cursor_style::double_underline:
                cursor_style = screen::cursor_style::underline;
                break;
            case cursor_style::hollow_block:
            case cursor_style::vintage:
            case cursor_style::automatic:
                break;
            }
            switch (cursor_style)
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
            console_color cursor_color = scheme_.cursor;
            if (cursor_.x < columns && cursor_.y < rows)
            {
                cursor_color = foreground_color(styles_.at(cell_state_[cursor_.y * columns + cursor_.x].style()),
                                                palette());
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

        if ((focused_ || config_.window.scrollbar == scrollbar_visibility::always) && history_size_ != 0)
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
                                                     scrollbar::track_inset * scale);
            if (geometry.visible)
            {
                const auto expand = scrollbar_expand_;
                const console_color thumb_color{255, 255, 255};
                const auto thumb_width = (scrollbar::thin_thumb_width + (scrollbar::expanded_thumb_width - scrollbar::thin_thumb_width) * expand) * scale;
                const auto thumb_x = client_width - scrollbar::thumb_right_margin * scale - thumb_width;
                if (expand > 0.0f)
                {
                    add_line_quad(quads_, client_width - scrollbar_width, 0.0f, scrollbar_width, client_height,
                                  thumb_color, sample, scrollbar::track_alpha * expand);
                }
                static const auto thumb_mask = make_thumb_mask();
                const auto *thumb_region = atlas_.find_or_add_mask(scrollbar::thumb_mask_key, static_cast<std::uint32_t>(scrollbar::mask_size), static_cast<std::uint32_t>(scrollbar::mask_size), thumb_mask);
                if (thumb_region == nullptr)
                {
                    add_line_quad(quads_, thumb_x, geometry.thumb_y, thumb_width, geometry.thumb_height, thumb_color,
                                  sample, scrollbar::thumb_alpha);
                }
                else
                {
                    const auto cap = std::min(scrollbar::expanded_thumb_width * scrollbar::thumb_cap_ratio * scale,
                                             geometry.thumb_height * 0.5f);
                    const auto u0 = static_cast<float>(thumb_region->x) * atlas_scale;
                    const auto u1 = static_cast<float>(thumb_region->x + scrollbar::mask_size) * atlas_scale;
                    const auto v0 = static_cast<float>(thumb_region->y) * atlas_scale;
                    const auto v1 = static_cast<float>(thumb_region->y + scrollbar::mask_size) * atlas_scale;
                    const auto vm = static_cast<float>(thumb_region->y + scrollbar::mask_size / 2) * atlas_scale;
                    add_mask_quad(quads_, thumb_x, geometry.thumb_y, thumb_width, cap, u0, v0, u1, vm, thumb_color,
                                  scrollbar::thumb_alpha);
                    add_mask_quad(quads_, thumb_x, geometry.thumb_y + cap, thumb_width,
                                  std::max(0.0f, geometry.thumb_height - cap * 2.0f), u0, vm, u1, vm, thumb_color,
                                  scrollbar::thumb_alpha);
                    add_mask_quad(quads_, thumb_x, geometry.thumb_y + geometry.thumb_height - cap, thumb_width, cap,
                                  u0, vm, u1, v1, thumb_color, scrollbar::thumb_alpha);
                }

                if (expand > 0.01f)
                {
                    static const auto up_mask = make_arrow_mask(false);
                    static const auto down_mask = make_arrow_mask(true);
                    const auto arrow_center = client_width - scrollbar::arrow_center_from_right * scale;
                    const auto arrow_width = scrollbar::arrow_visual_width * scale;
                    const auto arrow_height = scrollbar::arrow_visual_height * scale;
                    const auto arrow_margin = scrollbar::arrow_visual_margin * scale;
                    const auto *up = atlas_.find_or_add_mask(scrollbar::arrow_up_mask_key, scrollbar::mask_size, scrollbar::mask_size,
                                                             up_mask);
                    const auto *down = atlas_.find_or_add_mask(scrollbar::arrow_down_mask_key, scrollbar::mask_size, scrollbar::mask_size,
                                                               down_mask);
                    for (int i = 0; i < 2; ++i)
                    {
                        const auto *region = i == 0 ? up : down;
                        if (region == nullptr)
                        {
                            continue;
                        }
                        const auto u0 = static_cast<float>(region->x) * atlas_scale;
                        const auto v0 = static_cast<float>(region->y) * atlas_scale;
                        const auto arrow_y = i == 0 ? arrow_margin
                                                   : client_height - arrow_margin - arrow_height;
                        add_mask_quad(quads_, std::round(arrow_center - arrow_width * 0.5f),
                                      std::round(arrow_y), arrow_width, arrow_height, u0, v0,
                                      static_cast<float>(region->x + scrollbar::mask_size) * atlas_scale,
                                      static_cast<float>(region->y + scrollbar::mask_size) * atlas_scale, thumb_color,
                                      scrollbar::thumb_alpha * expand);
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

        for (auto it = image_generations_.begin(); it != image_generations_.end();)
        {
            if (!visible_images.contains(it->first))
            {
                renderer_.remove_image_texture(it->first);
                it = image_generations_.erase(it);
            }
            else
            {
                ++it;
            }
        }
        LOG2("image cache textures=%zu quads=%zu", image_generations_.size(), image_quads_.size());
        renderer_.set_image_quads(image_quads_);
    }

    void apply_text_correction()
    {
        float contrast = 0.0f;
        std::array<float, 4> gamma_ratios{};
        if (font_turning_contrast(config_.font.turning))
        {
            contrast = text_.grayscale_enhanced_contrast();
        }
        if (font_turning_gamma(config_.font.turning))
        {
            gamma_ratios = text_.gamma_ratios();
        }
        renderer_.set_text_correction(contrast, gamma_ratios);
    }

    static bool is_device_lost_error(const winrt::hresult_error &error) noexcept
    {
        const auto result = static_cast<HRESULT>(error.code());
        return result == DXGI_ERROR_DEVICE_REMOVED || result == DXGI_ERROR_DEVICE_RESET ||
               result == DXGI_ERROR_DEVICE_HUNG || result == DXGI_ERROR_DRIVER_INTERNAL_ERROR;
    }

    bool recover_renderer()
    {
        static constexpr std::array<DWORD, 4> retry_delays{0, 500, 2000, 8000};
        for (const auto delay : retry_delays)
        {
            if (delay != 0)
            {
                const auto wait = win32::wait_one(stop_event_, delay);
                if (wait.signaled() || wait.abandoned())
                {
                    return false;
                }
            }

            LOG2("device lost recovery attempt delay=%u", static_cast<unsigned>(delay));
            try
            {
                renderer_.reset();
                renderer_.initialize(window_);
                apply_text_correction();
                const auto &clear = palette().default_background;
                renderer_.set_clear_color(static_cast<float>(clear.r) / 255.0f,
                                          static_cast<float>(clear.g) / 255.0f,
                                          static_cast<float>(clear.b) / 255.0f,
                                          (config_.window.backdrop != backdrop_mode::none) ? 0.0f : 1.0f);
                atlas_.reset();
                color_atlas_.mark_dirty();
                force_full_redraw_ = true;
                LOG2("device lost recovery succeeded delay=%u", static_cast<unsigned>(delay));
                return true;
            }
            catch (const winrt::hresult_error &error)
            {
                LOG2("device lost recovery failed delay=%u hr=%08x", static_cast<unsigned>(delay),
                     static_cast<unsigned>(error.code()));
            }
        }
        return false;
    }

    DWORD wait_timeout() const noexcept
    {
        DWORD timeout = INFINITE;
        if (config_.text.blink_enable && config_.cursor.blink_rate != 0 && has_blinking_text_ && focused_ &&
            !config_.accessibility.reduce_motion)
        {
            const auto interval = std::max<std::uint64_t>(1, config_.cursor.blink_rate);
            const auto phase = static_cast<DWORD>(::GetTickCount64() % interval);
            timeout = std::min<DWORD>(timeout, static_cast<DWORD>(interval - phase));
        }
        if (config_.cursor.blink && config_.cursor.blink_rate != 0 && !config_.accessibility.reduce_motion && focused_ && cursor_.visible && cursor_.blinking &&
            !selection_.active &&
            composition_.empty() &&
            scroll_pixels_ == 0.0)
        {
            const auto blink_interval = std::max<std::uint64_t>(1, config_.cursor.blink_rate);
            const auto phase = static_cast<DWORD>(::GetTickCount64() % blink_interval);
            timeout = std::min<DWORD>(timeout, static_cast<DWORD>(blink_interval - phase));
        }
        return timeout;
    }

    void run()
    {
        LOG2("render thread start");
        renderer_.initialize(window_);
        LOG2("render initialized");
        apply_text_correction();
        const auto &clear = palette().default_background;
        renderer_.set_clear_color(static_cast<float>(clear.r) / 255.0f, static_cast<float>(clear.g) / 255.0f,
                                  static_cast<float>(clear.b) / 255.0f, (config_.window.backdrop != backdrop_mode::none) ? 0.0f : 1.0f);
        renderer_.render();
        LOG2("render first frame");
        bool cursor_dirty = true;
        bool first_frame = true;

        for (;;)
        {
            if (stop_.load(std::memory_order_relaxed))
            {
                break;
            }

            if (force_full_redraw_)
            {
                columns = 0;
                rows = 0;
                history_size_ = 0;
                force_full_redraw_ = false;
            }

            auto timeout = wait_timeout();
            if (first_frame)
            {
                timeout = 0;
                first_frame = false;
            }

            if (scrollbar_animation_active_)
            {
                // Let the compositor pace animation at the display refresh rate,
                // including high-refresh displays. Handle indexes are
                // WAIT_OBJECT_0 + n; a compositor tick is WAIT_OBJECT_0 + count.
                HANDLE waits[] = {stop_event_.get(), damage_event_.get(), wake_event_.get()};
                const auto result = ::DCompositionWaitForCompositorClock(3, waits, INFINITE);
                if (result == WAIT_FAILED)
                {
                    win32::throw_last_error();
                }
                if (result == WAIT_OBJECT_0)
                {
                    break;
                }
            }
            else
            {
                const auto wait = win32::wait_any(stop_event_, damage_event_, wake_event_, timeout);
                if (wait.abandoned() || (wait.signaled() && wait.index == 0))
                {
                    break;
                }
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
                text_.set_size(config_.font.size * static_cast<float>(dpi_) / 96.0f);
                atlas_.reset();
                cursor_dirty = true;
            }
            const auto metrics = text_.metrics();
            const auto scrollbar_width = scrollbar::default_width * static_cast<float>(dpi) / 96.0f;
            const auto content_width = std::max(1.0f, static_cast<float>(width) - scrollbar_width);
            const auto desired_columns =
                std::max<std::size_t>(1, static_cast<std::size_t>(content_width / metrics.advance));
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
            screen::selection_range selection{};
            std::u16string ime_composition;
            std::size_t ime_composition_x{};
            std::size_t ime_composition_y{};

            {
                std::unique_lock<win32::srwlock> lock{console_mutex_};
                bool resized = false;
                if (desired_columns != columns || desired_rows != rows)
                {
                    LOG2("console resize old=%zux%zu new=%zux%zu", columns, rows, desired_columns, desired_rows);
                    console_.resize(desired_columns, desired_rows);
                    columns = desired_columns;
                    rows = desired_rows;
                    cell_state_.assign(columns * (rows + 1), screen::cell{});
                    resized = true;
                }

                console_.set_scroll_row_height(static_cast<float>(row_height_px));
                console_.set_cell_metrics(metrics.advance, static_cast<float>(row_height_px));
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
                selection = console_.selection();
                ime_composition = console_.ime_composition();
                ime_composition_x = cursor.x;
                ime_composition_y = cursor.y;
                focused = console_.has_focus();
                scrollbar_hovered = console_.scrollbar_hovered();
                if (config_.bell.mode != bell_mode_full::none && console_.state().bell_count != bell_count_)
                {
                    bell_count_ = console_.state().bell_count;
                    (void)::PostMessageW(window_, WM_APP + 0x102, 0, 0);
                }
                if (console_.state().take_title_changed())
                {
                    const auto &title = console_.state().title;
                    window_title.assign(reinterpret_cast<const wchar_t *>(title.data()), title.size());
                    title_changed = true;
                }
            }

            if (title_changed && !config_.title.force_corehost)
            {
                ::SetWindowTextW(window_, window_title.c_str());
            }

            const bool cursor_changed = cursor_dirty || !same_cursor(cursor_, cursor);
            const bool selection_changed = !same_selection(selection_, selection);
            const bool hover_changed = scrollbar_hovered != scrollbar_hovered_;
            const bool focus_changed = focused != focused_;
            const auto now = ::GetTickCount64();
            const bool redraw_requested = redraw_requested_.exchange(false, std::memory_order_relaxed);
            const auto blink_interval = std::max<std::uint64_t>(1, config_.cursor.blink_rate);
            const bool blink_on = !config_.cursor.blink || config_.cursor.blink_rate == 0 ||
                                  (focused && ((now / blink_interval) % 2 == 0));
            const bool blink_changed = blink_on != blink_on_;
            const auto text_blink_interval = std::max<std::uint64_t>(1, config_.cursor.blink_rate);
            const bool text_blink_on =
                !config_.text.blink_enable || config_.cursor.blink_rate == 0 ||
                (focused && ((now / text_blink_interval) % 2 == 0));
            const bool text_blink_changed = text_blink_on != text_blink_on_;

            RECT dirty_rect{};
            bool dirty_valid = false;
            {
                auto include = [&](LONG left, LONG top, LONG right, LONG bottom) {
                    left = std::clamp<LONG>(left, 0, static_cast<LONG>(width));
                    top = std::clamp<LONG>(top, 0, static_cast<LONG>(height));
                    right = std::clamp<LONG>(right, 0, static_cast<LONG>(width));
                    bottom = std::clamp<LONG>(bottom, 0, static_cast<LONG>(height));
                    if (left >= right || top >= bottom)
                    {
                        return;
                    }
                    if (!dirty_valid)
                    {
                        dirty_rect = RECT{left, top, right, bottom};
                        dirty_valid = true;
                    }
                    else
                    {
                        dirty_rect.left = std::min(dirty_rect.left, left);
                        dirty_rect.top = std::min(dirty_rect.top, top);
                        dirty_rect.right = std::max(dirty_rect.right, right);
                        dirty_rect.bottom = std::max(dirty_rect.bottom, bottom);
                    }
                };
                const bool full_dirty = scroll_changed || dpi_changed || selection_changed || hover_changed ||
                                        focus_changed || text_blink_changed || scrollbar_animation_active_ || redraw_requested;
                if (full_dirty)
                {
                    include(0, 0, static_cast<LONG>(width), static_cast<LONG>(height));
                }
                else if (changed && !scroll_changed)
                {
                    for (std::size_t y = 0; y < updates_.size() && y < rows; ++y)
                    {
                        const auto &row = updates_[y];
                        if (row.cells.empty())
                        {
                            continue;
                        }
                        for (std::size_t x = 0; x < columns; ++x)
                        {
                            if (row.cells[x] != cell_state_[y * columns + x])
                            {
                                include(0,
                                        static_cast<LONG>(std::floor(static_cast<float>(y) * static_cast<float>(row_height_px))),
                                        static_cast<LONG>(width),
                                        static_cast<LONG>(std::ceil(static_cast<float>(y + 1) * static_cast<float>(row_height_px))));
                                break;
                            }
                        }
                    }
                }
                if (cursor_changed || blink_changed)
                {
                    const auto cursor_extent = [&](const screen::cursor_state &value) {
                        if (!value.visible || value.x >= columns || value.y >= rows)
                        {
                            return;
                        }
                        include(static_cast<LONG>(std::floor(static_cast<float>(value.x) * metrics.advance)),
                                static_cast<LONG>(std::floor(static_cast<float>(value.y) * static_cast<float>(row_height_px))),
                                static_cast<LONG>(std::ceil(static_cast<float>(value.x + 1) * metrics.advance)),
                                static_cast<LONG>(std::ceil(static_cast<float>(value.y + 1) * static_cast<float>(row_height_px))));
                    };
                    cursor_extent(cursor_);
                    cursor_extent(cursor);
                }
                if (!dirty_valid)
                {
                    include(0, 0, static_cast<LONG>(width), static_cast<LONG>(height));
                }
            }

            cursor_ = cursor;
            selection_ = selection;
            composition_ = std::move(ime_composition);
            composition_x_ = ime_composition_x;
            composition_y_ = ime_composition_y;
            focused_ = focused;
            scrollbar_hovered_ = scrollbar_hovered;
            bool scrollbar_animating = false;
            {
                const auto target = scrollbar_hovered_ ? 1.0f : 0.0f;
                if (config_.accessibility.reduce_motion)
                {
                    scrollbar_expand_ = target;
                }
                else if (scrollbar_expand_ != target)
                {
                    constexpr float step = 1.0f / static_cast<float>(scrollbar::expand_frames);
                    scrollbar_expand_ = target > scrollbar_expand_ ? std::min(target, scrollbar_expand_ + step)
                                                                   : std::max(target, scrollbar_expand_ - step);
                    scrollbar_animating = true;
                }
            }
            scrollbar_animation_active_ = scrollbar_animating;
            blink_on_ = blink_on;
            text_blink_on_ = text_blink_on;
            draw_cursor_ = cursor_.visible && focused_ && scroll_pixels_ == 0.0 && !selection_.active && composition_.empty() &&
                           (cursor_.blinking ? blink_on_ : true);
            cursor_dirty = false;
            try
            {
                renderer_.resize(width, height);
                client_width_px_ = static_cast<float>(width);
                client_height_px_ = static_cast<float>(height);

                if (changed || cursor_changed || selection_changed || hover_changed || scrollbar_animating ||
                    focus_changed || blink_changed || text_blink_changed || redraw_requested || dpi_changed)
                {
                    if (scroll_changed)
                    {
                        apply_scrolled_rows();
                    }
                    else if (changed)
                    {
                        apply_updates();
                    }
                    if (changed && text_changed_callback_)
                    {
                        text_changed_callback_(text_changed_callback_data_);
                    }
                    if (atlas_upload_requested_.exchange(false, std::memory_order_relaxed))
                    {
                        atlas_.mark_dirty();
                        color_atlas_.mark_dirty();
                    }
                    if (redraw_requested || changed)
                    {
                        LOG2("redraw rows: %zu %zu %zu %zu %zu changed=%d screen=%04x cell0=%04x", row_nonspace(0),
                             row_nonspace(1), row_nonspace(2), row_nonspace(3), row_nonspace(rows - 1), changed ? 1 : 0,
                             console_.buffer().at(0, 0).primary(), cell_state_[0].primary());
                    }
                    if (dirty_valid)
                    {
                        renderer_.set_present_dirty_rect(dirty_rect);
                    }

                    build_quads();
                    renderer_.set_text_texture(atlas_.pixels(), atlas_.size(), atlas_.size());
                    LOG2("quads=%zu colorquads=%zu scrollf=%.3f", quads_.size(), color_quads_.size(), scroll_fraction_);
                    renderer_.render();
                    if (renderer_.device_lost())
                    {
                        throw winrt::hresult_error{DXGI_ERROR_DEVICE_REMOVED};
                    }
                    LOG2("render done");
                }
            }
            catch (const winrt::hresult_error &error)
            {
                if (!is_device_lost_error(error))
                {
                    throw;
                }
                LOG2("device lost detected hr=%08x", static_cast<unsigned>(error.code()));
                if (!recover_renderer())
                {
                    LOG2("device lost recovery exhausted; closing window");
                    stop_.store(true, std::memory_order_relaxed);
                    stop_event_.set();
                    (void)::PostMessageW(window_, WM_CLOSE, 0, 0);
                    break;
                }
                cursor_dirty = true;
                first_frame = true;
            }
        }

        renderer_.reset();
    }

    // Zero means "not sampled yet"; the first compositor tick must perform a
    // full row copy even when the console already has the default grid size.
    std::size_t columns{};
    std::size_t rows{};

    console &console_;
    win32::srwlock &console_mutex_;
    HWND window_{};
    d3d12ui::d3d12_window_renderer renderer_;
    const corehost_config &config_;
    const color_scheme &scheme_;
    [[nodiscard]] const console_palette &palette() const noexcept
    {
        return scheme_.palette;
    }

    static const color_scheme &select_scheme(const color_config &color, bool dark) noexcept
    {
        return (color.mode == theme_mode::light || (color.mode == theme_mode::auto_mode && !dark)) ? color.light
                                                                                                    : color.dark;
    }

    static std::array<uitext::font_fallback_entry, max_font_fallbacks> make_fallback_families(const font_config &config) noexcept
    {
        std::array<uitext::font_fallback_entry, max_font_fallbacks> result{};
        const auto count = std::min(config.family_fallback_list.count, max_font_fallbacks);
        for (std::size_t i = 0; i < count; ++i)
        {
            const auto &entry = config.family_fallback_list.entries[i];
            result[i] = uitext::font_fallback_entry{
                win32::wcstring_view{entry.family},
                DWRITE_UNICODE_RANGE{entry.range.first, entry.range.last},
            };
        }
        return result;
    }

    static std::size_t make_fallback_count(const font_config &config) noexcept
    {
        return std::clamp<std::size_t>(config.family_fallback_list.count, 1, max_font_fallbacks);
    }

    std::array<uitext::font_fallback_entry, max_font_fallbacks> fallback_families_;
    std::size_t fallback_count_{};
    uitext::text_engine text_;
    uitext::cell_atlas atlas_{text_};
    uitext::color_cell_atlas color_atlas_{text_};
    UINT dpi_ = 96;

    std::vector<screen::cell> cell_state_ = std::vector<screen::cell>(columns * (rows + 1));
    screen::style_table styles_;
    win32::handle_view damage_event_{};
    std::uint64_t bell_count_{};
    screen::cursor_state cursor_{};
    screen::selection_range selection_{};
    screen::cell_hyperlink hovered_hyperlink_{screen::no_cell_hyperlink};
    std::u16string composition_;
    std::size_t composition_x_{};
    std::size_t composition_y_{};
    bool focused_{true};
    bool scrollbar_hovered_{};
    float scrollbar_expand_{};
    bool blink_on_{true};
    bool text_blink_on_{true};
    bool has_blinking_text_{};
    bool draw_cursor_{true};
    screen::grapheme_store graphemes_;
    std::vector<d3d12ui::text_quad> quads_;
    std::vector<d3d12ui::text_quad> color_quads_;
    std::vector<d3d12ui::image_quad> image_quads_;
    std::flat_map<std::uint32_t, std::uint64_t> image_generations_;
    std::vector<screen::row_block> updates_;
    std::vector<screen::row_block> scroll_rows_;
    double scroll_pixels_{};
    double scroll_fraction_{};
    float client_width_px_{};
    float client_height_px_{};
    std::size_t display_rows_{};
    std::size_t history_size_{};
    win32::basic_thread thread_;
    win32::event stop_event_{win32::create_tag, true, false};
    win32::event wake_event_{win32::create_tag, false, false};
    std::atomic_bool stop_{};
    bool scrollbar_animation_active_{};
    bool force_full_redraw_{};
    text_changed_callback text_changed_callback_{};
    void *text_changed_callback_data_{};
    std::atomic_bool redraw_requested_{};
    std::atomic_bool atlas_upload_requested_{};
};

} // namespace corehost::window
