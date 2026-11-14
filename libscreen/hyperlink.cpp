#include "hyperlink.hpp"

#include <limits>
#include <stdexcept>
#include <utility>

namespace corehost::screen
{

hyperlink_id hyperlink_table::intern(std::wstring_view uri, std::wstring_view id)
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

    if (links_.size() > std::numeric_limits<hyperlink_id>::max())
    {
        throw std::length_error("hyperlink table exhausted its 32-bit ID space");
    }

    const auto value = static_cast<hyperlink_id>(links_.size());
    links_.push_back(hyperlink{std::wstring{uri}, std::wstring{id}});
    ids_.emplace(std::move(key), value);
    return value;
}

const hyperlink &hyperlink_table::at(hyperlink_id value) const noexcept
{
    return links_[value];
}

std::size_t hyperlink_table::size() const noexcept
{
    return links_.size();
}

} // namespace corehost::screen
