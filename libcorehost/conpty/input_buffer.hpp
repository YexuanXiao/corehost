// ── conpty/input_buffer.hpp ────────────────────────
// Layer 4: 输入缓冲区 (对标 InputBuffer)
//
// 与 conpty/input_buffer.hpp 相同 — INPUT_RECORD 是 Windows 固定结构体，
// char32_t ↔ WCHAR 转换在填充 INPUT_RECORD 时由 vt_input_engine 负责。
#pragma once
#include <windows.h>
#include <deque>

namespace conpty
{

struct input_buffer
{
    static constexpr size_t max_events = 4096;

    HANDLE input_available_event = nullptr;

    void init_event()
    {
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
        size_t n = 0;
        while (n < count && _events.size() < max_events)
        {
            _events.push_back(events[n]);
            ++n;
        }
        if (n > 0 && input_available_event)
            ::SetEvent(input_available_event);
        return n;
    }

    size_t read(INPUT_RECORD *out, size_t max_count)
    {
        size_t n = 0;
        while (n < max_count && !_events.empty())
        {
            out[n] = _events.front();
            _events.pop_front(); // O(1) with std::deque (vs O(n) vector erase)
            ++n;
        }
        if (_events.empty() && input_available_event)
            ::ResetEvent(input_available_event);
        return n;
    }

    size_t available() const noexcept
    {
        return _events.size();
    }

    size_t peek(INPUT_RECORD *out, size_t max_count) const noexcept
    {
        size_t n = 0;
        for (size_t i = 0; i < _events.size() && n < max_count; ++i)
        {
            out[n] = _events[i]; // std::deque supports O(1) random access
            ++n;
        }
        return n;
    }

    void flush()
    {
        _events.clear();
        if (input_available_event)
            ::ResetEvent(input_available_event);
    }

    // debug: dump all events
    void dump_all() const
    {
        char buf[512];
        size_t p = 0;
        for (size_t i = 0; i < _events.size() && p < sizeof(buf) - 10; ++i)
        {
            auto &e = _events[i];
            if (e.EventType == KEY_EVENT)
                p += (size_t)snprintf(buf + p, sizeof(buf) - p, "%c%02X/%04X ", e.Event.KeyEvent.bKeyDown ? 'D' : 'U',
                                      e.Event.KeyEvent.wVirtualKeyCode, e.Event.KeyEvent.uChar.UnicodeChar);
        }
        if (p > 0)
            buf[p - 1] = '\0';
        OutputDebugStringA(buf);
    }

  private:
    std::deque<INPUT_RECORD> _events; // O(1) pop_front vs vector's O(n) erase
};

} // namespace conpty
