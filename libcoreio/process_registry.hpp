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
    void add(std::uintptr_t process, std::uintptr_t group = 0) noexcept
    {
        if (process == 0)
        {
            return;
        }
        if (std::ranges::find(processes_, process) == processes_.end())
        {
            processes_.push_back(process);
            groups_.push_back(group != 0 ? group : process);
        }
    }

    void remove(std::uintptr_t process) noexcept
    {
        for (std::size_t i = 0; i < processes_.size(); ++i)
        {
            if (processes_[i] == process)
            {
                processes_.erase(processes_.begin() + static_cast<std::ptrdiff_t>(i));
                groups_.erase(groups_.begin() + static_cast<std::ptrdiff_t>(i));
                return;
            }
        }
    }

    [[nodiscard]] bool contains(std::uintptr_t process) const noexcept
    {
        return std::ranges::find(processes_, process) != processes_.end();
    }

    [[nodiscard]] bool contains_group(std::uintptr_t group) const noexcept
    {
        return std::ranges::find(groups_, group) != groups_.end();
    }

    [[nodiscard]] std::size_t size() const noexcept
    {
        return processes_.size();
    }

    [[nodiscard]] std::span<const std::uintptr_t> processes() const noexcept
    {
        return processes_;
    }

    [[nodiscard]] std::size_t copy_for_group(std::uintptr_t group,
                                             std::span<std::uintptr_t> output) const noexcept
    {
        std::size_t written = 0;
        for (std::size_t i = 0; i < processes_.size() && written < output.size(); ++i)
        {
            if (group == 0 || groups_[i] == group)
            {
                output[written++] = processes_[i];
            }
        }
        return written;
    }

    void clear() noexcept
    {
        processes_.clear();
        groups_.clear();
    }

  private:
    std::vector<std::uintptr_t> processes_;
    std::vector<std::uintptr_t> groups_;
};

} // namespace corehost::coreio
