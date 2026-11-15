#pragma once

#include "cell.hpp"

#include <cassert>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <flat_map>
#include <limits>
#include <type_traits>
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

class style_table final
{
  public:
    [[nodiscard]] cell_style intern(const text_style &style)
    {
        const auto found = ids_.find(style);
        if (found != ids_.end())
        {
            return found->second;
        }

        const auto max_id = static_cast<std::size_t>(std::numeric_limits<std::underlying_type_t<cell_style>>::max());
        assert(styles_.size() <= max_id);

        const auto id = static_cast<cell_style>(styles_.size());
        styles_.push_back(style);
        ids_.emplace(style, id);
        return id;
    }

    [[nodiscard]] const text_style &at(cell_style id) const noexcept
    {
        const auto index = static_cast<std::size_t>(std::to_underlying(id));
        assert(index < styles_.size());
        return styles_[index];
    }

    [[nodiscard]] std::size_t size() const noexcept
    {
        return styles_.size();
    }

  private:
    std::vector<text_style> styles_{text_style{}};
    std::flat_map<text_style, cell_style> ids_;
};

} // namespace corehost::screen
