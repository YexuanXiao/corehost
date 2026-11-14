#include "image.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>

namespace corehost::screen
{

std::uint32_t image_table::intern(const image_fragment &fragment)
{
    const auto found = std::find(fragments_.begin(), fragments_.end(), fragment);
    if (found != fragments_.end())
    {
        return static_cast<std::uint32_t>(std::distance(fragments_.begin(), found));
    }

    if (fragments_.size() > std::numeric_limits<std::uint32_t>::max())
    {
        throw std::length_error("image table exhausted its 32-bit ID space");
    }

    const auto id = static_cast<std::uint32_t>(fragments_.size());
    fragments_.push_back(fragment);
    return id;
}

const image_fragment &image_table::at(std::uint32_t id) const noexcept
{
    return fragments_[id];
}

std::size_t image_table::size() const noexcept
{
    return fragments_.size();
}

} // namespace corehost::screen
