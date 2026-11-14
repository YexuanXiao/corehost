#include "style.hpp"

#include <functional>
#include <limits>
#include <stdexcept>
#include <utility>

namespace corehost::screen
{

namespace
{
constexpr std::uint64_t color_bits(const color &value) noexcept
{
    return static_cast<std::uint64_t>(std::to_underlying(value.kind)) << 32 | static_cast<std::uint64_t>(value.value);
}

constexpr std::uint64_t mix(std::uint64_t hash, std::uint64_t value) noexcept
{
    hash ^= value + 0x9E3779B97F4A7C15ull + (hash << 6) + (hash >> 2);
    return hash;
}
} // namespace

std::size_t text_style_hash::operator()(const text_style &style) const noexcept
{
    auto hash = mix(0, color_bits(style.foreground));
    hash = mix(hash, color_bits(style.background));
    hash = mix(hash, color_bits(style.underline));
    hash = mix(hash, style.attributes);
    hash = mix(hash, style.legacy_attributes);
    return static_cast<std::size_t>(hash);
}

cell_style style_table::intern(const text_style &style)
{
    const auto found = ids_.find(style);
    if (found != ids_.end())
    {
        return found->second;
    }

    if (styles_.size() > static_cast<std::size_t>(std::numeric_limits<std::underlying_type_t<cell_style>>::max()))
    {
        throw std::length_error("style table exhausted its 32-bit ID space");
    }

    const auto id = static_cast<cell_style>(styles_.size());
    styles_.push_back(style);
    ids_.emplace(style, id);
    return id;
}

const text_style &style_table::at(cell_style id) const noexcept
{
    return styles_[static_cast<std::size_t>(std::to_underlying(id))];
}

std::size_t style_table::size() const noexcept
{
    return styles_.size();
}

} // namespace corehost::screen
