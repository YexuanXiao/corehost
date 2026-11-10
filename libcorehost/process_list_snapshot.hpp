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

    // 用 CONNECT/DISCONNECT 后的进程列表替换快照；调用方保证两个 span 同序，
    // 且按连接时间从旧到新排列。groups[index] 为 0 时回退成 pid 自身。
    void assign(std::span<const DWORD> processes, std::span<const DWORD> groups) noexcept
    {
        _count = std::min(processes.size(), _processes.size());
        if (_count == 0)
            return;

        std::memcpy(_processes.data(), processes.data(), _count * sizeof(DWORD));
        for (size_t index = 0; index < _count; ++index)
            _groups[index] = index < groups.size() && groups[index] != 0 ? groups[index] : processes[index];
    }

    // 返回快照中有效 pid 数量。
    size_t count() const noexcept
    {
        return _count;
    }

    // 遍历快照中的 pid（Ctrl+C / Ctrl+Break 投递用）。
    [[nodiscard]] std::span<const DWORD> view() const noexcept
    {
        return {_processes.data(), _count};
    }

    // 与 view() 同序的进程组 id。
    [[nodiscard]] std::span<const DWORD> group_view() const noexcept
    {
        return {_groups.data(), _count};
    }

    // 进程组过滤：group == 0 表示"所有进程"（conhost 的 ProcessGroupId=0
    // 语义），否则只保留组 id 匹配的 pid。返回写入 output 的数量。
    size_t copy_for_group(DWORD group, DWORD *output, size_t capacity) const noexcept
    {
        size_t written = 0;
        for (size_t index = 0; index < _count && written < capacity; ++index)
        {
            if (group == 0 || _groups[index] == group)
                output[written++] = _processes[index];
        }
        return written;
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
    // 与 _processes 同序的进程组 id。
    std::array<DWORD, max_processes> _groups{};
    // _processes 的有效前缀长度。
    size_t _count = 0;
};

} // namespace corehost::conpty
