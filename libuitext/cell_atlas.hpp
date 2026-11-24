#pragma once

#include "text_engine.hpp"

#include <algorithm>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace corehost::uitext
{

struct atlas_region final
{
    std::uint32_t x{};
    std::uint32_t y{};
    std::uint32_t width{};
    std::uint32_t height{};
    int left{};
    int top{};
};

// Alpha-only atlas. Only a single BMP code unit is cached persistently.
// Bold glyphs, non-BMP scalars and grapheme clusters are rasterized into the
// transient top region and are rebuilt for every frame that uses them.
class cell_atlas final
{
  public:
    cell_atlas(text_engine &engine, std::uint32_t size = 1024)
        : engine_{engine}, size_{size}, pixels_(static_cast<std::size_t>(size) * size, 0),
          transient_rows_{std::max<std::uint32_t>(2, size / 4)}
    {
        assert(size_ > 0);
        pixels_.front() = 0xFF;
        shelf_y_ = transient_rows_;
        dirty_ = true;
    }

    void begin_frame() noexcept
    {
        transient_entries_.clear();
        transient_x_ = 0;
        transient_y_ = 1;
        transient_height_ = 0;
    }

    [[nodiscard]] const atlas_region *find_or_add(std::u16string_view text, bool bold = false)
    {
        if (text.empty())
        {
            return nullptr;
        }

        const bool cacheable = is_cacheable(text, bold);
        auto key = std::u16string{text};
        if (cacheable)
        {
            const auto found = entries_.find(key);
            if (found != entries_.end())
            {
                return &found->second;
            }
        }
        else
        {
            const auto found = transient_entries_.find(key);
            if (found != transient_entries_.end())
            {
                return &found->second;
            }
        }

        engine_.rasterize(text, scratch_, bold);
        if (scratch_.empty() || scratch_.front().alpha.empty())
        {
            return nullptr;
        }

        const auto &glyph = scratch_.front();
        const auto width = static_cast<std::uint32_t>(glyph.width);
        const auto height = static_cast<std::uint32_t>(glyph.height);
        if (width + 2 > size_ || height + 2 > size_)
        {
            return nullptr;
        }

        atlas_region region{};
        if (!cacheable)
        {
            if (!allocate(width, height, transient_x_, transient_y_, transient_height_, transient_rows_, region))
            {
                return nullptr;
            }
        }
        else
        {
            if (!allocate(width, height, shelf_x_, shelf_y_, shelf_height_, size_, region))
            {
                return nullptr;
            }
        }

        region.left = glyph.left;
        region.top = glyph.top;
        write(region, glyph);
        dirty_ = true;

        if (!cacheable)
        {
            return &transient_entries_.emplace(std::move(key), region).first->second;
        }
        return &entries_.emplace(std::move(key), region).first->second;
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

  private:
    [[nodiscard]] static bool is_cacheable(std::u16string_view text, bool bold) noexcept
    {
        if (bold || text.size() != 1)
        {
            return false;
        }
        const auto value = text.front();
        return value < 0xD800 || value > 0xDFFF;
    }

    [[nodiscard]] bool allocate(std::uint32_t width, std::uint32_t height, std::uint32_t &x, std::uint32_t &y,
                                std::uint32_t &shelf_height, std::uint32_t limit, atlas_region &region) noexcept
    {
        if (x + width + 2 > size_)
        {
            x = 0;
            y += shelf_height + 2;
            shelf_height = 0;
        }
        if (y + height + 2 > limit)
        {
            return false;
        }

        region = atlas_region{x + 1, y + 1, width, height};
        x += width + 2;
        shelf_height = std::max(shelf_height, height);
        return true;
    }

    void write(const atlas_region &region, const glyph_bitmap &glyph) noexcept
    {
        for (std::uint32_t row = 0; row < region.height; ++row)
        {
            const auto source = glyph.alpha.subspan(static_cast<std::size_t>(row) * region.width, region.width);
            auto *destination =
                pixels_.data() + static_cast<std::size_t>(region.y + row) * size_ + region.x;
            std::copy(source.begin(), source.end(), destination);
        }
    }

    text_engine &engine_;
    std::uint32_t size_{};
    std::vector<std::uint8_t> pixels_;
    std::uint32_t transient_rows_{};
    std::unordered_map<std::u16string, atlas_region> entries_;
    std::unordered_map<std::u16string, atlas_region> transient_entries_;
    std::vector<glyph_bitmap> scratch_;
    std::uint32_t shelf_x_{};
    std::uint32_t shelf_y_{};
    std::uint32_t shelf_height_{};
    std::uint32_t transient_x_{};
    std::uint32_t transient_y_{1};
    std::uint32_t transient_height_{};
    bool dirty_{};
};

} // namespace corehost::uitext
