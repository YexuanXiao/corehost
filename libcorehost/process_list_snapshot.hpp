#pragma once
#include <windows.h>
#include <algorithm>
#include <array>
#include <cstring>
#include <span>

namespace corehost::conpty
{

class process_list_snapshot
{
  public:
    // 与 io_state 的进程列表容量保持一致；超过容量的 pid 不进入快照。
    static constexpr size_t max_processes = 64;

    // 用 CONNECT/DISCONNECT 后的进程列表替换快照；调用方保证 span 中 pid
    // 按连接时间从旧到新排列。
    void assign(std::span<const DWORD> processes) noexcept
    {
        _count = std::min(processes.size(), _processes.size());
        if (_count > 0)
            std::memcpy(_processes.data(), processes.data(), _count * sizeof(DWORD));
    }

    // 返回快照中有效 pid 数量。
    size_t count() const noexcept
    {
        return _count;
    }

    // 按 Win32 GetConsoleProcessList 期望的新到旧顺序复制 pid。
    size_t copy_newest_first(DWORD *output, size_t capacity) const noexcept
    {
        const auto copy_count = std::min(_count, capacity);
        const auto first = _processes.begin() + static_cast<std::ptrdiff_t>(_count - copy_count);
        const auto last = _processes.begin() + static_cast<std::ptrdiff_t>(_count);
        std::reverse_copy(first, last, output);
        return copy_count;
    }

  private:
    // 当前控制台连接过且尚未 DISCONNECT 的 pid。
    std::array<DWORD, max_processes> _processes{};
    // _processes 的有效前缀长度。
    size_t _count = 0;
};

} // namespace corehost::conpty
