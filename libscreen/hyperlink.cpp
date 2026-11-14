#include "hyperlink.hpp"

#include <limits>
#include <stdexcept>
#include <utility>

namespace corehost::screen
{

cell_hyperlink hyperlink_table::intern(std::wstring_view uri, std::wstring_view id)
{
    // Use a separator that cannot occur in a URI for the lookup key. The
    // table remains a small screen-wide interning set; entries are stable.
    std::wstring key;
    key.reserve(uri.size() + id.size() + 1);
    key.append(uri);
    key.push_back(L'\n');
    key.append(id);

    const auto found = ids_.find(key);
    if (found != ids_.end())
    {
        return found->second;
    }

    if (links_.size() > static_cast<std::size_t>(std::numeric_limits<std::underlying_type_t<cell_hyperlink>>::max()))
    {
        throw std::length_error("hyperlink table exhausted its 32-bit ID space");
    }

    const auto value = static_cast<cell_hyperlink>(links_.size());
    links_.push_back(hyperlink{std::wstring{uri}, std::wstring{id}});
    ids_.emplace(std::move(key), value);
    return value;
}

const hyperlink &hyperlink_table::at(cell_hyperlink value) const noexcept
{
    return links_[static_cast<std::size_t>(std::to_underlying(value))];
}

std::size_t hyperlink_table::size() const noexcept
{
    return links_.size();
}

} // namespace corehost::screen
