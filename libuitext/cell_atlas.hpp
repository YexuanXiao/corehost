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

// Alpha-only atlas for repeated terminal cell text. The atlas is owned by the
// UI thread; text_engine and its buffers are reused across insertions.
class cell_atlas final
{
  public:
    cell_atlas(text_engine &engine, std::uint32_t size = 1024)
        : engine_{engine}, size_{size}, pixels_(static_cast<std::size_t>(size) * size, 0)
    {
        assert(size_ > 0);
        pixels_.front() = 0xFF;
        dirty_ = true;
    }

    [[nodiscard]] const atlas_region *find_or_add(std::u16string_view text)
    {
        if (text.empty())
        {
            return nullptr;
        }

        auto key = std::u16string{text};
        const auto found = entries_.find(key);
        if (found != entries_.end())
        {
            return &found->second;
        }

        engine_.rasterize(text, scratch_);
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

        if (shelf_x_ + width + 2 > size_)
        {
            shelf_x_ = 0;
            shelf_y_ += shelf_height_ + 2;
            shelf_height_ = 0;
        }
        if (shelf_y_ + height + 2 > size_)
        {
            return nullptr;
        }

        const atlas_region region{shelf_x_ + 1, shelf_y_ + 1, width, height, glyph.left, glyph.top};
        for (std::uint32_t row = 0; row < height; ++row)
        {
            const auto source = glyph.alpha.subspan(static_cast<std::size_t>(row) * width, width);
            auto *destination = pixels_.data() + static_cast<std::size_t>(region.y + row) * size_ + region.x;
            std::copy(source.begin(), source.end(), destination);
        }

        shelf_x_ += width + 2;
        shelf_height_ = std::max(shelf_height_, height);
        dirty_ = true;
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
    text_engine &engine_;
    std::uint32_t size_{};
    std::vector<std::uint8_t> pixels_;
    std::unordered_map<std::u16string, atlas_region> entries_;
    std::vector<glyph_bitmap> scratch_;
    std::uint32_t shelf_x_{};
    std::uint32_t shelf_y_{};
    std::uint32_t shelf_height_{};
    bool dirty_{};
};

} // namespace corehost::uitext
