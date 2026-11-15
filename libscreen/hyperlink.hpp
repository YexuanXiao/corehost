#pragma once

#include "cell.hpp"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <flat_map>
#include <limits>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace corehost::screen
{

struct hyperlink final
{
    std::u16string uri;
    std::u16string id;
};

class hyperlink_table final
{
  public:
    [[nodiscard]] cell_hyperlink intern(std::u16string_view uri, std::u16string_view id = {})
    {
        // A separator that cannot occur in a URI keeps the lookup key stable.
        std::u16string key;
        key.reserve(uri.size() + id.size() + 1);
        key.append(uri);
        key.push_back(u'\n');
        key.append(id);

        const auto found = ids_.find(key);
        if (found != ids_.end())
        {
            return found->second;
        }

        const auto max_id = static_cast<std::size_t>(std::numeric_limits<std::underlying_type_t<cell_hyperlink>>::max());
        assert(links_.size() <= max_id);

        const auto value = static_cast<cell_hyperlink>(links_.size());
        links_.push_back(hyperlink{std::u16string{uri}, std::u16string{id}});
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
    std::flat_map<std::u16string, cell_hyperlink> ids_;
};

} // namespace corehost::screen
