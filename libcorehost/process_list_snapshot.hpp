#pragma once
#include <windows.h>
#include <algorithm>
#include <array>
#include <cstring>
#include <span>

namespace conpty
{

class process_list_snapshot
{
  public:
    static constexpr size_t max_processes = 64;

    void assign(std::span<const DWORD> processes) noexcept
    {
        _count = std::min(processes.size(), _processes.size());
        if (_count > 0)
            std::memcpy(_processes.data(), processes.data(), _count * sizeof(DWORD));
    }

    size_t count() const noexcept
    {
        return _count;
    }

    size_t copy_newest_first(DWORD *output, size_t capacity) const noexcept
    {
        const auto copy_count = std::min(_count, capacity);
        for (size_t i = 0; i < copy_count; ++i)
            output[i] = _processes[_count - 1 - i];
        return copy_count;
    }

  private:
    std::array<DWORD, max_processes> _processes{};
    size_t _count = 0;
};

} // namespace conpty
