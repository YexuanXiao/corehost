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
    [[nodiscard]] cell_hyperlink intern(std::wstring_view uri, std::wstring_view id = {});
    [[nodiscard]] const hyperlink &at(cell_hyperlink value) const noexcept;
    [[nodiscard]] std::size_t size() const noexcept;

  private:
    std::vector<hyperlink> links_{{}};
    std::unordered_map<std::wstring, cell_hyperlink> ids_;
};

} // namespace corehost::screen
