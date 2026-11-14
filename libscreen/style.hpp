#pragma once

#include "cell.hpp"

#include <compare>
#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace corehost::screen
{

enum class color_kind : std::uint8_t
{
    default_color = 0,
    indexed = 1,
    rgb = 2,
};

struct color final
{
    color_kind kind{color_kind::default_color};
    std::uint32_t value{};

    friend constexpr auto operator<=>(const color &, const color &) noexcept = default;
};

// Full style is stored once in a screen-wide table. A cell only carries the
// 32-bit style ID; legacy Console API attributes are projected on demand.
struct text_style final
{
    color foreground{};
    color background{};
    color underline{};
    std::uint16_t attributes{};
    std::uint16_t legacy_attributes{};

    friend constexpr auto operator<=>(const text_style &, const text_style &) noexcept = default;
};

struct text_style_hash final
{
    [[nodiscard]] std::size_t operator()(const text_style &style) const noexcept;
};

class style_table final
{
  public:
    [[nodiscard]] cell_style intern(const text_style &style);
    [[nodiscard]] const text_style &at(cell_style id) const noexcept;
    [[nodiscard]] std::size_t size() const noexcept;

  private:
    std::vector<text_style> styles_{text_style{}};
    std::unordered_map<text_style, cell_style, text_style_hash> ids_;
};

} // namespace corehost::screen
