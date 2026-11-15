#pragma once

#include "cell.hpp"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <flat_map>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace corehost::screen
{

struct hyperlink final
{
    std::wstring uri;
    std::wstring id;
};

class hyperlink_table final
{
  public:
    [[nodiscard]] cell_hyperlink intern(std::wstring_view uri, std::wstring_view id = {})
    {
        // A separator that cannot occur in a URI keeps the lookup key stable.
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

        const auto max_id = static_cast<std::size_t>(std::numeric_limits<std::underlying_type_t<cell_hyperlink>>::max());
        if (links_.size() > max_id)
        {
            throw std::length_error("hyperlink table exhausted its 32-bit ID space");
        }

        const auto value = static_cast<cell_hyperlink>(links_.size());
        links_.push_back(hyperlink{std::wstring{uri}, std::wstring{id}});
        ids_.emplace(std::move(key), value);
        return value;
    }

    [[nodiscard]] const hyperlink &at(cell_hyperlink value) const noexcept
    {
        const auto index = static_cast<std::size_t>(std::to_underlying(value));
        assert(index < links_.size());
        return links_[index];
    }

    [[nodiscard]] std::size_t size() const noexcept
    {
        return links_.size();
    }

  private:
    std::vector<hyperlink> links_{{}};
    std::flat_map<std::wstring, cell_hyperlink> ids_;
};

} // namespace corehost::screen
