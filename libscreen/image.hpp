#pragma once

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <limits>
#include <vector>

namespace corehost::screen
{

enum class image_layer : std::uint8_t
{
    below = 0,
    replace = 1,
    above = 2,
};

// A cell-sized fragment of an image. Actual pixel storage/decoding belongs to
// the renderer; the screen buffer only needs stable placement metadata.
struct image_fragment final
{
    std::uint32_t image_id{};
    std::uint32_t placement_id{};
    std::uint32_t source_x{};
    std::uint32_t source_y{};
    std::uint32_t source_width{};
    std::uint32_t source_height{};
    image_layer layer{image_layer::replace};

    friend constexpr bool operator==(const image_fragment &, const image_fragment &) noexcept = default;
};

class image_table final
{
  public:
    [[nodiscard]] std::uint32_t intern(const image_fragment &fragment)
    {
        const auto found = std::find(fragments_.begin(), fragments_.end(), fragment);
        if (found != fragments_.end())
        {
            return static_cast<std::uint32_t>(std::distance(fragments_.begin(), found));
        }

        assert(fragments_.size() <= std::numeric_limits<std::uint32_t>::max());

        const auto id = static_cast<std::uint32_t>(fragments_.size());
        fragments_.push_back(fragment);
        return id;
    }

    [[nodiscard]] const image_fragment &at(std::uint32_t id) const noexcept
    {
        assert(id < fragments_.size());
        return fragments_[id];
    }

    [[nodiscard]] std::size_t size() const noexcept
    {
        return fragments_.size();
    }

  private:
    std::vector<image_fragment> fragments_{{}};
};

} // namespace corehost::screen
