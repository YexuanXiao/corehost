#pragma once

// ── Win32 overlapped 管道流式读者 ──────────────────────────
//
// 为匿名管道 overlapped 读提供通用的"缓冲 + 重读 + 断开检测"骨架。
// 派生类只实现协议解析（try_parse_message），I/O 机械部分全部集中在此。
//
// 使用场景：corehost 的 PtySignal / CONSOLECONTROL 信号管道消费者。
// 完成事件（event()）由调用方与主循环的其他句柄一起交给
// WaitForMultipleObjects；事件就绪后调用 handle_event() 消费数据。
//
// 错误策略（与 win32/overlapped.hpp 一致）：
// - 管道断开（0 字节 EOF / broken pipe / 未连接）→ handle_event 返回 false；
// - 协议损坏（缓冲满无法解析）→ 同样按断开处理；
// - 其余不可恢复错误由 begin/finish_overlapped_read 抛 win32::error。

#include <windows.h>
#include <cstddef>
#include <cstring>
#include <utility>

#include "win32/error.hpp"
#include "win32/event.hpp"
#include "win32/handle.hpp"
#include "win32/overlapped.hpp"
#include "win32/wait.hpp"

namespace win32
{

class overlapped_pipe_reader
{
  public:
    // 无管道状态；配合移动赋值用于"有管道时才装配"（见 move 赋值说明）。
    overlapped_pipe_reader() noexcept = default;

    // 绑定管道读端（独占所有权）。
    explicit overlapped_pipe_reader(win32::handle pipe)
        : _pipe(std::move(pipe)), _read_event(win32::event{win32::create_tag, false, false})
    {
        _ov.hEvent = _read_event.get();
    }

    virtual ~overlapped_pipe_reader() noexcept = default;

    // Move-only。移动赋值前提：两侧都没有未完成的 overlapped 读；解析状态
    // 由派生类默认构造值重新开始（conpty 装配模式：默认对象 ← 新构造对象）。
    overlapped_pipe_reader(const overlapped_pipe_reader &) = delete;
    overlapped_pipe_reader &operator=(const overlapped_pipe_reader &) = delete;

    overlapped_pipe_reader(overlapped_pipe_reader &&) noexcept = default;

    overlapped_pipe_reader &operator=(overlapped_pipe_reader &&other) noexcept
    {
        if (this != &other)
        {
            _pipe = std::move(other._pipe);
            _read_event = std::move(other._read_event);
            // _ov 整体清零后重新绑定本对象的事件句柄（句柄值虽不变，但显式
            // 重绑避免依赖移动后的事件所有权细节）。
            _ov = {};
            _ov.hEvent = _read_event.get();
            _available = 0;
            _consumed = 0;
            _disconnected = other._disconnected;
        }
        return *this;
    }

    // 发起首个 overlapped read。之后每次 handle_event/try_handle_event 消费
    // 完成事件后会自动重新发起读取。
    void start_read()
    {
        read_next();
    }

    // overlapped 完成事件；与主循环其他句柄一起交给 WaitForMultipleObjects。
    [[nodiscard]] win32::handle_view event() const noexcept
    {
        return _read_event.view();
    }

    // true 表示绑定了有效管道。
    [[nodiscard]] bool valid() const noexcept
    {
        return _pipe.valid();
    }

    // 调用方确认 event 已置位后调用：取回结果、解析消息、重新发起读。
    // 返回 false 表示管道断开（协议损坏也按断开处理）。
    bool handle_event()
    {
        if (_disconnected)
            return false;

        const auto result = win32::finish_overlapped_read(_pipe.view(), _ov);
        if (result.done())
        {
            _available += result.bytes;
            drain_parsed();
            if (!_disconnected)
                read_next();
            return !_disconnected;
        }

        // closed：管道断开。
        _disconnected = true;
        return false;
    }

    // 非阻塞版本：event 未置位直接返回 true；置位则等同 handle_event。
    bool try_handle_event()
    {
        if (_disconnected)
            return false;

        const auto wait = win32::wait_one(_read_event, 0);
        if (wait.abandoned())
        {
            _disconnected = true;
            return false;
        }
        if (!wait.signaled())
            return true;
        return handle_event();
    }

  protected:
    // ── 派生类协议解析入口 ────────────────────────────────
    // 从 data()[consumed()..available()) 尝试解析一条完整消息：成功时更新
    // consumed 并执行处理动作，返回 true（可能还有下一条）；数据不足返回
    // false（等待更多字节）。解析动作不得抛出；发现协议损坏应调用
    // mark_disconnected() 并返回 false。
    [[nodiscard]] virtual bool try_parse_message() noexcept = 0;

    [[nodiscard]] std::byte *data() noexcept
    {
        return _buffer;
    }

    [[nodiscard]] size_t available() const noexcept
    {
        return _available;
    }

    [[nodiscard]] size_t consumed() const noexcept
    {
        return _consumed;
    }

    void set_consumed(size_t n) noexcept
    {
        _consumed = n;
    }

    // 标记管道不可用（协议损坏）；handle_event 随后返回 false。
    void mark_disconnected() noexcept
    {
        _disconnected = true;
    }

  private:
    // 循环解析直到需要更多数据或消费完毕。
    void drain_parsed() noexcept
    {
        while (_consumed < _available && try_parse_message()) {}
    }

    // 发起下一轮读；立即完成时继续解析并重读，断开/协议损坏时停止。
    void read_next()
    {
        for (;;)
        {
            // 未消费字节移到缓冲头部，给新读腾出空间。
            const size_t remaining = _available - _consumed;
            if (remaining != 0 && _consumed != 0)
                std::memmove(_buffer, _buffer + _consumed, remaining);
            _available = remaining;
            _consumed = 0;

            if (_available == sizeof(_buffer))
            {
                // 缓冲已满但仍无法解析出完整消息：协议损坏，按断开处理。
                _disconnected = true;
                return;
            }

            const auto result = win32::begin_overlapped_read(_pipe.view(), _buffer + _available,
                                                             static_cast<DWORD>(sizeof(_buffer) - _available), _ov);
            if (result.pending())
                return;

            if (result.done())
            {
                _available += result.bytes;
                drain_parsed();
                if (_disconnected)
                    return;
                continue; // 立即完成说明管道里可能还有数据，继续读
            }

            // closed：管道断开。
            _disconnected = true;
            return;
        }
    }

    win32::handle _pipe;
    win32::event _read_event; // overlapped 完成事件（自动复位）
    OVERLAPPED _ov{};
    std::byte _buffer[64];
    size_t _available{}; // _buffer 中有效字节数
    size_t _consumed{};  // 已消费字节偏移
    bool _disconnected{};
};

} // namespace win32
