#pragma once

#include "cell.hpp"
#include "raw_buffer.hpp"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <flat_map>
#include <limits>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

namespace corehost::screen
{

struct hyperlink final
{
    vtparser::raw_u16_string uri;
    vtparser::raw_u16_string id;
};

class hyperlink_table final
{
  public:
    [[nodiscard]] cell_hyperlink intern(std::u16string_view uri, std::u16string_view id = {})
    {
        // A separator that cannot occur in a URI keeps the lookup key stable.
        key_scratch_.clear();
        key_scratch_.reserve(uri.size() + id.size() + 1);
        key_scratch_.append(uri);
        key_scratch_.push_back(u'\n');
        key_scratch_.append(id);

        const auto found = ids_.find(key_scratch_);
        if (found != ids_.end())
        {
            return found->second;
        }

        const auto max_id =
            static_cast<std::size_t>(std::numeric_limits<std::underlying_type_t<cell_hyperlink>>::max());
        assert(links_.size() <= max_id);

        const auto value = static_cast<cell_hyperlink>(links_.size());
        links_.push_back(hyperlink{vtparser::raw_u16_string{uri.begin(), uri.end()},
                                   vtparser::raw_u16_string{id.begin(), id.end()}});
        ids_.emplace(key_scratch_, value);
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
    vtparser::raw_byte_vector<hyperlink> links_{{}};
    std::flat_map<vtparser::raw_u16_string, cell_hyperlink> ids_;
    vtparser::raw_u16_string key_scratch_;
};

} // namespace corehost::screen
