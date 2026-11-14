#pragma once

#include <cstdint>
#include <cstddef>
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
    [[nodiscard]] std::uint32_t intern(const image_fragment &fragment);
    [[nodiscard]] const image_fragment &at(std::uint32_t id) const noexcept;
    [[nodiscard]] std::size_t size() const noexcept;

  private:
    std::vector<image_fragment> fragments_{{}};
};

} // namespace corehost::screen
