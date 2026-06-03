#pragma once
#include <windows.h>
#include <algorithm>
#include <span>
#include "win32/handle.hpp"
#include "miniio/io_thread.hpp"
#include "perf_diag.hpp"
#include "utility/log.hpp"

namespace conpty
{

enum class vt_pipe_read_status
{
    // 已从 vt_in 读取至少一个字节。
    bytes,
    // vt_in 当前没有可读字节；调用方可继续处理其他 ConDrv 消息。
    empty,
    // vt_in 关闭或读取失败；bridge 会把挂起读完成为 EOF。
    eof,
};

class pipe_bridge_io
{
  public:
    // 绑定 ConDrv server；后续 complete/read_input 都通过这个非拥有句柄执行。
    void set_server(win32::handle_view server) noexcept
    {
        // server 是 ConDrv \Server，用于 READ_INPUT 和 COMPLETE_IO。
        _server = server;
    }

    // 绑定终端输入 pipe；Peek/ReadFile 都集中在本类，避免 bridge 直接碰 Win32 I/O。
    void set_vt_input(win32::handle_view pipe) noexcept
    {
        // pipe 是终端到 corehost 的 VT 输入流，只在本类中执行 Peek/ReadFile。
        _vt_input = pipe;
    }

    // 绑定可选 shutdown event；有效时 pending 输入等待会按时间片检查它。
    void set_shutdown_event(win32::handle_view event) noexcept
    {
        // event 由 PtySignal 线程或 deftermv2 轮询路径提供，用来打断 pending
        // input 等待；本类不拥有该事件。
        _shutdown_event = event;
    }

    // 向 ConDrv 提交一个异步完成结果；调用方负责保证 completion 内容已构造好。
    void complete(CD_IO_COMPLETE &completion) const
    {
        // completion 是 bridge 构造好的 ConDrv 完成结果；这里仅把它提交给 server。
        miniio::complete_io(_server, completion);
    }

    // true 表示当前绑定了真实 ConDrv server，可以提交 COMPLETE_IO。
    [[nodiscard]] bool can_complete() const noexcept
    {
        return _server.valid();
    }

    // 从 ConDrv 读取 msg.body 容量之外的输入载荷到调用方提供的缓冲。
    void read_input(LUID identifier, ULONG offset, std::span<char8_t> destination) const
    {
        // identifier/offset 来自原始 io_msg descriptor，用于读取 body 之外的
        // 大输入载荷；destination 是 bridge 的持久输入 payload 缓冲。
        miniio::read_input(_server, identifier, offset, byte_span(destination));
    }

    // true 表示 pending 输入等待可以被 shutdown event 打断。
    [[nodiscard]] bool has_shutdown_event() const noexcept
    {
        return _shutdown_event.valid();
    }

    // 非阻塞检查 shutdown event；用于轮询路径决定是否退出等待。
    [[nodiscard]] bool shutdown_signaled() const noexcept
    {
        return _shutdown_event.valid() && ::WaitForSingleObject(_shutdown_event.get(), 0) == WAIT_OBJECT_0;
    }

    // 等待一个短时间片；返回 true 表示 shutdown event 已触发。
    [[nodiscard]] bool wait_shutdown_slice(DWORD timeout_ms) const noexcept
    {
        return _shutdown_event.valid() && ::WaitForSingleObject(_shutdown_event.get(), timeout_ms) == WAIT_OBJECT_0;
    }

    // 查询 vt_in 当前可读字节数。返回 false 表示 pipe 已不可用，bridge 会把
    // 输入状态推进到 EOF。
    [[nodiscard]] bool peek_available(DWORD &available) const noexcept
    {
        // available 返回 vt_in 当前可同步读取的字节数；失败时调用方按 EOF 处理。
        available = 0;
        COREHOST_PERF_SCOPE(vt_input_peek);
        if (::PeekNamedPipe(_vt_input.get(), nullptr, 0, nullptr, &available, nullptr))
            return true;

        LOG("[bridge_io] PeekNamedPipe failed err=%lu", ::GetLastError());
        return false;
    }

    // 非阻塞读取当前可见的 vt_in 字节。destination 是 bridge 的剩余输入缓冲；
    // 返回 empty 不改变 pending 状态，返回 eof 会触发 EOF completion。
    [[nodiscard]] vt_pipe_read_status read_available(std::span<char8_t> destination, DWORD &read_bytes) noexcept
    {
        // destination 是 pipe_bridge::_readbuf 的剩余空间；本函数最多读取当前
        // PeekNamedPipe 可见字节，避免阻塞 ConDrv I/O loop。
        read_bytes = 0;

        DWORD available = 0;
        if (!peek_available(available))
            return vt_pipe_read_status::eof;
        if (available == 0)
            return vt_pipe_read_status::empty;

        const auto to_read = std::min<DWORD>(available, static_cast<DWORD>(destination.size()));
        return read_from_vt_input(destination.first(to_read), read_bytes, "read_available");
    }

    // 阻塞读取 vt_in，直到有字节或 EOF。只有没有 shutdown event 的等待路径
    // 可以使用它，否则关闭信号无法打断 ReadFile。
    [[nodiscard]] vt_pipe_read_status read_blocking(std::span<char8_t> destination, DWORD &read_bytes) noexcept
    {
        // 只在没有 shutdown_event 的路径使用；调用方接受 ReadFile 阻塞到有输入或 EOF。
        read_bytes = 0;
        return read_from_vt_input(destination, read_bytes, "read_blocking");
    }

    // 尝试只消费一个特定字节。用于 CR 位于批次末尾时探测后续 LF；失败时
    // 必须保持 vt_in 不变，避免吞掉下一条输入。
    [[nodiscard]] bool try_consume_byte(char8_t expected, char8_t &consumed) noexcept
    {
        // 用于消费输入流中必须精确匹配的单字节后缀，例如 CR 后紧跟的 LF。
        // 只有下一个字节正好等于 expected 才读取；否则不改变 vt_in。
        consumed = {};

        BYTE next = 0;
        DWORD peeked = 0;
        COREHOST_PERF_SCOPE(vt_input_peek);
        if (!::PeekNamedPipe(_vt_input.get(), &next, sizeof(next), &peeked, nullptr, nullptr) || peeked == 0 ||
            next != static_cast<BYTE>(expected))
        {
            return false;
        }

        DWORD read = 0;
        {
            COREHOST_PERF_SCOPE_AMOUNT(vt_input_read_file, sizeof(next));
            if (!::ReadFile(_vt_input.get(), &next, sizeof(next), &read, nullptr) || read != sizeof(next))
                return false;
        }

        consumed = static_cast<char8_t>(next);
        return true;
    }

  private:
    // 把 char8_t 缓冲转换成 Win32 ReadFile/miniio 需要的 BYTE span。
    [[nodiscard]] static std::span<BYTE> byte_span(std::span<char8_t> buffer) noexcept
    {
        return {reinterpret_cast<BYTE *>(buffer.data()), buffer.size()};
    }

    // 执行实际 ReadFile 并统一转换为 vt_pipe_read_status；operation 仅用于日志。
    [[nodiscard]] vt_pipe_read_status read_from_vt_input(std::span<char8_t> destination, DWORD &read_bytes,
                                                         const char *operation) noexcept
    {
        // operation 只用于日志区分调用来源；destination 为空说明上层请求容量已满。
        if (destination.empty())
            return vt_pipe_read_status::empty;

        {
            COREHOST_PERF_SCOPE_AMOUNT(vt_input_read_file, destination.size());
            if (!::ReadFile(_vt_input.get(), byte_span(destination).data(), static_cast<DWORD>(destination.size()),
                            &read_bytes, nullptr) ||
                read_bytes == 0)
            {
                LOG("[bridge_io] %s failed read=%lu err=%lu", operation, read_bytes, ::GetLastError());
                return vt_pipe_read_status::eof;
            }
        }

        return vt_pipe_read_status::bytes;
    }

    // ConDrv server 非拥有句柄，提交 completion 和读取扩展输入载荷时使用。
    win32::handle_view _server;
    // 终端输入 pipe 非拥有句柄，所有 vt_in Peek/ReadFile 都集中在这里。
    win32::handle_view _vt_input;
    // 可选关闭/轮询事件。无效句柄表示等待路径不能被 signal 唤醒。
    win32::handle_view _shutdown_event;
};

} // namespace conpty
