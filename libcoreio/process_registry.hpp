#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace corehost::coreio
{

class process_registry final
{
  public:
    void add(std::uintptr_t process) noexcept
    {
        if (process == 0)
        {
            return;
        }
        if (std::ranges::find(processes_, process) == processes_.end())
        {
            processes_.push_back(process);
        }
    }

    void remove(std::uintptr_t process) noexcept
    {
        std::erase(processes_, process);
    }

    [[nodiscard]] std::size_t size() const noexcept
    {
        return processes_.size();
    }

    [[nodiscard]] std::span<const std::uintptr_t> processes() const noexcept
    {
        return processes_;
    }

    void clear() noexcept
    {
        processes_.clear();
    }

  private:
    std::vector<std::uintptr_t> processes_;
};

} // namespace corehost::coreio