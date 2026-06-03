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
#include <span>
#include "deque.hpp"

namespace conpty
{

struct input_buffer
{
    // 队列容量上限。写入超过该值时会截断并返回实际写入数量。
    static constexpr size_t max_events = 4096;

    // 手动复位事件。普通 headless/COM 路径应绑定 ConDrv 提供的
    // InputAvailableEvent；为空时 init_event 才创建本地兜底事件。
    HANDLE input_available_event = nullptr;
    bool owns_input_available_event = false;

    void set_event(HANDLE event) noexcept
    {
        if (owns_input_available_event && input_available_event)
            ::CloseHandle(input_available_event);
        input_available_event = event;
        owns_input_available_event = false;
        if (input_available_event)
        {
            if (_events.empty())
                ::ResetEvent(input_available_event);
            else
                ::SetEvent(input_available_event);
        }
    }

    // 懒创建“输入队列非空”事件；多次调用保持同一个事件句柄。
    void init_event()
    {
        // 手动复位事件表示“队列非空”这个状态，而不是单次输入到达。读空后
        // read/flush 负责复位。
        if (!input_available_event)
        {
            input_available_event = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
            owns_input_available_event = input_available_event != nullptr;
        }
    }

    // 释放由 init_event 创建的事件句柄。
    ~input_buffer()
    {
        if (owns_input_available_event && input_available_event)
            ::CloseHandle(input_available_event);
    }

    // 将 events 追加到队尾；用于终端输入或 WriteConsoleInput 产生新事件。
    size_t write(const INPUT_RECORD *events, size_t count)
    {
        // 返回值可能小于 count，表示队列已满。
        const auto n = std::min(count, max_events - _events.size());
        _events.append_range(std::span{events, n});
        if (n > 0 && input_available_event)
            // 只要写入了至少一条记录，等待 GetConsoleInput 的线程即可被唤醒。
            ::SetEvent(input_available_event);
        return n;
    }

    // 将 events 插入队头；用于需要让新事件优先被 Console API 读取的路径。
    size_t prepend(const INPUT_RECORD *events, size_t count)
    {
        const auto n = std::min(count, max_events - _events.size());
        _events.prepend_range(std::span{events, n});
        if (n > 0 && input_available_event)
            ::SetEvent(input_available_event);
        return n;
    }

    // 从队头取出最多 max_count 条事件，并在队列读空后复位可读事件。
    size_t read(INPUT_RECORD *out, size_t max_count)
    {
        // 返回值为实际读出的记录数；0 表示当前没有输入事件。
        const auto n = std::min(max_count, _events.size());
        std::copy_n(_events.begin(), n, out);
        _events.erase(_events.begin(), _events.begin() + static_cast<std::ptrdiff_t>(n));
        if (_events.empty() && input_available_event)
            // 事件必须反映读后的队列状态；否则调用方会在空队列上持续被唤醒。
            ::ResetEvent(input_available_event);
        return n;
    }

    // 返回当前未消费 INPUT_RECORD 数量。
    size_t available() const noexcept
    {
        return _events.size();
    }

    // 复制队头事件但不移除，供 CONSOLE_READ_NOREMOVE 使用。
    size_t peek(INPUT_RECORD *out, size_t max_count) const noexcept
    {
        // peek 不移除队列元素；返回值与 read 一样是实际复制数量。
        const auto n = std::min(max_count, _events.size());
        std::copy_n(_events.begin(), n, out);
        return n;
    }

    // 清空所有未消费事件并复位 input_available_event。
    void flush()
    {
        // FlushConsoleInputBuffer 丢弃所有尚未消费事件，并把“可读”状态清零。
        _events.clear();
        if (input_available_event)
            ::ResetEvent(input_available_event);
    }

  private:
    // deque 保证 pop_front 为 O(1)，适合控制台输入队列逐条消费。
    bizwen::deque<INPUT_RECORD> _events;
};

} // namespace conpty
