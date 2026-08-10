// ── defterm/signal.hpp ────────────────────────────────────
// Ctrl+C / Break / Close 信号管道消费者（无线程，overlapped I/O）。
//
// COM 移交后, 按键事件 (Ctrl+C / Ctrl+Break / 关闭按钮) 实际发生在
// 终端窗口中——但控制台子系统 (CSRSS) 需要被通知才能生成控制台事件
// (CTRL_C_EVENT 等) 并发送给客户端进程。
//
// 信号通道:
//   键盘 → WT 窗口 → 写入信号管道 → conhost 消费（ConsoleControl）→
//   user32!ConsoleControl → CSRSS → 客户端进程
//
// 管道断开或协议损坏时: handle_event 返回 false，terminal_handoff 主等待
//   循环退出，整个握手链路干净收尾。
//
// 协议: 1 字节 CONSOLECONTROL + 对应数据结构体
//
// I/O 机械部分（缓冲管理、overlapped 读生命周期、断开检测、帧原子性检查）
// 由基类 win32::overlapped_pipe_reader 提供；本类只实现 CONSOLECONTROL
// 协议解析（无状态：每帧 = 1 字节 code + dwSize 声明的完整 payload）。
// 完成事件由 terminal_handoff 主等待循环与终端进程句柄一起交给
// WaitForMultipleObjects。

#pragma once
#include <windows.h>
#include <cstddef>
#include "win32/overlapped_reader.hpp"

namespace corehost::defterm
{

// ── 信号消费者 ────────────────────────────────────────────
// 在主等待循环线程内用 overlapped I/O 读取信号管道，
// 通过 user32!ConsoleControl 转发到 CSRSS。
// 公共 API（start_read/event/handle_event/try_handle_event）继承自
// win32::overlapped_pipe_reader。
class signal_consumer : public win32::overlapped_pipe_reader
{
  public:
    // 绑定信号管道读端（独占所有权）。
    explicit signal_consumer(win32::handle pipe) : win32::overlapped_pipe_reader(std::move(pipe))
    {
    }

  private:
    // 解析一条完整信号帧（1 字节 code + dwSize 声明的 payload）；数据不足
    // 返回 false。
    [[nodiscard]] bool try_parse_message() noexcept override;

    // 查询 code 对应结构体字节数；未知 code 返回 0（不消费 payload）。
    [[nodiscard]] static size_t payload_size(unsigned code) noexcept;

    // 处理一条完整信号。payload 指向帧内 payload 起始。
    void process_signal(unsigned code, const std::byte *payload) noexcept;
};

} // namespace corehost::defterm
