#pragma once
#include <windows.h>
#include "miniio/io_thread.hpp"

namespace corehost::conpty
{

enum class PendingKind
{
    // 没有挂起 ConDrv 请求；I/O loop 可以继续 READ_IO。
    None,
    // CONSOLE_IO_RAW_READ 等待 vt_in 字节；完成体直接写原始字节。
    RawRead,
    // USER_DEFINED ReadConsole 等待一行 cooked input；完成体写
    // CONSOLE_READCONSOLE_MSG 和编码后的文本。
    ConsoleRead,
    // USER_DEFINED GetConsoleInput 等待 INPUT_RECORD；完成体写事件数组。
    ConsoleInput
};

class pending_io_state
{
  public:
    // 返回当前挂起请求种类。调用方用它决定 VT 输入到达时应完成 raw read、
    // cooked ReadConsole，还是 INPUT_RECORD 读取。
    PendingKind kind() const noexcept
    {
        return _kind;
    }

    // true 表示当前有一个 ConDrv 请求被 bridge 持有，io_loop 不能继续用同一
    // msg 缓冲提交下一条 completion。
    bool has_pending() const noexcept
    {
        return _kind != PendingKind::None;
    }

    void begin_raw_read(miniio::io_msg &msg, bool process_control_z) noexcept
    {
        // msg 是 READ_IO 提供的原始请求缓冲。返回 false 的 handler 会让
        // io_loop 停止提交它；complete_pending 后必须用 COMPLETE_IO 显式完成。
        _kind = PendingKind::RawRead;
        _raw_read = &msg;
        _console_read = nullptr;
        _console_input = nullptr;
        // true 时 Ctrl+Z 在 raw read 中作为 EOF；false 时按普通字节返回。
        _process_control_z = process_control_z;
    }

    void begin_console_read(miniio::io_msg &msg, bool process_control_z) noexcept
    {
        // msg 指向 USER_DEFINED ReadConsole 请求；completion 编码由 msg.body 中
        // CONSOLE_READCONSOLE_MSG::Unicode 决定，不在 pending 状态里重复保存。
        _kind = PendingKind::ConsoleRead;
        _raw_read = nullptr;
        _console_read = &msg;
        _console_input = nullptr;
        // 只在 ENABLE_PROCESSED_INPUT 下为 true；raw/cooked 行编辑据此决定
        // Ctrl+Z 是否提前完成读取。
        _process_control_z = process_control_z;
    }

    void begin_console_input(miniio::io_msg &msg) noexcept
    {
        // GetConsoleInput 等待 INPUT_RECORD；普通文本输入会先转换为 KEY_EVENT
        // 写入 input_buffer，再由 complete_pending 拷贝到 msg。
        _kind = PendingKind::ConsoleInput;
        _raw_read = nullptr;
        _console_read = nullptr;
        _console_input = &msg;
    }

    // 返回当前 RAW_READ 请求消息；仅当 kind()==RawRead 时非空。pending_io_state
    // 不拥有该消息，生命周期由 io_loop 的请求缓冲保证。
    miniio::io_msg *raw_read() const noexcept
    {
        return _raw_read;
    }

    // 返回当前 ReadConsole 请求消息；仅当 kind()==ConsoleRead 时非空。
    miniio::io_msg *console_read() const noexcept
    {
        return _console_read;
    }

    // 返回当前 GetConsoleInput 请求消息；仅当 kind()==ConsoleInput 时非空。
    miniio::io_msg *console_input() const noexcept
    {
        return _console_input;
    }

    // 当前挂起读取是否把 Ctrl+Z 当作 EOF。该值随 begin_raw_read/
    // begin_console_read 重新设置，ConsoleInput 不使用它。
    bool process_control_z() const noexcept
    {
        return _process_control_z;
    }

    // true 表示 VT 输入源已经关闭；pending 读取和后续读取都应直接完成 EOF。
    bool vt_eof() const noexcept
    {
        return _vt_eof;
    }

    void set_vt_eof(bool eof) noexcept
    {
        // vt_eof 是会话级输入关闭标志，不随 clear() 清除；后续新的读取也应
        // 立即得到 EOF，而不是再次等待已关闭的 vt_in。
        _vt_eof = eof;
    }

    void clear() noexcept
    {
        // 只清除当前挂起请求；vt_eof 保留为会话级 EOF 标志，process_control_z
        // 由下一次 begin_* 按当前 API/input mode 重新设置。
        _kind = PendingKind::None;
        _raw_read = nullptr;
        _console_read = nullptr;
        _console_input = nullptr;
        _process_control_z = false;
    }

  private:
    // 当前等待完成的请求类别。None 时下面三个 msg 指针都必须为空。
    PendingKind _kind = PendingKind::None;
    // RawRead/ConsoleRead/ConsoleInput 三者互斥；非空指针指向仍由 io_loop
    // 双缓冲持有的请求消息，complete_pending 会在完成前复制 completion 数据。
    miniio::io_msg *_raw_read = nullptr;
    miniio::io_msg *_console_read = nullptr;
    miniio::io_msg *_console_input = nullptr;
    // vt_in 已关闭或 signal 线程确认终端关闭；用于让后续读请求直接 EOF。
    bool _vt_eof = false;
    // 当前读取是否把 Ctrl+Z 当作 EOF；每次 begin_* 根据 API/input mode 重设。
    bool _process_control_z = false;
};

} // namespace corehost::conpty
