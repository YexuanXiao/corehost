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
// 帧原子性假设（关键设计前提）：
// 写端（libconpty / WT）总是用一次 WriteFile 写完一整帧（id + payload），
// 字节流管道保证一帧要么完整到达、要么还没到达。因此一次 ReadFile 返回的
// 字节必然由整数个完整帧组成：解析到剩余字节不足一帧，就是协议损坏
// （writer 拆帧或脏数据），按断开处理。为此：
// - 不需要跨读边界的半帧状态（need_payload）与 memmove；
// - 每次读取从缓冲头部开始，读完必消费干净。
//
// 错误策略（与 win32/overlapped.hpp 一致）：
// - 管道断开（0 字节 EOF / broken pipe / 未连接）→ handle_event 返回 false；
// - 协议损坏（半帧残留）→ 同样按断开处理；
// - 其余不可恢复错误由 begin/finish_overlapped_read 抛 win32::error。

#include <windows.h>
#include <cstddef>
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
        : _pipe(std::move(pipe)), _read_event(win32::event{win32::create_tag, true, false})
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
            // 帧原子性保证解析后无残留，_available 必为 0，直接赋值。
            _available = result.bytes;
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
    // 从 data()[consumed()..available()) 尝试解析一条完整帧：成功时更新
    // consumed 并执行处理动作，返回 true（可能还有下一帧）；数据不足（当前
    // 剩余字节无法构成一帧）返回 false 且不得修改 consumed。解析动作不得
    // 抛出；发现协议损坏应调用 mark_disconnected() 并返回 false。
    // 基类保证：解析结束后 consumed() != available() 即半帧残留，按协议
    // 损坏断开（帧原子性假设）。
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
    // 解析当前缓冲中的全部完整帧；残留半帧即协议损坏（帧原子性假设）。
    void drain_parsed() noexcept
    {
        while (_consumed < _available && try_parse_message()) {}
        if (_consumed != _available)
        {
            // 半帧残留：writer 拆帧或脏数据，按协议损坏断开。
            _disconnected = true;
        }
    }

    // 发起下一轮读；立即完成时继续解析并重读，断开/协议损坏时停止。
    // 帧原子性保证解析后无残留，每次读取都从缓冲头部开始。
    // 事件是手动复位：发起读前显式复位，完成时系统 SetEvent；wait 不会
    // 消费事件状态，任意多个等待/查询（io_loop wait_any、bridge
    // wait_signal_slice、try_handle_event）都能可靠看到就绪。
    void read_next()
    {
        for (;;)
        {
            _available = 0;
            _consumed = 0;
            ::ResetEvent(_read_event.get());

            const auto result = win32::begin_overlapped_read(_pipe.view(), _buffer, sizeof(_buffer), _ov);
            if (result.pending())
                return;

            if (result.done())
            {
                _available = result.bytes;
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
    win32::event _read_event; // overlapped 完成事件（手动复位；发起读前 ResetEvent）
    OVERLAPPED _ov{};
    std::byte _buffer[64]; // 读目标；64B 容纳多次合并的完整帧
    size_t _available{};   // 当前读入的有效字节数
    size_t _consumed{};    // 解析游标；解析结束必须等于 _available
    bool _disconnected{};
};

} // namespace win32
