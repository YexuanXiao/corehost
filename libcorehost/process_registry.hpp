#pragma once
#include <windows.h>
#include <algorithm>
#include <cstddef>
#include <span>
#include <vector>

namespace corehost::conpty
{

// 当前控制台上已连接进程的唯一条目：pid + 进程组 id。
// group 来自 CONNECT 的 CONSOLE_SERVER_MSG::ProcessGroupId；0 归一化为 pid，
// 与 conhost 的"按进程 id 定向普通子进程"的常见用法保持兼容（详见
// doc/design/0006-process-registry-review.md）。
struct process_entry
{
    DWORD pid = 0;
    DWORD group = 0;
};

// 控制台进程列表的单一真相：CONNECT/DISCONNECT 在会话锁内修改，Ctrl 事件
// 投递与 GetConsoleProcessList 读取它。所有访问都由会话锁串行化。
class process_registry
{
  public:
    // 加入 pid；重复 pid 不改变列表。group 为 0 时回退为 pid。
    void add(DWORD pid, DWORD group)
    {
        if (find(pid) != _entries.end())
            return;
        _entries.push_back(process_entry{pid, group != 0 ? group : pid});
    }

    // 移除 pid；不存在时不变。
    void remove(DWORD pid) noexcept
    {
        const auto it = find(pid);
        if (it != _entries.end())
            _entries.erase(it);
    }


    [[nodiscard]] size_t count() const noexcept
    {
        return _entries.size();
    }


    // 用两组同序 span 替换列表（测试注入/会话初始化）。groups 缺省时按 pid
    // 归一化；保持旧实现的行为。
    void assign(std::span<const DWORD> processes, std::span<const DWORD> groups)
    {
        _entries.clear();
        _entries.reserve(processes.size());
        for (size_t index = 0; index < processes.size(); ++index)
        {
            const auto group = index < groups.size() && groups[index] != 0 ? groups[index] : processes[index];
            _entries.push_back(process_entry{processes[index], group});
        }
    }

    // 进程组过滤：group == 0 表示"所有进程"（conhost 的 ProcessGroupId=0
    // 语义），否则只保留组 id 匹配的 pid。返回写入 output 的数量。
    // 是否存在指定进程组（GenerateConsoleCtrlEvent 的未知 group 校验）。
    [[nodiscard]] bool contains_group(DWORD group) const noexcept
    {
        return std::any_of(_entries.begin(), _entries.end(), [group](const process_entry &entry) {
            return entry.group == group;
        });
    }

    size_t copy_for_group(DWORD group, std::span<DWORD> output) const noexcept
    {
        size_t written = 0;
        for (const auto &entry : _entries)
        {
            if (written == output.size())
                break;
            if (group == 0 || entry.group == group)
                output[written++] = entry.pid;
        }
        return written;
    }

    // 按 GetConsoleProcessList 期望的新到旧顺序复制 pid。
    size_t copy_newest_first(std::span<DWORD> output) const noexcept
    {
        const auto copy_count = std::min(_entries.size(), output.size());
        for (size_t index = 0; index < copy_count; ++index)
            output[index] = _entries[_entries.size() - 1 - index].pid;
        return copy_count;
    }

  private:
    [[nodiscard]] std::vector<process_entry>::iterator find(DWORD pid) noexcept
    {
        return std::find_if(_entries.begin(), _entries.end(), [pid](const process_entry &entry) {
            return entry.pid == pid;
        });
    }

    // 连接顺序从旧到新；GetConsoleProcessList 反向输出。
    std::vector<process_entry> _entries;
};

} // namespace corehost::conpty