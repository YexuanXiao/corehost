#pragma once

#include "cell.hpp"

#include <cstddef>
#include <string>
#include <string_view>
#include <unordered_map>
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
    [[nodiscard]] hyperlink_id intern(std::wstring_view uri, std::wstring_view id = {});
    [[nodiscard]] const hyperlink &at(hyperlink_id value) const noexcept;
    [[nodiscard]] std::size_t size() const noexcept;

  private:
    std::vector<hyperlink> links_{{}};
    std::unordered_map<std::wstring, hyperlink_id> ids_;
};

} // namespace corehost::screen
