#pragma once

#include "text_engine.hpp"

#include <algorithm>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace corehost::uitext
{

struct color_atlas_region final
{
    std::uint32_t x{};
    std::uint32_t y{};
    std::uint32_t width{};
    std::uint32_t height{};
    std::int16_t left{};
    std::int16_t top{};
    glyph_color_kind kind{glyph_color_kind::monochrome};
};

static_assert(sizeof(color_atlas_region) <= 24);

// RGBA atlas for color glyph layers. This atlas is intentionally separate from
// cell_atlas: it never stores monochrome cell glyphs and does not cache
// clusters. Region storage and shelf allocation are reset every frame.
class color_cell_atlas final
{
  public:
    color_cell_atlas(text_engine &engine, std::uint32_t size = 1024)
        : engine_{engine}, size_{size}, pixels_(static_cast<std::size_t>(size) * size * 4, 0)
    {
        assert(size_ > 0);
        pixels_[0] = 0xFF;
        pixels_[1] = 0xFF;
        pixels_[2] = 0xFF;
        pixels_[3] = 0xFF;
        dirty_ = true;
    }

    void begin_frame() noexcept
    {
        regions_.clear();
        shelf_x_ = 0;
        shelf_y_ = 1;
        shelf_height_ = 0;
        dirty_ = false;
    }

    [[nodiscard]] std::span<const color_atlas_region> find_or_add(std::u16string_view text)
    {
        if (text.empty())
        {
            return {};
        }

        engine_.rasterize_color(text, scratch_);
        auto &regions = regions_;
        for (const auto &layer : scratch_)
        {
            const auto width = static_cast<std::uint32_t>(layer.width);
            const auto height = static_cast<std::uint32_t>(layer.height);
            if (width + 2 > size_ || height + 2 > size_)
            {
                continue;
            }

            if (shelf_x_ + width + 2 > size_)
            {
                shelf_x_ = 0;
                shelf_y_ += shelf_height_ + 2;
                shelf_height_ = 0;
            }
            if (shelf_y_ + height + 2 > size_)
            {
                break;
            }

            color_atlas_region region{shelf_x_ + 1, shelf_y_ + 1, width, height,
                                      static_cast<std::int16_t>(layer.left), static_cast<std::int16_t>(layer.top),
                                      layer.kind};
            const auto red = static_cast<std::uint8_t>((layer.rgb >> 16) & 0xFF);
            const auto green = static_cast<std::uint8_t>((layer.rgb >> 8) & 0xFF);
            const auto blue = static_cast<std::uint8_t>(layer.rgb & 0xFF);
            for (std::uint32_t row = 0; row < height; ++row)
            {
                const auto alpha_row = layer.alpha.subspan(static_cast<std::size_t>(row) * width, width);
                auto *destination = pixels_.data() +
                                    (static_cast<std::size_t>(region.y + row) * size_ + region.x) * 4;
                for (std::uint32_t column = 0; column < width; ++column)
                {
                    const auto alpha = static_cast<std::uint8_t>(
                        (static_cast<std::uint32_t>(alpha_row[column]) * layer.alpha_scale + 127) / 255);
                    destination[column * 4 + 0] = red;
                    destination[column * 4 + 1] = green;
                    destination[column * 4 + 2] = blue;
                    destination[column * 4 + 3] = alpha;
                }
            }

            regions.push_back(region);
            shelf_x_ += width + 2;
            shelf_height_ = std::max(shelf_height_, height);
            dirty_ = true;
        }

        return std::span<const color_atlas_region>{regions};
    }

    [[nodiscard]] std::span<const std::uint8_t> pixels() const noexcept
    {
        return pixels_;
    }

    [[nodiscard]] std::uint32_t size() const noexcept
    {
        return size_;
    }

    [[nodiscard]] bool dirty() const noexcept
    {
        return dirty_;
    }

    void clear_dirty() noexcept
    {
        dirty_ = false;
    }

    void mark_dirty() noexcept
    {
        dirty_ = true;
    }

  private:
    text_engine &engine_;
    std::uint32_t size_{};
    std::vector<std::uint8_t> pixels_;
    std::vector<color_atlas_region> regions_;
    std::vector<color_glyph_bitmap> scratch_;
    std::uint32_t shelf_x_{};
    std::uint32_t shelf_y_{1};
    std::uint32_t shelf_height_{};
    bool dirty_{};
};

} // namespace corehost::uitext
