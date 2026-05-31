// ── conpty/input_buffer.hpp ────────────────────────
// Layer 4: INPUT_RECORD 队列。
//
// 功能分解：
// 1. write/peek/read/flush 维护 ConDrv 可见的输入事件队列。
// 2. input_available_event 在队列非空时置位，读空或 flush 后复位。
// 3. 队列最多保留 max_events 条记录；写满时返回实际写入数量。
#pragma once
#include <windows.h>
#include <algorithm>
#include <deque>

namespace conpty
{

struct input_buffer
{
    // 队列容量上限。写入超过该值时会截断并返回实际写入数量。
    static constexpr size_t max_events = 4096;

    // 手动复位事件。nullptr 表示尚未初始化；非空时队列从空变为非空会
    // SetEvent，读空或 flush 后 ResetEvent。
    HANDLE input_available_event = nullptr;

    void init_event()
    {
        // 手动复位事件表示“队列非空”这个状态，而不是单次输入到达。读空后
        // read/flush 负责复位。
        if (!input_available_event)
            input_available_event = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
    }

    ~input_buffer()
    {
        if (input_available_event)
            ::CloseHandle(input_available_event);
    }

    size_t write(const INPUT_RECORD *events, size_t count)
    {
        // 返回值可能小于 count，表示队列已满。
        const auto n = std::min(count, max_events - _events.size());
        for (size_t i = 0; i < n; ++i)
            _events.push_back(events[i]);
        if (n > 0 && input_available_event)
            // 只要写入了至少一条记录，等待 GetConsoleInput 的线程即可被唤醒。
            ::SetEvent(input_available_event);
        return n;
    }

    size_t prepend(const INPUT_RECORD *events, size_t count)
    {
        const auto n = std::min(count, max_events - _events.size());
        for (size_t i = 0; i < n; ++i)
            _events.push_front(events[n - 1 - i]);
        if (n > 0 && input_available_event)
            ::SetEvent(input_available_event);
        return n;
    }

    size_t read(INPUT_RECORD *out, size_t max_count)
    {
        // 返回值为实际读出的记录数；0 表示当前没有输入事件。
        const auto n = std::min(max_count, _events.size());
        for (size_t i = 0; i < n; ++i)
        {
            out[i] = _events.front();
            _events.pop_front();
        }
        if (_events.empty() && input_available_event)
            // 事件必须反映读后的队列状态；否则调用方会在空队列上持续被唤醒。
            ::ResetEvent(input_available_event);
        return n;
    }

    size_t available() const noexcept
    {
        return _events.size();
    }

    size_t peek(INPUT_RECORD *out, size_t max_count) const noexcept
    {
        // peek 不移除队列元素；返回值与 read 一样是实际复制数量。
        const auto n = std::min(max_count, _events.size());
        for (size_t i = 0; i < n; ++i)
            out[i] = _events[i];
        return n;
    }

    void flush()
    {
        // FlushConsoleInputBuffer 丢弃所有尚未消费事件，并把“可读”状态清零。
        _events.clear();
        if (input_available_event)
            ::ResetEvent(input_available_event);
    }

  private:
    // deque 保证 pop_front 为 O(1)，适合控制台输入队列逐条消费。
    std::deque<INPUT_RECORD> _events;
};

} // namespace conpty
