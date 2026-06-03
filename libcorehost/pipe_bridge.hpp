// ── conpty/pipe_bridge.hpp ─────────────────────────
// Layer 2: PTY 管道桥接。
//
// 功能分解：
// 1. VT 输入：从 vt_in 读取 UTF-8 字节，解析为 VT 消息、Win32Input 键盘事件
//    或行编辑文本，并完成挂起的 ReadConsole/RawRead/GetConsoleInput。
// 2. VT 输出：把 Console API 的输出状态转换为 VT 字节写入 vt_out，并同步
//    terminal_cursor_state，避免 Console 状态和终端光标分叉。
// 3. 行编辑：_cooked_buf 保存当前 cooked input，_cooked_cursor 和输入列边界
//    控制插入、删除、左右移动和历史浏览。
// 4. 挂起 I/O：pending_io_state 决定当前等待哪类 ConDrv 请求；完成后必须通过
//    COMPLETE_IO 显式回给 ConDrv。
#pragma once
#include <windows.h>
#include <algorithm>
#include <array>
#include <cassert>
#include <charconv>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <span>
#include <string>
#include <string_view>
#include <vector>
#include "win32/handle.hpp"
#include "miniio/io_thread.hpp"
#include "os/Console/ntcon.h"
#include "os/Console/conmsgl1.h"
#include "os/Console/conmsgl2.h"
#include "vt_parser.hpp"
#include "vt_input_engine.hpp"
#include "input_buffer.hpp"
#include "console_state.hpp"
#include "screen_buffer.hpp"
#include "char_convert.hpp"
#include "vt_output_buffer.hpp"
#include "pipe_bridge_io.hpp"
#include "conversion_buffers.hpp"
#include "process_list_snapshot.hpp"
#include "terminal_cursor_state.hpp"
#include "pending_io_state.hpp"
#include "command_history_state.hpp"
#include "perf_diag.hpp"
#include "utility/log.hpp"
#include "deque.hpp"

namespace conpty
{
using namespace std::literals;

class pipe_bridge_testable;

struct pipe_bridge
{
    // 等待 pending VT 输入时的时间片。VT 输入和 signal shutdown 是两个独立
    // 条件，因此不能用无限等待。
    static constexpr DWORD pending_vt_input_wait_ms = 16;

    // ── 子系统 ──
    // inp 是 Console API 事件队列；bridge 在终端输入到达时写入 KEY_EVENT，
    // GetConsoleInput 挂起请求也从这里完成。
    input_buffer &inp;
    // cstate 是所有 Console API 共享的运行状态；bridge 在本地 echo、resize、
    // cursor sync 时更新它，保证 API 查询和终端输出一致。
    console_state &cstate;
    // sbuf 是主 screen buffer 的引用；_active_screen 可能临时指向备用缓冲区。
    screen_buffer &sbuf;
    // 当前 VT 输出/Console API 应操作的 screen buffer。它不拥有对象，只在
    // api_router 切换 alternate buffer 时改指向。
    screen_buffer *_active_screen = nullptr;

    pipe_bridge(input_buffer &input, console_state &state, screen_buffer &screen) noexcept
        : inp(input), cstate(state), sbuf(screen)
    {
        _active_screen = &screen;
        _history.set_capacity(state.history_buffer_size);
    }

    // 切换当前 API/VT 同步目标。screen 必须由 api_router 持有并覆盖 bridge
    // 生命周期；bridge 只保存指针，不复制屏幕状态。
    void set_active_screen_buffer(screen_buffer &screen) noexcept
    {
        _active_screen = &screen;
    }

    // 返回当前活动缓冲区引用。_active_screen 在构造后始终非空。
    screen_buffer &active_screen_buffer() const noexcept
    {
        return *_active_screen;
    }

    // 绑定 VT 输出管道。后续 vt_append*/vt_flush 都写入该 handle；handle 的
    // 所有权由 conpty/defterm 会话层管理。
    void set_vt_output(win32::handle_view output) noexcept
    {
        _vt_output.set_output(output);
    }

    // 绑定 ConDrv server handle，pending completion 通过 pipe_bridge_io 使用它
    // 调用 COMPLETE_IO。
    void set_server(win32::handle_view server) noexcept
    {
        _io.set_server(server);
    }

    // 绑定终端输入管道。bridge 从这里读取 UTF-8/VT 字节并推进 pending 读取。
    void set_vt_input(win32::handle_view input) noexcept
    {
        _io.set_vt_input(input);
    }

    // 更新 GetConsoleProcessList 可见的进程快照。message_router 在
    // CONNECT/DISCONNECT 后把 io_state 的真实列表同步到这里。
    void set_process_list(std::span<const DWORD> processes) noexcept
    {
        _processes.assign(processes);
    }

    // 返回当前进程快照数量；仅用于日志和 L3 GetConsoleProcessList 响应。
    size_t process_count() const noexcept
    {
        return _processes.count();
    }

    [[nodiscard]] PendingKind pending_kind() const noexcept
    {
        // 供 message_router 日志判断当前挂起请求如何消费后续 vt_in 字节。
        return _pending.kind();
    }

    [[nodiscard]] bool pending_vt_eof() const noexcept
    {
        // true 表示 vt_in 已关闭；后续读取应直接完成 EOF，而不是继续等待。
        return _pending.vt_eof();
    }

    [[nodiscard]] DWORD buffered_vt_input_bytes() const noexcept
    {
        // _readbuf 中已读取但可能还要返回给 RawRead/ReadConsole completion 的字节数。
        return _read_total;
    }

    [[nodiscard]] size_t queued_vt_input_bytes() const noexcept
    {
        // 当前请求未消费完的 vt_in 尾部字节数；下一次 pending 读取优先消费它。
        return _queued_vt_input.size();
    }

    [[nodiscard]] size_t buffered_vt_output_bytes() const noexcept
    {
        // vt_out 批量缓冲中等待 WriteFile 的字节数。
        return _vt_output.buffered_size();
    }

    // 将进程快照按最新进程在前复制到 output。capacity 是 DWORD 槽位数；
    // 返回实际写入数量。
    size_t copy_process_list_newest_first(DWORD *output, size_t capacity) const noexcept
    {
        return _processes.copy_newest_first(output, capacity);
    }

  private:
    // ── Parser 外部缓冲 ──────────────────────────────
    // _input_raw_buf 属于终端输入方向：vt_in 的 UTF-8 字节解码成 char32_t 后喂给
    // _input_parser。vt_parser 会把当前消息的原文写到这里，并让 vt_message::text /
    // title 指向其中的切片；调用方消费消息并 reset 后，该缓冲才能被清理或复用。
    raw_u32_buffer _input_raw_buf;

    // _output_raw_buf 属于 Console API 输出方向：WriteConsole/RAW_WRITE 的文本
    // 也可能包含 VT 序列。它和 _input_raw_buf 分离，避免应用输出中的半条 ESC/CSI/OSC
    // 序列污染终端输入解析状态。
    raw_u32_buffer _output_raw_buf;

    // _cooked_buf 保存当前 cooked ReadConsole 行编辑文本，只包含可以返回给
    // 控制台程序的地面态输入字符，不包含 ESC/CSI/OSC 控制序列原文。
    std::u32string _cooked_buf;
    // 编辑光标在 _cooked_buf 中的位置，范围 0.._cooked_buf.size()。
    size_t _cooked_cursor = 0;

    // ── char32_t VT 解析器 ───────────────────────────
    // _input_parser 只处理终端输入方向。它把键盘 VT 序列、Win32 Input Mode 事件、
    // CPR/resize 等终端回应以及普通文本拆成 vt_message；状态必须跨 vt_in
    // 读取块保留，因为 pipe 读取边界可能落在一条控制序列中间。
    vt_parser _input_parser{_input_raw_buf};

    // _output_parser 只处理 Console API 输出方向。它用于在透传输出文本前识别
    // CUP/SGR/ED/OSC title 等序列并同步本地终端状态；状态必须跨
    // WriteConsole/RAW_WRITE 消息保留，因为应用一次输出的 VT 序列可能被 ConDrv
    // 拆成多个 API 消息。
    vt_parser _output_parser{_output_raw_buf};

    // _engine 把 _input_parser 产出的输入方向 vt_message 转换为 INPUT_RECORD 或 cooked
    // 行编辑动作；输出方向不经过它。
    vt_input_engine _engine;

    // ── 流式 UTF-8 解码器 ──
    // vt_in 是 UTF-8 字节流。_utf8_decoder 保留跨 ReadFile 边界的半个多字节
    // 序列；只有得到完整 codepoint 后才交给 _input_parser。
    utf8_stream_decoder _utf8_decoder;

    // ── 原始字节缓冲 ──
    // _readbuf 保存本轮 pending 读取已经从 vt_in 取得的原始字节；RawRead
    // completion 直接从这里返回，ReadConsole 则同时把它解码进 _cooked_buf。
    std::array<char8_t, sizeof(miniio::io_msg::body)> _readbuf{};
    // 保存 USER_DEFINED 消息中超过 miniio::io_msg::body 的输入尾部。
    raw_u8_buffer _input_payload_buffer;
    // GetConsoleInput 的输出可能远大于 miniio::io_msg::body。同步完成时
    // completion 会在下一轮 READ_IO 提交，因此该缓冲必须作为 bridge 成员
    // 保持稳定，直到 ConDrv 消费 Write.Data。
    raw_byte_vector<BYTE> _console_input_output_buffer;
    // _readbuf 中有效字节数，范围 0.._readbuf.size()。
    DWORD _read_total = 0;
    // true 表示 process_input 已经遇到行终止符或 Ctrl+Z 并完成当前 pending 读。
    bool _line_found = false;
    // 没有 pending 时提前到达的 vt_in 字节，或当前批次完成一行后剩余的尾部。
    bizwen::deque<char8_t> _queued_vt_input;

    // 低级 Win32/ConDrv I/O 封装，bridge 只通过它访问 server/vt_in/shutdown。
    pipe_bridge_io _io;
    // VT 输出批量缓冲，所有发送到宿主终端的字节最终从这里 flush 到 vt_out。
    vt_output_buffer _vt_output;
    // API handler 和输入/输出转换复用的临时缓冲集合。
    conversion_buffers _conversion;
    // GetConsoleProcessList 可见的连接进程快照。
    process_list_snapshot _processes;
    // bridge 对宿主终端当前光标和 cooked input 编辑边界的本地追踪。
    terminal_cursor_state _terminal;
    // 当前被挂起的 ConDrv 读请求及 VT EOF 状态。
    pending_io_state _pending;
    // 当前控制台会话的 DOSKEY 命令历史。
    command_history_state _history;

    // 计算 GetConsoleInput completion 可返回的 INPUT_RECORD 上限。OutputSize
    // 不含 CONSOLE_MSG_HEADER，记录数组前还要扣掉 L1 描述符。
    size_t console_input_max_records(const miniio::io_msg &msg) const noexcept
    {
        const auto output_buffer = msg.descriptor.OutputSize > sizeof(CONSOLE_GETCONSOLEINPUT_MSG)
                                       ? msg.descriptor.OutputSize - sizeof(CONSOLE_GETCONSOLEINPUT_MSG)
                                       : 0;
        return output_buffer / sizeof(INPUT_RECORD);
    }

    // 计算 RAW_READ 可以直接写入 completion 的最大字节数。
    size_t raw_read_capacity(const miniio::io_msg &msg) const noexcept
    {
        // RAW_READ completion 只写客户端缓冲区字节，不附加 API 描述符。
        return std::min<size_t>(msg.descriptor.OutputSize, sizeof(msg.body));
    }

    void prepare_console_input_completion(miniio::io_msg &msg, CONSOLE_GETCONSOLEINPUT_MSG *req, bool peek,
                                          size_t max_count)
    {
        const auto records_to_copy = std::min(inp.available(), max_count);
        const auto size = sizeof(CONSOLE_GETCONSOLEINPUT_MSG) + records_to_copy * sizeof(INPUT_RECORD);
        _console_input_output_buffer.resize(size);

        auto *out_req = reinterpret_cast<CONSOLE_GETCONSOLEINPUT_MSG *>(_console_input_output_buffer.data());
        *out_req = *req;
        out_req->NumRecords = static_cast<ULONG>(records_to_copy);

        auto *out_records =
            reinterpret_cast<INPUT_RECORD *>(_console_input_output_buffer.data() + sizeof(CONSOLE_GETCONSOLEINPUT_MSG));
        if (records_to_copy != 0)
        {
            if (peek)
                inp.peek(out_records, records_to_copy);
            else
                inp.read(out_records, records_to_copy);
        }

        miniio::prepare_completion(msg, 0, size);
        msg.complete.Write.Data = _console_input_output_buffer.data();
        msg.complete.Write.Size = static_cast<ULONG>(size);
    }

    // 计算 ReadConsole 文本输出区容量。返回值只包含用户文本字节，不包含
    // CONSOLE_READCONSOLE_MSG descriptor。
    size_t console_read_data_capacity(const miniio::io_msg &msg) const noexcept
    {
        // USER_DEFINED ReadConsole completion 先返回 CONSOLE_READCONSOLE_MSG，
        // 随后的文本才是客户端 lpBuffer。OutputSize 不含
        // CONSOLE_MSG_HEADER，因此只扣掉 API 描述符。
        const auto local_capacity = sizeof(msg.body) - sizeof(CONSOLE_MSG_HEADER) - sizeof(CONSOLE_READCONSOLE_MSG);
        if (msg.descriptor.OutputSize <= sizeof(CONSOLE_READCONSOLE_MSG))
            return 0;
        return std::min<size_t>(msg.descriptor.OutputSize - sizeof(CONSOLE_READCONSOLE_MSG), local_capacity);
    }

  public:
    // true 表示当前有一个 ConDrv 读请求被 bridge 持有，必须等 VT 输入/EOF
    // 后显式 COMPLETE_IO。
    bool has_pending() const noexcept
    {
        return _pending.has_pending();
    }
    // 会话可退出条件：终端输入已经 EOF，并且没有仍需完成给 ConDrv 的请求。
    bool should_exit() const noexcept
    {
        return (_pending.vt_eof() || _io.shutdown_signaled()) && !_pending.has_pending();
    }

    // 绑定 signal 线程的 shutdown event。该 event 只用于打断等待，不代表
    // vt_in 一定还有可读字节。
    void set_signal_shutdown_event(win32::handle_view event) noexcept
    {
        _io.set_shutdown_event(event);
    }

    // 检查 shutdown event 是否已触发；触发后 pending read 应按 EOF 完成。
    [[nodiscard]] bool is_signal_shutdown_signaled() const noexcept
    {
        return _io.shutdown_signaled();
    }

    // 查询 vt_in 当前可读字节数。返回 false 表示管道关闭或不可读，调用方会
    // 将 pending 状态推进到 EOF。
    [[nodiscard]] bool peek_vt_input(DWORD &avail) noexcept
    {
        return _io.peek_available(avail);
    }

    enum class vt_read_status
    {
        // 至少有一个新字节进入 _readbuf。
        bytes,
        // vt_in 暂时没有可读字节，pending 状态未改变。
        empty,
        // vt_in 已关闭或读取失败，调用方应按 EOF 完成 pending。
        eof,
        // _readbuf 已达到当前请求容量，调用方应先完成 pending。
        full,
    };

    // 非阻塞读取 vt_in。函数优先消费 _queued_vt_input，再从 pipe 读取当前
    // 可见字节；不会等待新输入到达。
    [[nodiscard]] vt_read_status read_available_vt_input()
    {
        // room==0 表示本次 pending 可返回的缓冲已满，调用方必须先完成它。
        auto limit = _readbuf.size();
        if (_pending.kind() == PendingKind::RawRead && _pending.raw_read())
            limit = raw_read_capacity(*_pending.raw_read());
        DWORD room = static_cast<DWORD>(limit) - _read_total;
        if (room == 0)
            return vt_read_status::full;

        if (consume_queued_vt_input(room))
            return vt_read_status::bytes;

        DWORD read = 0;
        auto result = _io.read_available(std::span{_readbuf}.subspan(_read_total, room), read);
        if (result == vt_pipe_read_status::bytes)
        {
            _read_total += read;
            return vt_read_status::bytes;
        }

        return result == vt_pipe_read_status::empty ? vt_read_status::empty : vt_read_status::eof;
    }

    // 阻塞读取 vt_in。只允许在没有 shutdown event 的路径调用，因为 ReadFile
    // 阻塞后无法被 signal 线程唤醒。
    [[nodiscard]] vt_read_status read_blocking_vt_input()
    {
        // 阻塞读取只在没有 shutdown event 时使用；有 shutdown event 的路径必须
        // 按时间片等待，否则关闭通知不能打断 ReadFile。
        DWORD room = static_cast<DWORD>(_readbuf.size()) - _read_total;
        if (room == 0)
            return vt_read_status::full;

        if (consume_queued_vt_input(room))
            return vt_read_status::bytes;

        DWORD read = 0;
        auto result = _io.read_blocking(std::span{_readbuf}.subspan(_read_total, room), read);
        if (result == vt_pipe_read_status::bytes)
        {
            _read_total += read;
            return vt_read_status::bytes;
        }

        return result == vt_pipe_read_status::empty ? vt_read_status::empty : vt_read_status::eof;
    }

    // 解析刚刚追加到 _readbuf 的新字节，并把由输入产生的 VT echo 立即 flush。
    // old_total 是追加前 _readbuf 的有效长度。
    void process_new_vt_input(DWORD old_total)
    {
        // old_total 是读取前的有效字节数。_readbuf 中旧字节已经被解析过，
        // 但可能仍要保留给 RawRead/ReadConsole completion 返回。
        _line_found = false;
        process_input(_readbuf.data() + old_total, _read_total - old_total);
        vt_flush();
    }

    // 将当前或后续 pending 读取推进到 EOF completion。EOF 标志是会话级状态，
    // 所以先写入 _pending，再复用 complete_pending 的统一出口。
    void complete_pending_with_eof()
    {
        // EOF 必须先记录到 pending 状态，再走正常 completion。这样 RawRead 和
        // ReadConsole 共享同一套“返回 0 字节/0 记录”的完成语义。
        _pending.set_vt_eof(true);
        complete_pending();
    }

    // 等待 signal/shutdown event 一个短时间片。返回 true 表示关闭信号已到达，
    // pending 读不应继续等待用户输入。
    [[nodiscard]] bool wait_for_signal_shutdown_slice()
    {
        // shutdown event 只代表关闭/轮询时间片；VT 输入不会唤醒它。
        return _io.wait_shutdown_slice(pending_vt_input_wait_ms);
    }

    // ── 持久转换缓冲区访问器 ──
    // 返回共享 UTF-32 转换缓冲。API handlers 只在单次请求内使用该缓冲；
    // 下一次 handler 调用可能覆盖内容。
    raw_u32_buffer &conv_u32() noexcept
    {
        return _conversion.u32();
    }
    // 返回共享 UTF-8/ANSI 字节缓冲，用于 API 边界转码和 completion 组包。
    raw_u8_buffer &conv_u8() noexcept
    {
        return _conversion.utf8();
    }
    // 返回共享 UTF-16/wchar_t 缓冲，用于 Win32 API descriptor 和别名/标题转换。
    raw_wide_buffer &conv_wstr() noexcept
    {
        return _conversion.wide();
    }
    // 返回共享 CHAR_INFO 缓冲，用于 Read/WriteConsoleOutput 矩形转换。
    std::vector<CHAR_INFO> &conv_char_info() noexcept
    {
        return _conversion.char_info();
    }
    // 返回共享 INPUT_RECORD 缓冲，用于 WriteConsoleInput 或 pending completion。
    std::vector<INPUT_RECORD> &conv_input_records() noexcept
    {
        return _conversion.input_records();
    }
    // 读取 USER_DEFINED 消息中 descriptor 后的变长输入。小载荷直接返回 msg.body
    // 切片；超过 body 的部分通过 READ_INPUT 读入 _input_payload_buffer。
    std::span<const BYTE> read_input_payload(const miniio::io_msg &msg, size_t offset)
    {
        if (offset >= msg.descriptor.InputSize)
            return {};

        const auto size = static_cast<size_t>(msg.descriptor.InputSize) - offset;
        if (offset <= sizeof(msg.body) && size <= sizeof(msg.body) - offset)
            return {msg.body + offset, size};

        auto &buffer = _input_payload_buffer;
        buffer.resize(size);
        _io.read_input(msg.descriptor.Identifier, static_cast<ULONG>(offset), std::span{buffer.data(), buffer.size()});
        return {reinterpret_cast<const BYTE *>(buffer.data()), buffer.size()};
    }

    // 返回 Console 输出方向的 VT parser。api_write_console/raw_write 路径用它
    // 解析应用输出中的 VT 序列并同步本地状态。
    vt_parser &output_parser() noexcept
    {
        return _output_parser;
    }

    // 取走 Enter 本地 echo 后留下的一次性 CUP 修正标志。返回 true 时，
    // api_write_console 会先移动到 get_enter_dest()，再输出应用内容。
    bool consume_enter_newline()
    {
        // Enter 的本地回显已经把终端推进到下一行，但应用随后的输出通常
        // 仍从 Console 光标位置开始。这里把“下一次输出前先 CUP”的一次性
        // 动作交给 api_write_console 消费，避免空行被重复插入。
        const auto dest = _terminal.enter_dest();
        if (!_terminal.consume_enter_newline())
        {
            LOG3("[bridge] consume_enter: false");
            return false;
        }
        LOG3("[bridge] consume_enter: TRUE, dest=(%d,%d)", dest.X, dest.Y);
        return true;
    }
    // 返回 consume_enter_newline 成功时应 CUP 到的终端坐标；仅在 enter
    // newline pending 状态有效。
    COORD get_enter_dest() const noexcept
    {
        return _terminal.enter_dest();
    }
    // api_set_cursor_pos / api_fill_output 全屏清空时 shell 已自行管理光标，
    // 必须清除 Enter 遗留的假换行标志，否则下一条 WriteConsole 会错误 CUP
    void reset_enter_newline() noexcept
    {
        _terminal.reset_enter_newline();
    }

    // 开始继承终端当前光标；下一条 CPR 响应会更新 console_state 和
    // terminal_cursor_state。
    void set_pending_inherit_cursor() noexcept
    {
        _terminal.set_pending_inherit_cursor();
    }

    // WriteConsole/RAW_WRITE 完成后同步 bridge 追踪的终端光标，并把当前位置
    // 作为下一次 cooked input 的左边界。
    void sync_cursor_after_write(COORD pos)
    {
        // Console 输出是新的行编辑边界：后续 ReadConsole 的左右移动不得越过
        // 本次输出结束位置，否则用户可以删到 shell prompt 或上一条输出。
        const auto terminal_pos = active_screen_buffer().viewport.relative_position(pos);
        const auto old_cursor = _terminal.cursor();
        LOG3("[bridge] sync_cursor_after_write: pos=(%d,%d) was_tc=(%d,%d) was_col_start=%d was_col_end=%d enter_nl=%d",
             pos.X, pos.Y, old_cursor.X, old_cursor.Y, _terminal.input_column_start(), _terminal.input_column_end(),
             _terminal.enter_newline_pending());
        term_cursor_set(terminal_pos);
        bounds_reset(terminal_pos.X);
    }

    // ════════════════════════════════════════════════════
    //  VT 输出器
    // ════════════════════════════════════════════════════

    // 将累计 VT 输出写入 vt_out，并清空输出缓冲。
    void vt_flush()
    {
        _vt_output.flush();
    }

    // true 表示 vt_out 缓冲中还有尚未写给宿主终端的数据。
    [[nodiscard]] bool has_buffered_vt_output() const noexcept
    {
        return _vt_output.buffered_size() != 0;
    }

    // true 表示 vt_out 缓冲已达到主动刷新阈值。
    [[nodiscard]] bool should_flush_vt_output() const noexcept
    {
        return _vt_output.should_flush();
    }

    // ── 缓冲追加方法 ──

    // 追加已经是 UTF-8/ASCII VT 的字节序列。
    void vt_append_str(std::string_view s)
    {
        _vt_output.append(s);
    }

    // 追加单个 ASCII/VT 字节。
    void vt_append_char(char c)
    {
        _vt_output.append(c);
    }

    // 追加 VT 参数整数；用于 CUP/SGR/resize 等序列参数。
    void vt_append_int(int n)
    {
        _vt_output.append_int(n);
    }

    // 追加 SGR 参数并自动维护分号分隔。first 为 true 表示这是第一个参数。
    void vt_append_sgr_param(bool &first, int value)
    {
        if (!first)
            vt_append_char(';');
        first = false;
        vt_append_int(value);
    }

    // 追加两位小写十六进制字节，用于 OSC 4 rgb:RR/GG/BB。
    void vt_append_hex_byte(uint8_t value)
    {
        constexpr char hex_digits[] = "0123456789abcdef";
        vt_append_char(hex_digits[value >> 4]);
        vt_append_char(hex_digits[value & 15]);
    }

    // 透传 parser 保存的原始 VT 序列。text 指向 parser raw 缓冲，调用方必须
    // 在 parser.reset 前调用本函数。
    void vt_append_raw_sequence(std::u32string_view text)
    {
        COREHOST_PERF_SCOPE_AMOUNT(vt_raw_passthrough, text.size());
        _vt_output.append_utf32(text);
    }

    // ── 高层 VT 序列 ──

    // 发送 viewport-relative CUP。row/col 是 0-based 终端坐标，函数内部转换为
    // VT 需要的 1-based 参数。
    void vt_write_cup(SHORT row, SHORT col)
    {
        // 内部坐标沿用 Console 的 0-based COORD；VT CUP 参数是 1-based。
        vt_append_str("\x1b["sv);
        vt_append_int(static_cast<int>(row) + 1);
        vt_append_char(';');
        vt_append_int(static_cast<int>(col) + 1);
        vt_append_char('H');
    }

    // 将 screen_buffer 绝对坐标转换为 viewport-relative 坐标后发送 CUP。
    void vt_write_cup_buffer(COORD buffer_position)
    {
        const auto terminal_position = active_screen_buffer().viewport.clamped_relative_position(buffer_position);
        vt_write_cup(terminal_position.Y, terminal_position.X);
    }

    // 判断 bridge 追踪的终端光标是否已经位于指定 buffer 坐标。
    [[nodiscard]] bool terminal_cursor_matches_buffer(COORD buffer_position) const noexcept
    {
        if (!_terminal.cursor_valid())
            return false;
        const auto terminal_position = active_screen_buffer().viewport.clamped_relative_position(buffer_position);
        const auto cursor = _terminal.cursor();
        return cursor.X == terminal_position.X && cursor.Y == terminal_position.Y;
    }

    // 把 Win32 legacy 属性序列化为 SGR。该函数只改变终端当前图形属性，
    // screen_buffer 的属性状态由调用方同步维护。
    void vt_write_attr(WORD attr)
    {
        // Console 属性低 4 位是前景 BGRI，高 4 位是背景 BGRI；映射表把
        // Win32 颜色编号转换为 SGR 的 ANSI/bright ANSI 编号。
        vt_append_str("\x1b[0"sv);
        auto fg = attr & 0x0F;
        auto bg = (attr >> 4) & 0x0F;
        auto fl = (attr >> 8) & 0xFF;
        if (fl & COMMON_LVB_REVERSE_VIDEO)
            vt_append_str(";7"sv);
        if (fl & COMMON_LVB_UNDERSCORE)
            vt_append_str(";4"sv);
        if (fl & COMMON_LVB_GRID_HORIZONTAL)
            vt_append_str(";9"sv);
        const int fg_map[] = {30, 34, 32, 36, 31, 35, 33, 37, 90, 94, 92, 96, 91, 95, 93, 97};
        const int bg_map[] = {40, 44, 42, 46, 41, 45, 43, 47, 100, 104, 102, 106, 101, 105, 103, 107};
        vt_append_char(';');
        vt_append_int(fg_map[fg & 15]);
        vt_append_char(';');
        vt_append_int(bg_map[bg & 15]);
        vt_append_char('m');
    }

    // 写入一个屏幕单元的首 codepoint。0 按空格输出，避免重绘时留下旧字形。
    void vt_write_cell(char32_t ch)
    {
        // screen_buffer 用 0 表示空单元格。终端没有“空字符”，重绘时用空格
        // 清除对应列，避免旧字形残留。
        if (ch == 0)
            ch = U' ';
        _vt_output.append_cell(ch);
    }

    // 清空宿主终端可见屏幕并移动到左上角。
    void vt_write_clear_screen()
    {
        vt_append_str("\x1b[2J\x1b[H"sv);
    }

    // 发送 CPR 查询；响应会从 vt_in 回到 process_input_cpr_response。
    void vt_write_dsr_cpr()
    {
        vt_append_str("\x1b[6n"sv);
    }

    // 使用 OSC 0 设置宿主终端标题。title 是 UTF-32 状态文本。
    void vt_write_window_title(std::u32string_view title)
    {
        if (title.empty())
            return;
        // WT 接收 OSC 0/2 标题。这里使用 OSC 0 同时覆盖 icon/window title；
        // COM/defterm 的启动标题最终也会走到这条 VT 输出路径。
        vt_append_str("\x1b]0;"sv);
        _vt_output.append_utf32(title);
        vt_append_char('\x07');
    }

    // 输出普通文本消息，不解析其中 VT；调用方已确认 text 是地面态文本。
    void vt_write_text(std::u32string_view text)
    {
        COREHOST_PERF_SCOPE_AMOUNT(vt_msg_send_text, text.size());
        _vt_output.append_utf32(text);
    }

    // 输出 CRLF，并让终端按 Windows 控制台换行语义前进到下一行行首。
    void vt_write_crlf()
    {
        vt_write_cell(U'\r');
        vt_write_cell(U'\n');
    }

    // ── vt_msg_send: vt_message → UTF-8 序列化并追加到缓冲 ──
    // handler 调用此方法替代直接拼接原始 VT 字节。
    // 注意: 不会自动 flush，调用方负责在合适的时机 vt_flush()。
    template <vt_message_id id>
    void vt_msg_send(const vt_message &msg)
    {
        COREHOST_PERF_SCOPE_AMOUNT(vt_msg_send, id == vt_message_id::text ? msg.payload.text.size() : 0);
        // vt_message 是 parser 的结构化中间形态。这里把它重新序列化为宿主
        // 终端可理解的 VT，方便 API handler 与原始 VT 输入共用输出路径。
        switch (id)
        {
        case vt_message_id::cursor_position:
            vt_write_cup(static_cast<SHORT>(msg.payload.position.row - 1),
                         static_cast<SHORT>(msg.payload.position.col - 1));
            break;

        case vt_message_id::cursor_horiz_absolute:
            vt_append_str("\x1b["sv);
            vt_append_int(msg.payload.position.col);
            vt_append_char('G');
            break;

        case vt_message_id::cursor_vert_absolute:
            vt_append_str("\x1b["sv);
            vt_append_int(msg.payload.position.row);
            vt_append_char('d');
            break;

        case vt_message_id::cursor_up:
            if (msg.payload.count.value > 1)
            {
                vt_append_str("\x1b["sv);
                vt_append_int(msg.payload.count.value);
                vt_append_char('A');
            }
            else
                vt_append_str("\x1b[A"sv);
            break;

        case vt_message_id::cursor_down:
            if (msg.payload.count.value > 1)
            {
                vt_append_str("\x1b["sv);
                vt_append_int(msg.payload.count.value);
                vt_append_char('B');
            }
            else
                vt_append_str("\x1b[B"sv);
            break;

        case vt_message_id::cursor_forward:
            if (msg.payload.count.value > 1)
            {
                vt_append_str("\x1b["sv);
                vt_append_int(msg.payload.count.value);
                vt_append_char('C');
            }
            else
                vt_append_str("\x1b[C"sv);
            break;

        case vt_message_id::cursor_backward:
            if (msg.payload.count.value > 1)
            {
                vt_append_str("\x1b["sv);
                vt_append_int(msg.payload.count.value);
                vt_append_char('D');
            }
            else
                vt_append_str("\x1b[D"sv);
            break;

        case vt_message_id::cursor_next_line:
            if (msg.payload.count.value > 1)
            {
                vt_append_str("\x1b["sv);
                vt_append_int(msg.payload.count.value);
                vt_append_char('E');
            }
            else
                vt_append_str("\x1b[E"sv);
            break;

        case vt_message_id::cursor_prev_line:
            if (msg.payload.count.value > 1)
            {
                vt_append_str("\x1b["sv);
                vt_append_int(msg.payload.count.value);
                vt_append_char('F');
            }
            else
                vt_append_str("\x1b[F"sv);
            break;

        case vt_message_id::sgr: {
            vt_append_str("\x1b["sv);
            if (msg.payload.sgr.has_reset())
            {
                vt_append_char('0');
                vt_append_char('m');
                break;
            }

            bool first = true;

            // 重置 → 先发 0
            vt_append_sgr_param(first, 0);

            if (msg.payload.sgr.has(vt_sgr_flag::bold))
                vt_append_sgr_param(first, 1);
            if (msg.payload.sgr.has(vt_sgr_flag::faint))
                vt_append_sgr_param(first, 2);
            if (msg.payload.sgr.has(vt_sgr_flag::italic))
                vt_append_sgr_param(first, 3);
            if (msg.payload.sgr.has(vt_sgr_flag::underline))
                vt_append_sgr_param(first, 4);
            if (msg.payload.sgr.has(vt_sgr_flag::blink))
                vt_append_sgr_param(first, 5);
            if (msg.payload.sgr.has(vt_sgr_flag::negative))
                vt_append_sgr_param(first, 7);
            if (msg.payload.sgr.has(vt_sgr_flag::conceal))
                vt_append_sgr_param(first, 8);
            if (msg.payload.sgr.has(vt_sgr_flag::strikethrough))
                vt_append_sgr_param(first, 9);

            if (msg.payload.sgr.clears(vt_sgr_flag::bold) || msg.payload.sgr.clears(vt_sgr_flag::faint))
                vt_append_sgr_param(first, 22);
            if (msg.payload.sgr.clears(vt_sgr_flag::italic))
                vt_append_sgr_param(first, 23);
            if (msg.payload.sgr.clears(vt_sgr_flag::underline))
                vt_append_sgr_param(first, 24);
            if (msg.payload.sgr.clears(vt_sgr_flag::blink))
                vt_append_sgr_param(first, 25);
            if (msg.payload.sgr.clears(vt_sgr_flag::negative))
                vt_append_sgr_param(first, 27);
            if (msg.payload.sgr.clears(vt_sgr_flag::conceal))
                vt_append_sgr_param(first, 28);
            if (msg.payload.sgr.clears(vt_sgr_flag::strikethrough))
                vt_append_sgr_param(first, 29);

            if (msg.payload.sgr.fg.is_default())
                vt_append_sgr_param(first, 39);
            else if (msg.payload.sgr.fg.is_rgb())
            {
                vt_append_sgr_param(first, 38);
                vt_append_sgr_param(first, 2);
                vt_append_sgr_param(first, msg.payload.sgr.fg.value);
                vt_append_sgr_param(first, msg.payload.sgr.fg.g);
                vt_append_sgr_param(first, msg.payload.sgr.fg.b);
            }
            else if (msg.payload.sgr.fg.is_indexed() && msg.payload.sgr.fg.value <= 7)
                vt_append_sgr_param(first, 30 + msg.payload.sgr.fg.value);
            else if (msg.payload.sgr.fg.is_indexed() && msg.payload.sgr.fg.value <= 15)
                vt_append_sgr_param(first, 90 + (msg.payload.sgr.fg.value - 8));
            else if (msg.payload.sgr.fg.is_indexed())
            {
                vt_append_sgr_param(first, 38);
                vt_append_sgr_param(first, 5);
                vt_append_sgr_param(first, msg.payload.sgr.fg.value);
            }

            if (msg.payload.sgr.bg.is_default())
                vt_append_sgr_param(first, 49);
            else if (msg.payload.sgr.bg.is_rgb())
            {
                vt_append_sgr_param(first, 48);
                vt_append_sgr_param(first, 2);
                vt_append_sgr_param(first, msg.payload.sgr.bg.value);
                vt_append_sgr_param(first, msg.payload.sgr.bg.g);
                vt_append_sgr_param(first, msg.payload.sgr.bg.b);
            }
            else if (msg.payload.sgr.bg.is_indexed() && msg.payload.sgr.bg.value <= 7)
                vt_append_sgr_param(first, 40 + msg.payload.sgr.bg.value);
            else if (msg.payload.sgr.bg.is_indexed() && msg.payload.sgr.bg.value <= 15)
                vt_append_sgr_param(first, 100 + (msg.payload.sgr.bg.value - 8));
            else if (msg.payload.sgr.bg.is_indexed())
            {
                vt_append_sgr_param(first, 48);
                vt_append_sgr_param(first, 5);
                vt_append_sgr_param(first, msg.payload.sgr.bg.value);
            }

            vt_append_char('m');
            break;
        }

        case vt_message_id::carriage_return:
            vt_write_cell(U'\r');
            break;

        case vt_message_id::line_feed:
            vt_write_crlf();
            break;

        case vt_message_id::text: {
            vt_write_text(msg.payload.text);
            break;
        }

        case vt_message_id::save_cursor:
        case vt_message_id::ansi_save_cursor:
            vt_append_str("\x1b"
                          "7"sv);
            break;

        case vt_message_id::restore_cursor:
        case vt_message_id::ansi_restore_cursor:
            vt_append_str("\x1b"
                          "8"sv);
            break;

        case vt_message_id::cursor_show:
            vt_append_str("\x1b[?25h"sv);
            break;

        case vt_message_id::cursor_hide:
            vt_append_str("\x1b[?25l"sv);
            break;

        case vt_message_id::scroll_up:
            if (msg.payload.count.value > 1)
            {
                vt_append_str("\x1b["sv);
                vt_append_int(msg.payload.count.value);
                vt_append_char('S');
            }
            else
                vt_append_str("\x1b[S"sv);
            break;

        case vt_message_id::scroll_down:
            if (msg.payload.count.value > 1)
            {
                vt_append_str("\x1b["sv);
                vt_append_int(msg.payload.count.value);
                vt_append_char('T');
            }
            else
                vt_append_str("\x1b[T"sv);
            break;

        case vt_message_id::insert_lines:
            vt_append_str("\x1b["sv);
            vt_append_int(msg.payload.count.value);
            vt_append_char('L');
            break;

        case vt_message_id::delete_lines:
            vt_append_str("\x1b["sv);
            vt_append_int(msg.payload.count.value);
            vt_append_char('M');
            break;

        case vt_message_id::erase_in_display:
            vt_append_str("\x1b["sv);
            vt_append_int(msg.payload.erase_mode);
            vt_append_char('J');
            break;

        case vt_message_id::erase_in_line:
            vt_append_str("\x1b["sv);
            vt_append_int(msg.payload.erase_mode);
            vt_append_char('K');
            break;

        case vt_message_id::set_scrolling_region:
            vt_append_str("\x1b["sv);
            vt_append_int(msg.payload.scroll_region.top);
            vt_append_char(';');
            vt_append_int(msg.payload.scroll_region.bottom);
            vt_append_char('r');
            break;

        case vt_message_id::set_window_title:
            vt_write_window_title(msg.payload.title);
            break;

        case vt_message_id::set_cursor_shape:
            vt_append_str("\x1b["sv);
            vt_append_int(msg.payload.cursor_shape);
            vt_append_str(" q"sv);
            break;

        case vt_message_id::cursor_enable_blinking:
            vt_append_str("\x1b[?12h"sv);
            break;

        case vt_message_id::cursor_disable_blinking:
            vt_append_str("\x1b[?12l"sv);
            break;

        case vt_message_id::designate_charset_line_drawing:
            vt_append_str("\x1b(0"sv);
            break;

        case vt_message_id::designate_charset_ascii:
            vt_append_str("\x1b(B"sv);
            break;

        // ── 简单 ESC 序列 ──
        case vt_message_id::reverse_index:
            vt_append_str("\x1bM"sv);
            break;
        case vt_message_id::horizontal_tab_set:
            vt_append_str("\x1bH"sv);
            break;
        case vt_message_id::keypad_app_mode:
            vt_append_str("\x1b="sv);
            break;
        case vt_message_id::keypad_numeric_mode:
            vt_append_str("\x1b>"sv);
            break;

        // ── 文本修改 (count 驱动) ──
        case vt_message_id::insert_characters:
            vt_append_str("\x1b["sv);
            vt_append_int(msg.payload.count.value);
            vt_append_char('@');
            break;
        case vt_message_id::delete_characters:
            vt_append_str("\x1b["sv);
            vt_append_int(msg.payload.count.value);
            vt_append_char('P');
            break;
        case vt_message_id::erase_characters:
            vt_append_str("\x1b["sv);
            vt_append_int(msg.payload.count.value);
            vt_append_char('X');
            break;

        // ── OSC 4 调色板 ──
        case vt_message_id::set_palette_color: {
            vt_append_str("\x1b]4;"sv);
            vt_append_int(msg.payload.palette.index);
            vt_append_char(';');
            // rgb:RR/GG/BB 格式 (不使用 snprintf)
            vt_append_str("rgb:"sv);
            vt_append_hex_byte(msg.payload.palette.r);
            vt_append_char('/');
            vt_append_hex_byte(msg.payload.palette.g);
            vt_append_char('/');
            vt_append_hex_byte(msg.payload.palette.b);
            vt_append_char('\x07');
            break;
        }

        // ── 制表符移动 ──
        case vt_message_id::cursor_forward_tab:
            vt_append_str("\x1b["sv);
            vt_append_int(msg.payload.count.value);
            vt_append_char('I');
            break;
        case vt_message_id::cursor_backward_tab:
            vt_append_str("\x1b["sv);
            vt_append_int(msg.payload.count.value);
            vt_append_char('Z');
            break;

        // ── 制表符清除 ──
        case vt_message_id::tab_clear_current:
            vt_append_str("\x1b[0g"sv);
            break;
        case vt_message_id::tab_clear_all:
            vt_append_str("\x1b[3g"sv);
            break;

        // ── 交替缓冲区 ──
        case vt_message_id::use_alternate_buffer:
            vt_append_str("\x1b[?1049h"sv);
            break;
        case vt_message_id::use_main_buffer:
            vt_append_str("\x1b[?1049l"sv);
            break;

        // ── 列宽切换 (DECCOLM) ──
        case vt_message_id::set_columns_132:
            vt_append_str("\x1b[?3h"sv);
            break;
        case vt_message_id::set_columns_80:
            vt_append_str("\x1b[?3l"sv);
            break;

        // ── 软复位 (DECSTR) ──
        case vt_message_id::soft_reset:
            vt_append_str("\x1b[!p\x1b[?9001h"sv);
            break;

        // ── 查询 (发送到终端, 响应经 vt_in 返回) ──
        case vt_message_id::report_cursor_position:
            vt_append_str("\x1b[6n"sv);
            break;
        case vt_message_id::device_attributes:
            vt_append_str("\x1b[0c"sv);
            break;

        // ── 光标键模式 ──
        case vt_message_id::cursor_keys_app_mode:
            vt_append_str("\x1b[?1h"sv);
            break;
        case vt_message_id::cursor_keys_normal_mode:
            vt_append_str("\x1b[?1l"sv);
            break;

        // ── 无 VT 输出的消息 (键盘输入、内部标记) ──
        case vt_message_id::continue_:
        default:
            break;
        }
    }

    // ════════════════════════════════════════════════════
    //  I/O Handlers
    // ════════════════════════════════════════════════════

    // ── RAW_READ (挂起模式) ──
    // 建立 RAW_READ pending，并尽量同步消费已经到达的 vt_in 字节。
    bool handle_raw_read(miniio::io_msg &msg)
    {
        // RAW_READ 是 ANSI/raw byte read；Ctrl+Z 只在 ENABLE_PROCESSED_INPUT
        // 下代表 EOF。
        if (_pending.vt_eof())
        {
            miniio::prepare_completion(msg, 0, 0);
            return true;
        }

        _pending.begin_raw_read(msg, (cstate.input_mode & ENABLE_PROCESSED_INPUT) != 0);
        // 新的 RawRead 请求不能继承上一次读取留下的扫描状态；否则旧的
        // _line_found 会让本次请求被错误地立即完成。
        _read_total = 0;
        _line_found = false;

        if (accumulate_from_pipe())
            return true;
        return false;
    }

    // ── ReadConsole ──
    bool handle_console_read(miniio::io_msg &msg, bool proc_z, const BYTE *init_data, DWORD init_bytes)
    {
        // USER_DEFINED ReadConsole body 位于 CONSOLE_MSG_HEADER 后。返回时只写
        // CONSOLE_READCONSOLE_MSG 和后续文本，不把 header 回传给客户端。
        auto *req = reinterpret_cast<CONSOLE_READCONSOLE_MSG *>(msg.body + sizeof(CONSOLE_MSG_HEADER));
        LOG3("[bridge] handle_console_read: vt_eof=%d proc_z=%d init_bytes=%lu", _pending.vt_eof(), proc_z, init_bytes);
        if (_pending.vt_eof())
        {
            req->NumBytes = 0;
            req->ControlKeyState = 0;
            auto sz = static_cast<ULONG>(sizeof(CONSOLE_READCONSOLE_MSG));
            miniio::prepare_completion(msg, 0, sz);
            msg.complete.Write.Data = msg.body + sizeof(CONSOLE_MSG_HEADER);
            msg.complete.Write.Size = sz;
            return true;
        }

        _pending.begin_console_read(msg, proc_z);
        _read_total = 0;
        _line_found = false;
        _cooked_buf.clear();
        _cooked_cursor = 0;
        _history.reset_browse();
        LOG3("[bridge] handle_console_read: pending ConsoleRead, unicode=%d", req->Unicode != 0);

        if (init_data && init_bytes > 0)
        {
            // ReadConsole 支持调用方提供初始输入。它已经属于本次读取结果，
            // 因此既放进 _readbuf，也提前进入 cooked line 缓冲。
            if (init_bytes > _readbuf.size())
                init_bytes = static_cast<DWORD>(_readbuf.size());
            std::memcpy(_readbuf.data(), init_data, init_bytes);
            _read_total = init_bytes;
            // ── 预填充 _cooked_buf：解码 init_data 并累积到行缓冲 ──
            process_input(_readbuf.data(), init_bytes);
            LOG3("[bridge] handle_console_read: seeded %lu init bytes, cooked=%zu", init_bytes, _cooked_buf.size());
        }

        if (consume_input_buffer_for_console_read())
        {
            LOG3("[bridge] handle_console_read: completed from input_buffer");
            return true;
        }

        if (accumulate_from_pipe())
        {
            LOG3("[bridge] handle_console_read: sync complete");
            return true;
        }
        LOG3("[bridge] handle_console_read: pending, returning false");
        return false;
    }

    // GetConsoleInput 路径：先把可用 VT 输入准备成 INPUT_RECORD，再按 READ/PEEK/
    // NOWAIT 标志从 input_buffer 返回；可等待且无记录时进入 ConsoleInput pending。
    bool handle_console_input(miniio::io_msg &msg)
    {
        // GetConsoleInput 直接服务于 input_buffer。PEEK 不消费记录；NOWAIT
        // 在没有记录时必须同步返回 0，而不是挂起。
        prepare_console_input_events();

        auto *req = reinterpret_cast<CONSOLE_GETCONSOLEINPUT_MSG *>(msg.body + sizeof(CONSOLE_MSG_HEADER));
        if ((req->Flags & ~CONSOLE_READ_VALID) != 0)
        {
            miniio::prepare_completion(msg, 0xC000000D /* STATUS_INVALID_PARAMETER */);
            return true;
        }

        const auto max_count = console_input_max_records(msg);
        if (max_count == 0)
        {
            req->NumRecords = 0;
            miniio::prepare_completion(msg, 0, sizeof(CONSOLE_GETCONSOLEINPUT_MSG));
            msg.complete.Write.Data = msg.body + sizeof(CONSOLE_MSG_HEADER);
            msg.complete.Write.Size = sizeof(CONSOLE_GETCONSOLEINPUT_MSG);
            return true;
        }

        const bool peek = (req->Flags & CONSOLE_READ_NOREMOVE) != 0;
        const bool wait_allowed = (req->Flags & CONSOLE_READ_NOWAIT) == 0;
        const auto count = inp.available();

        if (count == 0 && wait_allowed)
        {
            // 只有“可等待且当前无记录”的请求进入 pending；后续 emit_key 会
            // 调 complete_pending_console_input 唤醒它。
            _pending.begin_console_input(msg);
            return false;
        }

        prepare_console_input_completion(msg, req, peek, max_count);
        return true;
    }

    // 为 GetConsoleInput 主动准备 INPUT_RECORD。没有 pending 请求时，函数会
    // 从 vt_in 非阻塞读取可用字节并转成 input_buffer 事件；有 pending 时由
    // 对应 pending 路径独占消费输入。
    void prepare_console_input_events()
    {
        if (_pending.has_pending())
            return;

        for (;;)
        {
            if (_pending.vt_eof())
                return;

            _read_total = 0;

            if (!_queued_vt_input.empty())
            {
                drain_available_vt_input();
                continue;
            }

            DWORD avail = 0;
            if (!peek_vt_input(avail))
            {
                _pending.set_vt_eof(true);
                return;
            }
            if (avail == 0)
                return;

            if (!queue_available_vt_input())
                return;
        }
    }

    // 非阻塞抽取当前可用 VT 输入并推进 pending 请求。返回 true 表示状态已
    // 变化：读到字节、完成请求、缓冲满或 EOF；false 仅表示暂时无输入。
    bool drain_available_vt_input()
    {
        // 先非阻塞读取，避免在已经有 VT 输入可用时浪费 16ms 时间片。
        const auto old_total = _read_total;
        switch (read_available_vt_input())
        {
        case vt_read_status::bytes:
            process_new_vt_input(old_total);
            return true;
        case vt_read_status::empty:
            return false;
        case vt_read_status::full:
            complete_pending();
            return true;
        case vt_read_status::eof:
            complete_pending_with_eof();
            return true;
        default:
            std::unreachable();
        }
    }

    // 在存在 pending 读请求时等待 VT 输入或 shutdown。函数只等待必要对象：
    // 有 shutdown event 时按 16ms 时间片轮询，无 event 时才允许阻塞读 vt_in。
    void wait_for_pending_vt_input()
    {
        vt_flush();

        if (_pending.vt_eof() || !_pending.has_pending())
            return;

        if (drain_available_vt_input())
        {
            return;
        }

        if (is_signal_shutdown_signaled())
        {
            // signal 线程已经确认控制管道关闭。pending 读不能继续等待用户
            // 输入，只能按 EOF 完成，让主循环有机会退出。
            complete_pending_with_eof();
            return;
        }

        if (wait_for_signal_shutdown_slice())
        {
            complete_pending_with_eof();
            return;
        }
        if (_io.has_shutdown_event())
        {
            // 有 shutdown event 的模式不能进入阻塞 ReadFile；这里再试一次
            // 非阻塞 drain，没读到就把控制权还给 io_loop。
            drain_available_vt_input();
            return;
        }

        const auto old_total = _read_total;
        switch (read_blocking_vt_input())
        {
        case vt_read_status::bytes:
            process_new_vt_input(old_total);
            return;
        case vt_read_status::full:
            complete_pending();
            return;
        case vt_read_status::eof:
            complete_pending_with_eof();
            return;
        case vt_read_status::empty:
            return;
        default:
            std::unreachable();
        }
    }

    // ── on_idle ──
    // 没有 ConDrv 消息时推进终端输入。它负责支持 GetConsoleInput(PEEK) 这种
    // 不挂起 ReadConsole 的输入模型，同时检测终端关闭。
    void on_idle()
    {
        if (_pending.vt_eof())
            return;

        // 即使没有 pending ReadConsole，也要轮询 vt_in。PowerShell/PSReadLine
        // 常用 GetConsoleInput(PEEK) 驱动输入；如果只在 ReadConsole pending
        // 时读 vt_in，键盘事件和终端断开都会被饿死。
        DWORD avail = 0;
        if (!peek_vt_input(avail))
        {
            _pending.set_vt_eof(true);
            if (_pending.has_pending())
                complete_pending();
            return;
        }

        if (avail == 0)
        {
            // PeekNamedPipe 的 0 字节只是“暂时没输入”。只有 signal 线程通知
            // 关闭时才把它提升为 EOF。
            if (is_signal_shutdown_signaled())
            {
                LOG3("[bridge] on_idle: signal shutdown event set, marking EOF");
                _pending.set_vt_eof(true);
                if (_pending.has_pending())
                    complete_pending();
                return;
            }
            return;
        }

        LOG3("[bridge] on_idle: avail=%lu kind=%d total=%lu", avail, static_cast<int>(_pending.kind()), _read_total);
        if (!_pending.has_pending())
        {
            // edit/PSReadLine 这类程序会先等待 InputAvailableEvent，再调用
            // GetConsoleInput。idle 不能只保存 VT 字节；必须立即解析为
            // INPUT_RECORD 并写入 input_buffer，才能唤醒等待 stdin 的客户端。
            drain_available_vt_input();
            return;
        }

        if (accumulate_from_pipe())
            return;
    }

    // 取消当前挂起读取并立即完成给 ConDrv。Flush/Disconnect 类路径用它把
    // bridge 从 pending 状态拉回可继续 READ_IO 的状态。
    void cancel_pending_read()
    {
        // DISCONNECT 或外部关闭需要释放当前 ConDrv 请求；completion 的具体
        // 形态仍由 complete_pending 根据 pending 类型决定。
        if (_pending.has_pending())
            complete_pending();
    }

    // 直接把 Console API 文本按当前输出代码页转成 UTF-8 写入 VT 输出缓冲。
    // 该函数只负责转码/发送，不更新 screen_buffer 或 cursor 状态。
    void raw_write(bool uni, BYTE *data, DWORD bytes)
    {
        if (bytes == 0)
            return;
        // Console API 写入可能是 UTF-16 或控制台输出代码页的 ANSI 字节。
        // 这里不能用 corehost 进程 ACP；客户端可通过 SetConsoleOutputCP
        // 修改 console output CP，WriteFile/RAW_WRITE 应跟随该状态。
        if (uni)
        {
            auto *ws = reinterpret_cast<const wchar_t *>(data);
            int wl = static_cast<int>(bytes / sizeof(wchar_t));
            _vt_output.append_utf16(std::wstring_view{ws, static_cast<size_t>(wl)});
        }
        else
        {
            auto *s = reinterpret_cast<const char *>(data);
            const auto cp = cstate.output_code_page ? cstate.output_code_page : CP_ACP;
            if (cp == CP_UTF8)
            {
                vt_append_str(std::string_view{s, bytes});
                return;
            }
#ifdef COREHOST_ANSI_OPT
            if (cp == code_page_gbk)
            {
                _vt_output.append_gbk(std::string_view{s, bytes});
                return;
            }
#endif

            auto &wide = _conversion.wide();
            convert_ansi_to_wstr(s, bytes, cp, wide);
            _vt_output.append_utf16(wide_view(wide));
        }
    }

    // ════════════════════════════════════════════════════
    //  Layer 1 — 独立状态操作 (每个函数只改一个状态域)
    // ════════════════════════════════════════════════════

    // ── 编辑缓冲 (_cooked_buf, _cooked_cursor) ──
    // 在 cooked line 的编辑光标处插入字符，并把编辑光标移动到插入字符之后。
    void cooked_append(char32_t ch)
    {
        // _cooked_cursor 是插入点而不是字符索引缓存；插入后移动到新字符后方。
        _cooked_buf.insert(_cooked_cursor, 1, ch);
        ++_cooked_cursor;
    }
    // 删除编辑光标左侧字符，用于 Backspace；光标已在函数内回退。
    void cooked_pop_before()
    {
        if (_cooked_cursor == 0)
            return;
        _cooked_buf.erase(--_cooked_cursor, 1);
    }
    // 删除编辑光标所在字符，用于 Delete；编辑光标位置保持不变。
    void cooked_pop_at()
    {
        if (_cooked_cursor >= _cooked_buf.size())
            return;
        _cooked_buf.erase(_cooked_cursor, 1);
    }
    // 清空当前 cooked line，并把编辑光标重置到行首。
    void cooked_clear()
    {
        _cooked_buf.clear();
        _cooked_cursor = 0;
    }
    // 设置 cooked line 编辑光标。调用方必须保证 p 不超过 _cooked_buf.size()。
    void cooked_set_pos(size_t p)
    {
        _cooked_cursor = p;
    }
    // 判断编辑光标是否位于 cooked line 末尾；用于选择追加回显还是重绘后缀。
    bool cooked_at_end() const noexcept
    {
        return _cooked_cursor >= _cooked_buf.size();
    }

    // ── 终端原始 echo (经 VT 缓冲批量写入，消除逐字节 WriteFile) ──
    void echo_byte(BYTE b)
    {
        vt_append_char(static_cast<char>(b));
    }

    // ── term 光标追踪 ──
    // 设置 bridge 追踪的终端光标坐标。该坐标是 viewport-relative，不是
    // screen_buffer 绝对坐标。
    void term_cursor_set(COORD c)
    {
        _terminal.set_cursor(c);
    }
    // 本地回显一个单列字符后推进终端光标追踪状态。
    void term_cursor_advance()
    {
        _terminal.advance();
    }
    // 本地删除/左移后回退终端光标追踪状态。
    void term_cursor_retreat()
    {
        _terminal.retreat();
    }
    // 根据 _cooked_cursor 和当前输入起始列计算终端列号。
    SHORT term_cursor_col() const noexcept
    {
        return _terminal.column_for_offset(_cooked_cursor);
    };

    // 将行编辑左右边界重设为终端列 x。应用输出完成后，新输入不能越过该列。
    void bounds_reset(SHORT x)
    {
        _terminal.reset_bounds(x);
    }
    // cooked line 增加一个字符后扩展输入右边界。
    void bounds_extend()
    {
        _terminal.extend_bounds();
    }
    // cooked line 删除一个字符后收缩输入右边界。
    void bounds_retract()
    {
        _terminal.retract_bounds();
    }

    // ── KEY_EVENT 输出到 input_buffer ──
    void emit_key(WORD vk, WCHAR uc)
    {
        // Win32Input 模式下终端输入要转成 KEY_EVENT，供 GetConsoleInput
        // 使用。这里生成 KEY_DOWN，是否补 KEY_UP 由调用者按路径决定。
        INPUT_RECORD r{};
        r.EventType = KEY_EVENT;
        r.Event.KeyEvent.bKeyDown = TRUE;
        r.Event.KeyEvent.wRepeatCount = 1;
        r.Event.KeyEvent.wVirtualKeyCode = vk;
        r.Event.KeyEvent.wVirtualScanCode = static_cast<WORD>(::MapVirtualKeyW(vk, MAPVK_VK_TO_VSC));
        r.Event.KeyEvent.uChar.UnicodeChar = uc;
        r.Event.KeyEvent.dwControlKeyState = 0;
        inp.write(&r, 1);
        complete_pending_console_input();
    }
    // 写入 KEY_DOWN/KEY_UP 一对事件。普通按键输入给 GetConsoleInput 时使用；
    // Enter 等特殊路径可只写 KEY_DOWN。
    void emit_key_pair(WORD vk, WCHAR uc)
    {
        emit_key(vk, uc);
        INPUT_RECORD up{};
        up.EventType = KEY_EVENT;
        up.Event.KeyEvent.bKeyDown = FALSE;
        up.Event.KeyEvent.wRepeatCount = 1;
        up.Event.KeyEvent.wVirtualKeyCode = vk;
        up.Event.KeyEvent.wVirtualScanCode = static_cast<WORD>(::MapVirtualKeyW(vk, MAPVK_VK_TO_VSC));
        up.Event.KeyEvent.uChar.UnicodeChar = 0;
        up.Event.KeyEvent.dwControlKeyState = 0;
        inp.write(&up, 1);
        complete_pending_console_input();
    }

    // ════════════════════════════════════════════════════
    //  Layer 2 — 高层组合 (编排 layer-1 操作)
    // ════════════════════════════════════════════════════

    // ── VT 辅助 ──
    // 移动终端光标并立即刷新，供行编辑在重绘后把用户光标放回逻辑位置。
    void cup_to(SHORT row, SHORT col)
    {
        LOG3("[vt] cup_to(%d,%d)", row, col);
        vt_write_cup(row, col);
        vt_flush();
    }
    // 从当前 cooked 光标开始重绘后缀，用于行中间插入/删除后修正终端显示。
    void repaint_suffix()
    {
        // 光标位于行中间插入/删除时，只重绘光标后的后缀。调用者随后会
        // CUP 回编辑光标，避免把用户光标留在行尾。
        auto sf = std::u32string_view(_cooked_buf).substr(_cooked_cursor);
        for (char32_t cp : sf)
            vt_write_cell(cp);
    }
    // 从输入起始列重绘整条 cooked line，用于历史导航和别名展开后刷新显示。
    void repaint_full_line()
    {
        // 历史导航替换整行时，从输入起始列清到行尾再写新内容。这里不清理
        // prompt 左侧内容，因为输入起始列是行编辑的左边界。
        if (!_terminal.cursor_valid())
        {
            LOG3("[history] repaint_full_line: SKIP tc_valid=0");
            return;
        }
        const auto cursor = _terminal.cursor();
        LOG3("[history] repaint_full_line: cup_to(%d,%d) cooked_sz=%zu", cursor.Y, _terminal.input_column_start(),
             _cooked_buf.size());
        cup_to(cursor.Y, _terminal.input_column_start());
        vt_append_str("\x1b[K"sv);
        for (char32_t cp : _cooked_buf)
            vt_write_cell(cp);
        vt_flush();
        _terminal.set_cursor_x(_terminal.input_column_end());
    }
    // 将 _history 当前选中的命令加载到 cooked line，并把编辑光标和终端显示
    // 都移动到新行尾。
    void load_history_line()
    {
        // 加载后光标移动到行尾，和 Windows 控制台历史浏览行为一致。
        cooked_set_pos(_cooked_buf.size());
        _terminal.set_bounds_end_for_length(_cooked_buf.size());
        const auto cursor = _terminal.cursor();
        LOG3("[history] load_history_line: tc=(%d,%d) col_start=%d col_end=%d cooked_sz=%zu", cursor.X, cursor.Y,
             _terminal.input_column_start(), _terminal.input_column_end(), _cooked_buf.size());
        repaint_full_line();
        const auto done_cursor = _terminal.cursor();
        LOG3("[history] load_history_line: done tc=(%d,%d)", done_cursor.X, done_cursor.Y);
    }

    // ── ConsoleRead 路径: 行编辑 ──

    // ConsoleRead 行编辑插入单字节字符。raw 用于本地 echo，ch 写入
    // _cooked_buf；多字节 Unicode 输入应使用 edit_insert_codepoint。
    void edit_insert_char(char32_t ch, BYTE raw)
    {
        // raw 是原始终端字节，适用于 ASCII/单字节输入的本地回显。多字节
        // UTF-8 输入走 edit_insert_codepoint，避免只回显其中一个字节。
        history_break_browse();
        cooked_append(ch);
        bounds_extend();
        if (!_terminal.cursor_valid())
            return;
        if (cooked_at_end())
        {
            echo_byte(raw);
            term_cursor_advance();
        }
        else
        {
            // 行中间插入时，终端已经显示新字符；再重绘后缀并回到插入点后一列。
            COORD sv = _terminal.cursor();
            echo_byte(raw);
            repaint_suffix();
            cup_to(sv.Y, sv.X + 1);
            _terminal.set_cursor_x(sv.X + 1);
        }
    }
    // ConsoleRead 行编辑插入 Unicode codepoint。函数负责 UTF-8 回显，并在
    // 行中间插入时重绘后缀。
    void edit_insert_codepoint(char32_t ch)
    {
        LOG3(L"[in] EDIT_CP ch=U+%04X cooked_sz=%zu", (unsigned)ch, _cooked_buf.size());
        // 该路径用于 Win32Input UnicodeChar 和 parser 聚合后的 UTF-8 文本。
        // 回显必须从 codepoint 编码为 UTF-8，不能复用单个 raw 字节。
        history_break_browse();
        cooked_append(ch);
        bounds_extend();
        if (!_terminal.cursor_valid())
            return;
        if (cooked_at_end())
        {
            vt_write_cell(ch);
            term_cursor_advance();
        }
        else
        {
            COORD sv = _terminal.cursor();
            vt_write_cell(ch);
            term_cursor_advance();
            repaint_suffix();
            cup_to(sv.Y, sv.X + 1);
            _terminal.set_cursor_x(sv.X + 1);
        }
    }
    // 提交 cooked line：回显 CRLF、标记本批输入完成，并完成 pending
    // ReadConsole。
    void edit_submit_line()
    {
        // ConsoleRead 的 Enter 在本地完成：把当前 cooked line 回给 ConDrv，
        // 并回显 CRLF。非 ConsoleRead/Win32Input 路径另有 enter_pending_newline。
        if (_terminal.cursor_valid())
        {
            vt_append_str("\r\n"sv);
            _terminal.crlf();
        }
        _line_found = true;
        complete_pending();
    }
    // 将 input_buffer 中的 KEY_EVENT 应用到 ConsoleRead 行编辑。返回 true
    // 表示 Enter 已完成 pending 读取；其他编辑键只改变 _cooked_buf/终端显示。
    bool edit_key_event_for_console_read(const KEY_EVENT_RECORD &key)
    {
        if (!key.bKeyDown)
            return false;

        switch (key.wVirtualKeyCode)
        {
        case VK_RETURN:
            edit_submit_line();
            return true;
        case VK_BACK:
            _edit_backspace();
            break;
        case VK_DELETE:
            _edit_delete();
            break;
        case VK_LEFT:
            _edit_move_left();
            break;
        case VK_RIGHT:
            _edit_move_right();
            break;
        case VK_HOME:
            _edit_home();
            break;
        case VK_END:
            _edit_end();
            break;
        case VK_UP:
            _edit_history_up();
            break;
        case VK_DOWN:
            _edit_history_down();
            break;
        default:
            if (key.uChar.UnicodeChar >= L' ' || key.uChar.UnicodeChar == L'\t')
                edit_insert_codepoint(static_cast<char32_t>(key.uChar.UnicodeChar));
            break;
        }
        return false;
    }
    // ConsoleRead pending 时优先消费已有 input_buffer 事件。这样
    // WriteConsoleInput 注入的键盘事件也能驱动 cooked line。
    bool consume_input_buffer_for_console_read()
    {
        INPUT_RECORD record{};
        while (_pending.kind() == PendingKind::ConsoleRead && inp.read(&record, 1) == 1)
        {
            if (record.EventType != KEY_EVENT)
                continue;
            if (edit_key_event_for_console_read(record.Event.KeyEvent))
                return true;
        }
        return false;
    }
    // ConsoleRead cooked 编辑的 Backspace：删除光标左侧字符、收缩输入边界，
    // 并用 VT 删除序列同步终端显示。
    void edit_backspace()
    {
        // Backspace 删除光标左侧字符。VT 的 D+P 先左移再删除当前位置字符，
        // 与 cooked_pop_before 后的新缓冲状态一致。
        history_break_browse();
        if (_cooked_cursor == 0)
            return;
        cooked_pop_before();
        bounds_retract();
        if (!_terminal.cursor_valid())
            return;
        vt_append_str("\x1b[D\x1b[P"sv);
        vt_flush();
        term_cursor_retreat();
        if (!cooked_at_end())
        {
            COORD c = _terminal.cursor();
            repaint_suffix();
            cup_to(c.Y, c.X);
        }
    }
    // ConsoleRead cooked 编辑的 Delete：删除光标所在字符，不移动 cooked 光标，
    // 并从终端当前位置删除一列。
    void edit_delete()
    {
        // Delete 删除光标所在字符，不移动编辑光标；VT P 从当前位置删除一列。
        history_break_browse();
        if (_cooked_cursor >= _cooked_buf.size())
            return;
        cooked_pop_at();
        bounds_retract();
        vt_append_str("\x1b[P"sv);
        vt_flush();
    }
    // ConsoleRead cooked 编辑左移：只移动 _cooked_cursor 和终端光标，不改变
    // _cooked_buf 内容。
    void edit_move_left()
    {
        // 左移只改变编辑插入点，不改 cooked 内容；CUP 目标列由起始列加
        // cooked_cursor 得到，避免累积终端相对移动误差。
        if (_cooked_cursor > 0)
        {
            cooked_set_pos(_cooked_cursor - 1);
            if (_terminal.cursor_valid())
            {
                cup_to(_terminal.cursor().Y, term_cursor_col());
                _terminal.set_cursor_x(term_cursor_col());
            }
        }
    }
    // ConsoleRead cooked 编辑右移：不能越过 _cooked_buf 末尾，终端列由
    // terminal_cursor_state 的输入边界换算。
    void edit_move_right()
    {
        // 右移不能越过 cooked_buf 末尾；输入末尾列只记录已显示尾列，
        // 不作为逻辑长度来源。
        if (_cooked_cursor < _cooked_buf.size())
        {
            cooked_set_pos(_cooked_cursor + 1);
            if (_terminal.cursor_valid())
            {
                cup_to(_terminal.cursor().Y, term_cursor_col());
                _terminal.set_cursor_x(term_cursor_col());
            }
        }
    }
    // 将编辑光标移动到当前输入边界的起始列。
    void edit_home()
    {
        cooked_set_pos(0);
        if (_terminal.cursor_valid())
        {
            cup_to(_terminal.cursor().Y, _terminal.input_column_start());
            _terminal.set_cursor_x(_terminal.input_column_start());
        }
    }
    // 将编辑光标移动到 cooked line 末尾。
    void edit_end()
    {
        cooked_set_pos(_cooked_buf.size());
        if (_terminal.cursor_valid())
        {
            cup_to(_terminal.cursor().Y, _terminal.input_column_end());
            _terminal.set_cursor_x(_terminal.input_column_end());
        }
    }

    // ── 历史导航 ──
    // 将已提交 cooked line 追加到本会话命令历史；空行由 command_history_state
    // 自己过滤。
    void history_push()
    {
        _history.push(_cooked_buf);
    }
    // 用户手动编辑后退出历史浏览模式，避免下一次 Up/Down 仍基于旧 browse
    // index。
    void history_break_browse()
    {
        _history.break_browse();
    }
    // 选择上一条历史命令并重绘 cooked line；如果首次浏览，会把当前输入作为
    // browse_down 的恢复值保存。
    void history_up()
    {
        const auto cursor = _terminal.cursor();
        LOG3("[history] history_up: tc=(%d,%d) col_start=%d col_end=%d history_sz=%zu idx=%zu", cursor.X, cursor.Y,
             _terminal.input_column_start(), _terminal.input_column_end(), _history.size(), _history.browse_index());
        if (!_history.browse_up(_cooked_buf, _cooked_buf))
        {
            LOG3("[history] history_up: empty, return");
            return;
        }
        LOG3("[history] history_up: loading idx=%zu cooked_sz=%zu", _history.browse_index(), _cooked_buf.size());
        load_history_line();
    }
    // 选择下一条历史命令或恢复进入历史浏览前的当前输入。
    void history_down()
    {
        if (!_history.browse_down(_cooked_buf))
            return;
        load_history_line();
    }

    // ── 别名展开 ──
    void expand_alias()
    {
        // Alias 只匹配第一个空格前的命令名；参数部分原样拼回展开结果。
        if (_cooked_buf.empty())
            return;
        const auto word_end = std::ranges::find(_cooked_buf, U' ');
        const auto we = static_cast<size_t>(word_end - _cooked_buf.begin());
        auto &wk = _conversion.wide();
        wk.clear();
        wk.reserve(we);
        std::transform(_cooked_buf.begin(), word_end, std::back_inserter(wk),
                       [](char32_t ch) { return static_cast<wchar_t>(ch); });
        auto it = cstate.aliases.find(std::wstring_view{wk.data(), wk.size()});
        if (it == cstate.aliases.end())
        {
            LOG3("[bridge] alias not found");
            return;
        }
        auto &ex = _conversion.u32();
        ex.clear();
        ex.reserve(it->second.size() + _cooked_buf.size() - we);
        std::transform(it->second.begin(), it->second.end(), std::back_inserter(ex),
                       [](wchar_t wc) { return static_cast<char32_t>(wc); });
        if (we < _cooked_buf.size())
            ex.append_range(std::u32string_view{_cooked_buf}.substr(we));
        _cooked_buf.clear();
        _cooked_buf.append(ex.data(), ex.size());
    }

    // ── 非 ConsoleRead (PowerShell) 路径: 只发 KEY_DOWN ──
    WORD ascii_to_vk(WCHAR ch) const noexcept
    {
        // 该回退映射只服务于非 Win32Input 的普通文本路径。复杂组合键应由
        // vt_input_engine 根据结构化 VT 消息转换。
        if (ch >= L'a' && ch <= L'z')
            return static_cast<WORD>(ch - 0x20);
        if (ch >= L'A' && ch <= L'Z')
            return static_cast<WORD>(ch);
        if (ch >= L'0' && ch <= L'9')
            return static_cast<WORD>(ch);
        if (ch == L'\r' || ch == L'\n')
            return VK_RETURN;
        if (ch == L' ')
            return VK_SPACE;
        if (ch == L'\t')
            return VK_TAB;
        if (ch == L'\b')
            return VK_BACK;
        SHORT vk = ::VkKeyScanW(ch);
        return (vk != -1) ? LOBYTE(vk) : 0;
    }
    // 非 ConsoleRead 路径把普通文本转成 KEY_EVENT 写入 input_buffer。这里
    // 也维护 _cooked_buf，便于 Enter 后清理和兼容少量内部编辑状态。
    void input_printable(char32_t ch, BYTE raw)
    {
        WORD vk = ascii_to_vk(static_cast<WCHAR>(ch));
        LOG3(L"[in] PRINTABLE ch=U+%04X vk=0x%X uc=0x%X", (unsigned)ch, vk, (unsigned)(WCHAR)ch);
        // 非 ConsoleRead 模式下应用从 GetConsoleInput 获取按键事件；同时维护
        // cooked_buf 只用于本层对 Enter/历史等兼容行为的内部判断。
        emit_key_pair(vk, static_cast<WCHAR>(ch));
        if (ch != U'\r')
            cooked_append(ch);
        else
            cooked_clear();
    }
    // 非 ConsoleRead 路径写入 Enter KEY_DOWN，供 PowerShell/PSReadLine 这类
    // GetConsoleInput 驱动的 shell 提交当前行。
    void input_enter()
    {
        LOG3("[bridge] input ENTER");
        // PSReadLine 只需要 KEY_DOWN Enter 即可触发提交。这里不补 KEY_UP，
        // 避免某些 shell 把一组 Enter 解释为两次输入状态变化。
        emit_key(VK_RETURN, L'\r'); // D only — PSReadLine accepts Enter on KEY_DOWN
    }

    // ════════════════════════════════════════════════════
    //  兼容层 (process_input / 测试 / 旧调用者 使用)
    // ════════════════════════════════════════════════════
    // 兼容旧测试/旧调用点的薄包装：保持原来的函数名，但实际状态变化集中在
    // edit_* / input_* / history_* 函数里。
    // 在 cooked 光标处插入单字节字符。
    void _edit_insert(char32_t ch, BYTE raw)
    {
        edit_insert_char(ch, raw);
    }
    // Backspace 包装：删除 cooked 光标左侧字符。
    void _edit_backspace()
    {
        edit_backspace();
    }
    // Delete 包装：删除 cooked 光标所在字符。
    void _edit_delete()
    {
        edit_delete();
    }
    // Left 包装：移动 cooked 光标和终端光标。
    void _edit_move_left()
    {
        edit_move_left();
    }
    // Right 包装：移动 cooked 光标和终端光标。
    void _edit_move_right()
    {
        edit_move_right();
    }
    // Home 包装：回到当前 cooked input 起始列。
    void _edit_home()
    {
        edit_home();
    }
    // End 包装：移动到 cooked line 末尾。
    void _edit_end()
    {
        edit_end();
    }
    // Up 包装：浏览上一条历史，并记录当前状态到日志。
    void _edit_history_up()
    {
        const auto cursor = _terminal.cursor();
        LOG3("[history] _edit_history_up: tc=(%d,%d) col_start=%d col_end=%d cook_sz=%zu cook_pos=%zu", cursor.X,
             cursor.Y, _terminal.input_column_start(), _terminal.input_column_end(), _cooked_buf.size(),
             _cooked_cursor);
        history_up();
    }
    // Down 包装：浏览下一条历史或恢复进入浏览前的输入。
    void _edit_history_down()
    {
        history_down();
    }
    // DOSKEY alias 包装：在完成 ReadConsole 前替换 cooked line。
    void _expand_alias()
    {
        expand_alias();
    }
    // 非 ConsoleRead 文本包装：把普通字符写成 KEY_EVENT。
    void _write_char_key_event(char32_t ch, BYTE raw)
    {
        LOG3(L"[in] WRITE_KEY ch=U+%04X raw=0x%02X", (unsigned)ch, raw);
        input_printable(ch, raw);
    }
    // 非 ConsoleRead Enter 包装：只写 KEY_DOWN Enter。
    void _write_enter_key_event()
    {
        input_enter();
    }
    // 把已构造的 KEY_EVENT 以按下/释放一对写入 input_buffer。
    void _write_key_event_pair(const INPUT_RECORD &t)
    {
        emit_key_pair(t.Event.KeyEvent.wVirtualKeyCode, t.Event.KeyEvent.uChar.UnicodeChar);
    }

    // 应用输出方向的终端查询响应需要回到 Console input 队列。调用方传入
    // ASCII VT 响应序列；这里按字符写成 KEY_EVENT，让 ReadConsoleInputExW
    // 看到和真实终端回包一致的字节流。
    void inject_terminal_response(std::string_view response)
    {
        LOG3("[bridge] inject terminal response bytes=%zu", response.size());
        for (char ch : response)
            _write_char_key_event(static_cast<unsigned char>(ch), static_cast<BYTE>(ch));
    }

    // 应用发送 DSR CPR 查询时，corehost 作为 console host 返回当前可见
    // 光标位置。响应坐标是终端协议要求的 viewport-relative 1-based row/col。
    void inject_cursor_position_response()
    {
        std::array<char, 32> response{};
        char *out = response.data();
        *out++ = '\x1b';
        *out++ = '[';

        const auto terminal_pos = active_screen_buffer().viewport.clamped_relative_position(cstate.cursor.position);
        auto [row_end, row_ec] = std::to_chars(out, response.data() + response.size(), terminal_pos.Y + 1);
        assert(row_ec == std::errc{});
        out = row_end;
        *out++ = ';';
        auto [col_end, col_ec] = std::to_chars(out, response.data() + response.size(), terminal_pos.X + 1);
        assert(col_ec == std::errc{});
        out = col_end;
        *out++ = 'R';

        inject_terminal_response(std::string_view{response.data(), static_cast<size_t>(out - response.data())});
    }

    // 应用发送 DA 查询时，返回一个稳定的 VT100-style 响应。edit 只需要
    // 收到合法 DA 来结束启动探测，具体能力位当前不参与 corehost 状态。
    void inject_device_attributes_response()
    {
        inject_terminal_response("\x1b[?1;0c"sv);
    }

  private:
    // ════════════════════════════════════════════════════
    //  内部管道
    // ════════════════════════════════════════════════════

    // 把上一次 process_input 未消费完的 vt_in 尾部重新放入 _readbuf。room 是
    // 当前 pending completion 还可容纳的字节数。
    bool consume_queued_vt_input(DWORD room)
    {
        if (_queued_vt_input.empty())
            return false;

        const auto count = std::min<size_t>(room, _queued_vt_input.size());
        std::copy_n(_queued_vt_input.begin(), count, _readbuf.data() + _read_total);
        _queued_vt_input.erase(_queued_vt_input.begin(), _queued_vt_input.begin() + static_cast<std::ptrdiff_t>(count));
        _read_total += static_cast<DWORD>(count);
        LOG3("[bridge] consume_queued_vt_input: consumed=%zu remaining=%zu", count, _queued_vt_input.size());
        return true;
    }

    // 在没有 pending 请求时读取当前可用 vt_in 字节并追加到 _queued_vt_input。
    // 后续 GetConsoleInput/ReadConsole 会从该队列先消费，避免丢失提前到达的输入。
    bool queue_available_vt_input()
    {
        DWORD read = 0;
        auto result = _io.read_available(std::span{_readbuf}, read);
        if (result == vt_pipe_read_status::bytes)
        {
            _queued_vt_input.insert(_queued_vt_input.end(), _readbuf.data(), _readbuf.data() + read);
            LOG3("[bridge] queue_available_vt_input: read=%lu total=%zu", read, _queued_vt_input.size());
            return true;
        }

        if (result == vt_pipe_read_status::eof)
            _pending.set_vt_eof(true);
        return false;
    }

    // 把当前输入批次中 consumed 之后的字节放回队首。行终止符完成 pending 后，
    // 同一 ReadFile 批次里的下一行输入必须留给下一次请求。
    void queue_unprocessed_vt_input(const char8_t *bytes, DWORD consumed, DWORD len)
    {
        if (consumed >= len)
            return;

        const auto tail_size = static_cast<size_t>(len - consumed);
        _queued_vt_input.prepend_range(std::span{bytes + consumed, tail_size});

        const auto readbuf_begin = reinterpret_cast<std::uintptr_t>(_readbuf.data());
        const auto readbuf_end = readbuf_begin + _readbuf.size();
        const auto batch_begin = reinterpret_cast<std::uintptr_t>(bytes);
        if (batch_begin >= readbuf_begin && batch_begin <= readbuf_end)
        {
            const auto absolute_consumed = static_cast<DWORD>((batch_begin - readbuf_begin) + consumed);
            if (absolute_consumed < _read_total)
                _read_total = absolute_consumed;
        }

        LOG3("[bridge] queue_unprocessed_vt_input: queued_tail=%zu total=%zu", tail_size, _queued_vt_input.size());
    }

    // ── _echo_byte: 向终端输出单个字节并跟踪光标（经 VT 缓冲批量写入）──
    void _echo_byte(char8_t b)
    {
        // parse result 的 echo 只用于控制字符/ESC 路径。普通文本由 edit_* 处理，
        // 否则同一字节会同时进入 echo 和 cooked line，造成重复显示。
        vt_append_char(static_cast<char>(b));
        _terminal.apply_echo_byte(static_cast<BYTE>(b));
    }

    // 判断当前 RAW_READ 是否需要本地 echo。只有 raw read pending 且输入模式
    // 启用 ENABLE_ECHO_INPUT 时才回显。
    bool raw_read_echo_enabled() const noexcept
    {
        return _pending.kind() == PendingKind::RawRead && (cstate.input_mode & ENABLE_ECHO_INPUT) != 0;
    }

    // RAW_READ 文本字节回显。CR/LF 由行终止符路径统一规范化为 CRLF。
    void echo_raw_read_text_byte(char8_t b)
    {
        if (raw_read_echo_enabled() && b != static_cast<char8_t>('\r') && b != static_cast<char8_t>('\n'))
            vt_append_char(static_cast<char>(b));
    }

    // ── accumulate_from_pipe: 缓冲后统一走 process_input 解析 ──
    // 返回 true 表示已发现行终止符并完成 pending（调用方应 return）
    bool accumulate_from_pipe()
    {
        // 连续 drain 当前可用输入，直到无数据、完成 pending 或 EOF。这样一次
        // ConDrv 请求可以同步消化已经在管道里的整行输入。
        for (;;)
        {
            const auto old_total = _read_total;
            switch (read_available_vt_input())
            {
            case vt_read_status::empty:
                return false;
            case vt_read_status::full:
                LOG3("[bridge] accumulate: buffer full");
                complete_pending();
                return true;
            case vt_read_status::eof:
                _pending.set_vt_eof(true);
                complete_pending();
                return true;
            case vt_read_status::bytes:
                break;
            default:
                std::unreachable();
            }

            // process_input 在解码/解析同一遍里检测 \r/\n/Ctrl+Z，并直接完成
            // 当前 pending 读取。
            _line_found = false;
            process_input(_readbuf.data() + old_total, _read_total - old_total);
            // 批量 echo 后必须在本批输入结束时刷新，否则普通打字会滞留在 VT 输出缓冲，
            // 直到后续控制序列/应用输出/缓冲满才显示，表现为终端输入卡顿。
            vt_flush();

            if (_line_found)
                return true;
        }
    }

    // 处理终端 Win32 Input Mode 产生的键盘事件。ConsoleRead 使用事件驱动
    // cooked 行编辑；其他模式把事件写入 input_buffer。返回 true 表示事件
    // 已完成当前 pending，调用者应停止处理本批剩余字节。
    bool process_input_win32_key(PendingKind pending_kind, const vt_message &m, DWORD i, DWORD len,
                                 const char8_t *bytes)
    {
        if (pending_kind == PendingKind::ConsoleRead)
        {
            // ConsoleRead 自己做本地行编辑，只处理 KEY_DOWN；KEY_UP 不应
            // 改变 cooked buffer，也不应完成读取。
            if (!m.payload.win32_key.key_down)
                return false;

            INPUT_RECORD record{};
            record.EventType = KEY_EVENT;
            record.Event.KeyEvent.bKeyDown = TRUE;
            record.Event.KeyEvent.wVirtualKeyCode = static_cast<WORD>(m.payload.win32_key.vk);
            record.Event.KeyEvent.wVirtualScanCode = static_cast<WORD>(m.payload.win32_key.sc);
            record.Event.KeyEvent.uChar.UnicodeChar = static_cast<WCHAR>(m.payload.win32_key.uc);
            record.Event.KeyEvent.dwControlKeyState = m.payload.win32_key.control_state;
            if (edit_key_event_for_console_read(record.Event.KeyEvent))
            {
                queue_unprocessed_vt_input(bytes, i + 1, len);
                return true;
            }
            return false;
        }

        // Win32Input Enter 非 ConsoleRead: 设置换行标志 + 写终端 \r\n。
        if (m.payload.win32_key.key_down && m.payload.win32_key.vk == VK_RETURN)
        {
            const auto old_cursor = _terminal.cursor();
            LOG3("[bridge] ENTER_Win32Input was_tc=(%d,%d)", old_cursor.X, old_cursor.Y);
            _line_found = true;
            _terminal.crlf();
            _terminal.mark_enter_newline_at_cursor();
            vt_append_str("\r\n"sv);
            const auto cursor = _terminal.cursor();
            LOG3("[bridge] ENTER_Win32Input done tc=(%d,%d)", cursor.X, cursor.Y);
        }
        else if (pending_kind != PendingKind::RawRead && m.payload.win32_key.key_down)
        {
            const auto cursor = _terminal.cursor();
            LOG3("[bridge] Win32Input write_input: vk=%d uc=0x%04X cs=0x%X tc=(%d,%d)", m.payload.win32_key.vk,
                 m.payload.win32_key.uc, m.payload.win32_key.control_state, cursor.X, cursor.Y);
        }

        if (pending_kind != PendingKind::RawRead)
        {
            INPUT_RECORD ir{};
            ir.EventType = KEY_EVENT;
            ir.Event.KeyEvent.bKeyDown = m.payload.win32_key.key_down ? TRUE : FALSE;
            ir.Event.KeyEvent.wRepeatCount = m.payload.win32_key.repeat_count;
            ir.Event.KeyEvent.wVirtualKeyCode = m.payload.win32_key.vk;
            ir.Event.KeyEvent.wVirtualScanCode = m.payload.win32_key.sc;
            ir.Event.KeyEvent.uChar.UnicodeChar = m.payload.win32_key.uc;
            ir.Event.KeyEvent.dwControlKeyState = m.payload.win32_key.control_state;
            inp.write(&ir, 1);
            complete_pending_console_input();
        }
        return false;
    }

    // 处理左移输入：ConsoleRead 修改 cooked 插入点，其他模式产生 Left
    // KEY_EVENT。
    void process_input_move_left(PendingKind pending_kind, const vt_message &msg)
    {
        if (pending_kind == PendingKind::ConsoleRead)
            _edit_move_left();
        else
        {
            INPUT_RECORD ir;
            if (_engine.convert(vt_message_id::key_left, msg, ir))
                _write_key_event_pair(ir);
        }
    }

    // 处理右移输入：ConsoleRead 修改 cooked 插入点，其他模式产生 Right
    // KEY_EVENT。
    void process_input_move_right(PendingKind pending_kind, const vt_message &msg)
    {
        if (pending_kind == PendingKind::ConsoleRead)
            _edit_move_right();
        else
        {
            INPUT_RECORD ir;
            if (_engine.convert(vt_message_id::key_right, msg, ir))
                _write_key_event_pair(ir);
        }
    }

    // 处理 Home 类输入：ConsoleRead 回到输入起始列，其他模式产生 Home 事件。
    void process_input_home(PendingKind pending_kind, const vt_message &msg)
    {
        if (pending_kind == PendingKind::ConsoleRead)
            _edit_home();
        else
        {
            INPUT_RECORD ir;
            if (_engine.convert(vt_message_id::key_home, msg, ir))
                _write_key_event_pair(ir);
        }
    }

    // 处理 End 类输入：ConsoleRead 跳到 cooked 行尾，其他模式产生 End 事件。
    void process_input_end(PendingKind pending_kind, const vt_message &msg)
    {
        if (pending_kind == PendingKind::ConsoleRead)
            _edit_end();
        else
        {
            INPUT_RECORD ir;
            if (_engine.convert(vt_message_id::key_end, msg, ir))
                _write_key_event_pair(ir);
        }
    }

    // 处理 Delete 类输入：ConsoleRead 删除当前位置字符，其他模式产生 Delete
    // KEY_EVENT。
    void process_input_delete(PendingKind pending_kind, const vt_message &msg)
    {
        if (pending_kind == PendingKind::ConsoleRead)
            _edit_delete();
        else
        {
            INPUT_RECORD ir;
            if (_engine.convert(vt_message_id::key_delete, msg, ir))
                _write_key_event_pair(ir);
        }
    }

    // 处理 Backspace/Delete 字符输入：ConsoleRead 删除左侧字符，其他模式产生
    // Backspace KEY_EVENT。
    void process_input_backspace(PendingKind pending_kind, const vt_message &msg)
    {
        if (pending_kind == PendingKind::ConsoleRead)
            _edit_backspace();
        else
        {
            INPUT_RECORD ir;
            if (_engine.convert(vt_message_id::char_del, msg, ir))
                _write_key_event_pair(ir);
        }
    }

    // 处理 Up 输入：ConsoleRead 浏览历史，其他模式产生 Up KEY_EVENT。
    void process_input_history_up(PendingKind pending_kind, const vt_message &msg)
    {
        if (pending_kind == PendingKind::ConsoleRead)
            _edit_history_up();
        else
        {
            INPUT_RECORD ir;
            if (_engine.convert(vt_message_id::key_up, msg, ir))
                _write_key_event_pair(ir);
        }
    }

    // 处理 Down 输入：ConsoleRead 浏览历史，其他模式产生 Down KEY_EVENT。
    void process_input_history_down(PendingKind pending_kind, const vt_message &msg)
    {
        if (pending_kind == PendingKind::ConsoleRead)
            _edit_history_down();
        else
        {
            INPUT_RECORD ir;
            if (_engine.convert(vt_message_id::key_down, msg, ir))
                _write_key_event_pair(ir);
        }
    }

    // 将结构化 VT 键消息转换为 KEY_DOWN/KEY_UP 事件对。key_id 由调用者在
    // switch case 中静态指定，避免再次动态分派。
    template <vt_message_id key_id>
    void process_input_key_event(const vt_message &msg)
    {
        INPUT_RECORD rec;
        if (_engine.convert(key_id, msg, rec))
            _write_key_event_pair(rec);
    }

    // 处理键盘输入路径中的 CUP。当前只把 CUP 1;1 视为 Home 键兼容编码；
    // 其他绝对定位序列属于终端控制，不应进入 input_buffer。
    void process_input_cursor_position(const vt_message &msg)
    {
        // 某些终端把 Home 编码成 CUP 1;1。只有明确 1,1 时才作为 Home，
        // 其他 CUP 输入在键盘路径中不产生事件。
        if (msg.payload.position.row == 1 && msg.payload.position.col == 1)
        {
            INPUT_RECORD rec;
            if (_engine.convert(vt_message_id::key_home, msg, rec))
                _write_key_event_pair(rec);
        }
    }

    // 处理终端 CPR 响应。只有 pending inherit cursor 时才用响应更新
    // cstate.cursor；普通 CPR 响应由调用点按原始序列交还给应用。
    bool process_input_cpr_response(const vt_message &m)
    {
        if (_terminal.pending_inherit_cursor() && m.payload.cpr.row > 0 && m.payload.cpr.col > 0)
        {
            const COORD terminal_position{static_cast<SHORT>(m.payload.cpr.col - 1),
                                          static_cast<SHORT>(m.payload.cpr.row - 1)};
            cstate.cursor.position = active_screen_buffer().viewport.absolute_position(terminal_position);
            cstate.clamp_cursor_to_buffer();
            _terminal.finish_inherit_cursor(terminal_position);
            LOG3("[bridge] cpr_response: inherit cursor (%d,%d)", cstate.cursor.position.X, cstate.cursor.position.Y);
            return true;
        }
        return false;
    }

    // 处理普通 Tab 输入，写入 Tab KEY_EVENT 对。
    void process_input_tab()
    {
        INPUT_RECORD ir{};
        ir.EventType = KEY_EVENT;
        ir.Event.KeyEvent.wRepeatCount = 1;
        ir.Event.KeyEvent.wVirtualKeyCode = VK_TAB;
        ir.Event.KeyEvent.uChar.UnicodeChar = L'\t';
        _write_key_event_pair(ir);
    }

    // 处理终端 resize 通知：更新 console_state 尺寸、活动 screen_buffer 和
    // viewport，并把当前可见内容按新尺寸重绘到终端。
    void process_input_resize_window(const vt_message &msg)
    {
        COORD new_size{msg.payload.resize.cols, msg.payload.resize.rows};
        if (new_size.X <= 0 || new_size.Y <= 0)
            return;

        LOG3("[bridge] resize_window: old=(%d,%d) new=(%d,%d)", cstate.screen_buffer_size.X,
             cstate.screen_buffer_size.Y, new_size.X, new_size.Y);
        cstate.screen_buffer_size = new_size;
        cstate.max_window_size = new_size;
        auto &active_screen = active_screen_buffer();
        active_screen.viewport.reset_to_buffer(cstate.screen_buffer_size);
        active_screen.resize(new_size);

        vt_flush();
        vt_append_str("\x1b[8;"sv);
        vt_append_int(new_size.Y);
        vt_append_char(';');
        vt_append_int(new_size.X);
        vt_append_str("t"sv);
        vt_append_str("\x1b[2J\x1b[H"sv);
        WORD last_attr = 0xFFFF;
        for (SHORT y = 0; y != new_size.Y; ++y)
        {
            vt_write_cup(y, 0);
            for (SHORT x = 0; x != new_size.X; ++x)
            {
                WORD attr = active_screen.attr_at({x, y});
                if (attr != last_attr)
                {
                    vt_write_attr(attr);
                    last_attr = attr;
                }
                vt_write_cell(active_screen.at_u32({x, y}));
            }
        }
        vt_write_cup_buffer(cstate.cursor.position);
        vt_flush();
    }

    // 处理 parser 累积出的文本消息。ConsoleRead 写入 cooked line；其他非
    // RawRead 模式写入 KEY_EVENT。
    void process_input_text(PendingKind pending_kind, const vt_message &tm)
    {
        LOG3(L"[in] TEXT_MSG len=%zu", tm.payload.text.size());
        for (char32_t tc : tm.payload.text)
        {
            if (tc <= 0x1F || tc == 0x7F)
                continue;
            LOG3(L"[in] TEXT_DISP ch=U+%04X", (unsigned)tc);
            if (pending_kind == PendingKind::ConsoleRead)
                edit_insert_codepoint(tc);
            else
                _write_char_key_event(tc, static_cast<BYTE>(tc & 0xFF));
        }
    }

    // 应用发出的终端查询响应必须作为输入字符流返回给应用。只有 corehost
    // 自己发起的内部查询（例如继承光标 CPR）才在本层消费；其它完整控制
    // 序列按原文写入 input_buffer，让应用自己的 VT parser 处理。
    void emit_raw_sequence_as_input(std::u32string_view sequence)
    {
        LOG3(L"[in] RAW_SEQ len=%zu", sequence.size());
        for (char32_t ch : sequence)
            _write_char_key_event(ch, static_cast<BYTE>(ch & 0xFF));
    }

    // ── process_input: 解码 → 解析 → echo → 分发 ──
    // 在同一遍输入处理中检测 \r/\n/Ctrl+Z 并设置 _line_found。
    void process_input(const char8_t *bytes, DWORD len)
    {
        if (len > 0)
            LOG3_HEX("input", bytes, len);
        // _utf8_decoder 是流式状态机；多字节序列跨 ReadFile 边界时，前几次
        // 调用会产生 continuation，直到完整 codepoint 才交给 VT parser。
        for (DWORD i = 0; i != len; ++i)
        {
            char8_t b = bytes[i];
            echo_raw_read_text_byte(b);

            // ── Ctrl+Z 即时检测 ──
            if (_pending.process_control_z() && b == static_cast<char8_t>(0x1A)) [[unlikely]]
            {
                LOG3("[bridge] process_input: Ctrl+Z at offset %lu", i);
                _line_found = true;
                queue_unprocessed_vt_input(bytes, i + 1, len);
                complete_pending();
                return;
            }

            // ── 1. 解码：UTF-8 → char32_t ──
            auto decoded = _utf8_decoder(static_cast<uint8_t>(b));
            if (!decoded)
                continue;
            char32_t ch = *decoded;

            // ── 2. 解析 ──
            // parse() 接收一个 codepoint 范围。这里输入路径仍按单字符交互处理。
            const auto parsed = _input_parser.parse({&ch, 1});
            vt_message_id id = parsed.id;

            if (id == vt_message_id::continue_text) [[likely]]
            {
                // parser 把普通地面态文本累积在 msg.payload.text，但交互输入需要逐字符
                // 响应编辑键和回显，所以这里立即消费并重置文本累积。
                const auto pending_kind = _pending.kind();
                LOG3(L"[in] TEXT ch=U+%04X raw=0x%02X kind=%d", (unsigned)ch, static_cast<unsigned>(b),
                     (int)pending_kind);
                if (pending_kind == PendingKind::ConsoleRead)
                {
                    if (ch <= 0x7F)
                        _edit_insert(ch, static_cast<BYTE>(b));
                    else
                        edit_insert_codepoint(ch);
                }
                else if (pending_kind != PendingKind::RawRead)
                {
                    _write_char_key_event(ch, static_cast<BYTE>(b));
                }
                _input_parser.reset<vt_message_id::continue_text>(); // 清累积文本
                continue;
            }

            if (id == vt_message_id::continue_) [[likely]]
                continue;

            const auto pending_kind = _pending.kind();
            LOG3(L"[in] MSG id=%d kind=%d", (int)id, (int)pending_kind);
            if ((id == vt_message_id::carriage_return || id == vt_message_id::line_feed ||
                 id == vt_message_id::cursor_forward_tab) &&
                pending_kind == PendingKind::ConsoleRead)
                _echo_byte(b);

            const auto &msg = parsed.message;
            switch (id)
            {
            // 行终止符直接完成 pending 读取，并把同批次剩余字节退回队列。
            case vt_message_id::carriage_return: {
                if (pending_kind != PendingKind::ConsoleRead && pending_kind != PendingKind::RawRead)
                    _write_enter_key_event();
                _on_line_terminator(true, i, len, bytes);
                _input_parser.reset<vt_message_id::carriage_return>();
                return;
            }
            case vt_message_id::line_feed: {
                if (pending_kind != PendingKind::ConsoleRead && pending_kind != PendingKind::RawRead)
                    _write_char_key_event(U'\n', 0x0A);
                _on_line_terminator(false, i, len, bytes);
                _input_parser.reset<vt_message_id::line_feed>();
                return;
            }
            // Windows Terminal Win32 Input Mode 已经携带完整 KEY_EVENT 字段，
            // 不需要再通过 vt_input_engine 猜测 VK/修饰键。
            case vt_message_id::win32_input_key: {
                if (process_input_win32_key(pending_kind, msg, i, len, bytes))
                {
                    _input_parser.reset<vt_message_id::win32_input_key>();
                    return;
                }
                _input_parser.reset<vt_message_id::win32_input_key>();
                break;
            }
            // 方向键在 ConsoleRead 中是本地行编辑动作；在 GetConsoleInput
            // 驱动的 shell 中是 INPUT_RECORD。
            case vt_message_id::key_left: {
                process_input_move_left(pending_kind, msg);
                _input_parser.reset<vt_message_id::key_left>();
                break;
            }
            case vt_message_id::cursor_backward: {
                process_input_move_left(pending_kind, msg);
                _input_parser.reset<vt_message_id::cursor_backward>();
                break;
            }
            case vt_message_id::key_right: {
                process_input_move_right(pending_kind, msg);
                _input_parser.reset<vt_message_id::key_right>();
                break;
            }
            case vt_message_id::cursor_forward: {
                process_input_move_right(pending_kind, msg);
                _input_parser.reset<vt_message_id::cursor_forward>();
                break;
            }
            case vt_message_id::key_home: {
                process_input_home(pending_kind, msg);
                _input_parser.reset<vt_message_id::key_home>();
                break;
            }
            case vt_message_id::cursor_prev_line: {
                process_input_home(pending_kind, msg);
                _input_parser.reset<vt_message_id::cursor_prev_line>();
                break;
            }
            case vt_message_id::key_end: {
                process_input_end(pending_kind, msg);
                _input_parser.reset<vt_message_id::key_end>();
                break;
            }
            case vt_message_id::cursor_next_line: {
                process_input_end(pending_kind, msg);
                _input_parser.reset<vt_message_id::cursor_next_line>();
                break;
            }
            case vt_message_id::key_delete: {
                process_input_delete(pending_kind, msg);
                _input_parser.reset<vt_message_id::key_delete>();
                break;
            }
            case vt_message_id::char_del: {
                process_input_backspace(pending_kind, msg);
                _input_parser.reset<vt_message_id::char_del>();
                break;
            }
            case vt_message_id::key_up: {
                process_input_history_up(pending_kind, msg);
                _input_parser.reset<vt_message_id::key_up>();
                break;
            }
            case vt_message_id::cursor_up: {
                process_input_history_up(pending_kind, msg);
                _input_parser.reset<vt_message_id::cursor_up>();
                break;
            }
            case vt_message_id::key_down: {
                process_input_history_down(pending_kind, msg);
                _input_parser.reset<vt_message_id::key_down>();
                break;
            }
            case vt_message_id::cursor_down: {
                process_input_history_down(pending_kind, msg);
                _input_parser.reset<vt_message_id::cursor_down>();
                break;
            }
            // 其他结构化键不改变 cooked buffer，统一转换为 KEY_EVENT 对。
            case vt_message_id::key_f3: {
                process_input_key_event<vt_message_id::key_f3>(msg);
                _input_parser.reset<vt_message_id::key_f3>();
                break;
            }
            case vt_message_id::key_f4: {
                process_input_key_event<vt_message_id::key_f4>(msg);
                _input_parser.reset<vt_message_id::key_f4>();
                break;
            }
            case vt_message_id::key_f5: {
                process_input_key_event<vt_message_id::key_f5>(msg);
                _input_parser.reset<vt_message_id::key_f5>();
                break;
            }
            case vt_message_id::key_f6: {
                process_input_key_event<vt_message_id::key_f6>(msg);
                _input_parser.reset<vt_message_id::key_f6>();
                break;
            }
            case vt_message_id::key_f7: {
                process_input_key_event<vt_message_id::key_f7>(msg);
                _input_parser.reset<vt_message_id::key_f7>();
                break;
            }
            case vt_message_id::key_f8: {
                process_input_key_event<vt_message_id::key_f8>(msg);
                _input_parser.reset<vt_message_id::key_f8>();
                break;
            }
            case vt_message_id::key_f9: {
                process_input_key_event<vt_message_id::key_f9>(msg);
                _input_parser.reset<vt_message_id::key_f9>();
                break;
            }
            case vt_message_id::key_f10: {
                process_input_key_event<vt_message_id::key_f10>(msg);
                _input_parser.reset<vt_message_id::key_f10>();
                break;
            }
            case vt_message_id::key_f11: {
                process_input_key_event<vt_message_id::key_f11>(msg);
                _input_parser.reset<vt_message_id::key_f11>();
                break;
            }
            case vt_message_id::key_f12: {
                process_input_key_event<vt_message_id::key_f12>(msg);
                _input_parser.reset<vt_message_id::key_f12>();
                break;
            }
            case vt_message_id::key_insert: {
                process_input_key_event<vt_message_id::key_insert>(msg);
                _input_parser.reset<vt_message_id::key_insert>();
                break;
            }
            case vt_message_id::key_page_up: {
                process_input_key_event<vt_message_id::key_page_up>(msg);
                _input_parser.reset<vt_message_id::key_page_up>();
                break;
            }
            case vt_message_id::key_page_down: {
                process_input_key_event<vt_message_id::key_page_down>(msg);
                _input_parser.reset<vt_message_id::key_page_down>();
                break;
            }
            // 终端控制/响应类输入只更新 bridge 状态；不能作为用户按键回给
            // Console API。
            case vt_message_id::cursor_position: {
                process_input_cursor_position(msg);
                _input_parser.reset<vt_message_id::cursor_position>();
                break;
            }
            case vt_message_id::cursor_horiz_absolute: {
                _input_parser.reset<vt_message_id::cursor_horiz_absolute>();
                break;
            }
            case vt_message_id::cursor_vert_absolute: {
                _input_parser.reset<vt_message_id::cursor_vert_absolute>();
                break;
            }
            case vt_message_id::cpr_response: {
                if (!process_input_cpr_response(msg))
                    emit_raw_sequence_as_input(parsed.raw_sequence);
                _input_parser.reset<vt_message_id::cpr_response>();
                break;
            }
            case vt_message_id::cursor_forward_tab: {
                process_input_tab();
                _input_parser.reset<vt_message_id::cursor_forward_tab>();
                break;
            }
            // C0 控制字符可作为按键事件返回给 GetConsoleInput。
            case vt_message_id::char_sub: {
                process_input_key_event<vt_message_id::char_sub>(msg);
                _input_parser.reset<vt_message_id::char_sub>();
                break;
            }
            case vt_message_id::char_esc: {
                process_input_key_event<vt_message_id::char_esc>(msg);
                _input_parser.reset<vt_message_id::char_esc>();
                break;
            }
            case vt_message_id::resize_window: {
                process_input_resize_window(msg);
                _input_parser.reset<vt_message_id::resize_window>();
                break;
            }
            case vt_message_id::text: {
                process_input_text(pending_kind, msg);
                _input_parser.reset<vt_message_id::text>();
                break;
            }
            // 未知控制序列由 parser 保留原文，但输入方向当前不透传给应用；
            // 丢弃后重置 parser，避免半条未知序列污染后续输入。
            case vt_message_id::unknown_sequence: {
                emit_raw_sequence_as_input(parsed.raw_sequence);
                _input_parser.reset<vt_message_id::unknown_sequence>();
                break;
            }
            default: {
                if (!parsed.raw_sequence.empty())
                    emit_raw_sequence_as_input(parsed.raw_sequence);
                _input_parser.reset<vt_message_id::continue_>();
                break;
            }
            }
        }
    }
    // 行终止符公共收尾。consumed 是当前 ReadFile 批次中已经属于本次读取的
    // 字节数；剩余字节会退回队列，供下一次 pending 读取继续消费。
    void _finish_line_terminator(bool is_cr, bool has_lf, DWORD consumed, DWORD len, const char8_t *bytes)
    {
        _line_found = true;

        if (is_cr && !has_lf && !raw_read_echo_enabled())
            vt_append_char('\n');

        if (raw_read_echo_enabled())
            vt_append_str("\r\n"sv);

        queue_unprocessed_vt_input(bytes, consumed, len);

        // 本地 echo 已经让终端进入下一行；内部 cursor 必须同步，否则下一次
        // WriteConsole 会在旧行列计算输出位置。
        _terminal.crlf();

        if (_pending.kind() != PendingKind::ConsoleRead)
        {
            // 非 ConsoleRead 的 shell 通常随后通过 Console API 输出 prompt。
            // 标记一次性换行目标，让输出路径先 CUP 到新行首。
            _terminal.mark_enter_newline_at_cursor();
            const auto dest = _terminal.enter_dest();
            LOG3("[bridge] LINE_TERM enter_nl=1 dest=(%d,%d)", dest.X, dest.Y);
        }

        LOG3(L"[in] LINE_TERM cooked=[%.*ls]", static_cast<int>(_cooked_buf.size() < 200 ? _cooked_buf.size() : 200),
             _cooked_buf.data());
        complete_pending();
    }

    // 处理 CR/LF 行终止符：完成当前 pending 读取、保留同批次未消费输入、
    // 同步终端光标，并为非 ConsoleRead shell 设置下一次输出前的 CUP 修正。
    void _on_line_terminator(bool is_cr, DWORD i, DWORD len, const char8_t *bytes)
    {
        // 行终止符处理只消费当前行；i/len/bytes 描述当前 process_input 批次，
        // 用于识别 CRLF 是否跨 ReadFile 边界。
        auto consumed = i + 1;
        bool has_lf = false;

        if (is_cr)
        {
            if (i + 1 < len && bytes[i + 1] == static_cast<char8_t>('\n'))
            {
                has_lf = true;
                consumed = i + 2;
            }
            else if (i + 1 == len)
            {
                // CR 位于本批次末尾时，LF 可能已经在管道中但尚未读入。只在
                // 下一个字节确认为 LF 时消费它，避免吞掉下一行首字符。
                char8_t nb = {};
                if (_io.try_consume_byte(static_cast<char8_t>('\n'), nb))
                {
                    if (_read_total < _readbuf.size())
                        _readbuf[_read_total++] = nb;
                    has_lf = true;
                }
            }
        }

        _finish_line_terminator(is_cr, has_lf, consumed, len, bytes);
    }

    // 为 pending RAW_READ 构造 completion。它返回 _readbuf 中的原始字节，
    // 并在容量允许时把单独 CR/LF 规范化为 ConDrv 期望的 CRLF。
    void prepare_raw_read_completion(miniio::io_msg &m)
    {
        // RawRead 返回 _readbuf 原始字节；它不使用 cooked line 编辑结果。
        auto capacity = static_cast<DWORD>(raw_read_capacity(m));
        DWORD data_bytes = std::min(_read_total, capacity);

        // ConDrv read-line 语义期望 CRLF。终端可能只送 CR 或只送 LF，
        // 因此 completion 前把行尾规范化为 CRLF；但绝不超过客户端
        // ReadFile 提供的 OutputSize。
        if (data_bytes >= 1 && _readbuf[data_bytes - 1] == static_cast<char8_t>('\r'))
        {
            if (data_bytes < capacity)
                _readbuf[data_bytes++] = static_cast<char8_t>('\n');
        }
        else if (data_bytes >= 1 && _readbuf[data_bytes - 1] == static_cast<char8_t>('\n'))
        {
            if (data_bytes < 2 || _readbuf[data_bytes - 2] != static_cast<char8_t>('\r'))
            {
                if (data_bytes < capacity)
                {
                    _readbuf[data_bytes] = static_cast<char8_t>('\n');
                    _readbuf[data_bytes - 1] = static_cast<char8_t>('\r');
                    data_bytes++;
                }
            }
        }

        if (_pending.vt_eof() && _read_total == 0)
        {
            // EOF 且没有已读数据时返回空读取；这是会话退出的可观察信号。
            miniio::prepare_completion(m, 0, 0);
            return;
        }

        if (data_bytes > 0)
            std::memcpy(m.body, _readbuf.data(), data_bytes);
        miniio::prepare_completion(m, 0, data_bytes);
        m.complete.Write.Data = m.body;
        m.complete.Write.Size = data_bytes;
    }

    // 为 pending ReadConsole 构造 completion。函数把 _cooked_buf 转成调用者
    // 请求的 Unicode/ANSI 格式，并追加 CRLF；不会清理 pending 状态。
    void prepare_console_read_completion(miniio::io_msg &m)
    {
        auto *req = reinterpret_cast<CONSOLE_READCONSOLE_MSG *>(m.body + sizeof(CONSOLE_MSG_HEADER));
        req->ControlKeyState = 0;

        if (_pending.vt_eof() && _cooked_buf.empty() && _read_total == 0)
        {
            req->NumBytes = 0;
            auto sz = static_cast<ULONG>(sizeof(CONSOLE_READCONSOLE_MSG));
            miniio::prepare_completion(m, 0, sz);
            m.complete.Write.Data = m.body + sizeof(CONSOLE_MSG_HEADER);
            m.complete.Write.Size = sz;
            return;
        }

        // DOSKEY 别名展开发生在完成读取前；客户端只看到展开后的命令行。
        _expand_alias();

        auto *db = m.body + sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_READCONSOLE_MSG);
        DWORD maxd = static_cast<DWORD>(console_read_data_capacity(m));
        DWORD cp = 0;
        if (req->Unicode != 0)
        {
            auto *utf16_out = reinterpret_cast<wchar_t *>(db);
            auto max_chars = maxd / sizeof(wchar_t);
            auto copied_text =
                std::u32string_view{_cooked_buf.data(), u32_prefix_for_wide_units(_cooked_buf, max_chars)};
            size_t n = convert_u32_to_wide_raw(copied_text, utf16_out, max_chars);
            if (n < max_chars)
                utf16_out[n++] = L'\r';
            if (n < max_chars)
                utf16_out[n++] = L'\n';
            cp = static_cast<DWORD>(n * sizeof(wchar_t));
        }
        else
        {
            // ReadConsoleA 使用控制台输入代码页，而不是终端 UTF-8。否则
            // CJK 输入会以 UTF-8 字节交给期望 OEM/ANSI 的控制台程序。
            auto *ansi_out = reinterpret_cast<char *>(db);
            const auto text_capacity = maxd >= 2 ? static_cast<size_t>(maxd - 2) : size_t{0};
            const auto copied_text =
                std::u32string_view{_cooked_buf.data(), u32_prefix_for_ansi_bytes(_cooked_buf, cstate.input_code_page,
                                                                                  text_capacity, _conversion.wide())};
            size_t written = convert_u32_to_ansi_raw(copied_text, cstate.input_code_page, ansi_out, text_capacity,
                                                     _conversion.wide());
            written = append_ascii_raw('\r', ansi_out, maxd, written);
            written = append_ascii_raw('\n', ansi_out, maxd, written);
            cp = static_cast<DWORD>(written);
        }
        req->NumBytes = cp;
        auto sz = static_cast<ULONG>(sizeof(CONSOLE_READCONSOLE_MSG) + cp);
        miniio::prepare_completion(m, 0, sz);
        m.complete.Write.Data = m.body + sizeof(CONSOLE_MSG_HEADER);
        m.complete.Write.Size = sz;
    }

    // ── complete_pending ──────────────────────────────
    void complete_pending()
    {
        // complete_pending 是所有挂起读的唯一出口。它先构造 CD_IO_COMPLETE
        // 的副本，再清空 pending 状态，最后显式通知 ConDrv。
        LOG3(L"[bridge] complete_pending: kind=%d total=%lu cooked_len=%zu vt_eof=%d cooked=[%.*ls]",
             static_cast<int>(_pending.kind()), _read_total, _cooked_buf.size(), _pending.vt_eof(),
             static_cast<int>(_cooked_buf.size() < 200 ? _cooked_buf.size() : 200), _cooked_buf.data());
        if (!_pending.has_pending())
            return;

        if (_pending.kind() == PendingKind::ConsoleInput)
        {
            complete_pending_console_input();
            return;
        }

        CD_IO_COMPLETE comp_before{};
        CD_IO_COMPLETE *comp_ptr = nullptr;
        if (_pending.kind() == PendingKind::RawRead && _pending.raw_read())
        {
            auto &m = *_pending.raw_read();
            prepare_raw_read_completion(m);
            comp_before = m.complete;
            comp_ptr = &comp_before;
        }
        else if (_pending.console_read()) // ConsoleRead
        {
            auto &m = *_pending.console_read();
            // ConsoleRead 返回行编辑后的文本，而不是原始 VT 字节；退格、历史、
            // alias 等本地编辑结果都已经体现在 _cooked_buf 中。
            prepare_console_read_completion(m);
            comp_before = m.complete;
            comp_ptr = &comp_before;
        }

        _pending.clear();
        _read_total = 0;

        history_push();

        // completion 后所有行编辑临时状态失效；下一次 ReadConsole 重新从当前
        // cursor 和 prompt 边界建立编辑上下文。
        _cooked_buf.clear();
        _cooked_cursor = 0;
        _input_raw_buf.clear();
        _history.reset_browse();

        if (_terminal.cursor_valid())
        {
            // Console 状态 cursor 追随本地 echo 后的终端位置，保证后续
            // GetConsoleScreenBufferInfo/WriteConsole 使用同一坐标。
            const auto cursor = _terminal.cursor();
            cstate.cursor.position = cursor;
            LOG3("[bridge] complete_pending: synced state cursor to (%d,%d)", cursor.X, cursor.Y);
        }

        LOG3("[bridge] complete_pending: done kind=%d cooked_len=%zu vt_eof=%d", static_cast<int>(_pending.kind()),
             _cooked_buf.size(), _pending.vt_eof());

        if (comp_ptr && _io.can_complete())
        {
            // 挂起请求已经从原始 READ_IO 返回 false，必须额外发送
            // CD_IO_COMPLETE；同步完成的请求则由 io_loop 直接带回。
            LOG3("[bridge] complete_pending: sending CD_IO_COMPLETE");
            _io.complete(*comp_ptr);
        }
    }

    // 完成 pending GetConsoleInput。它按原请求的 PEEK/READ 标志重新从
    // input_buffer 取记录，构造 CD_IO_COMPLETE 后清除 pending。
    void complete_pending_console_input()
    {
        if (_pending.kind() != PendingKind::ConsoleInput || !_pending.console_input())
            return;

        // ConsoleInput pending 只等待 input_buffer 出现记录。completion 使用
        // 当前请求的 PEEK/READ 标志重新取数，避免 pending 期间写入多条记录时
        // 只返回触发唤醒的那一条。
        auto &m = *_pending.console_input();
        auto *req = reinterpret_cast<CONSOLE_GETCONSOLEINPUT_MSG *>(m.body + sizeof(CONSOLE_MSG_HEADER));
        const auto max_count = console_input_max_records(m);
        if (max_count == 0)
        {
            _console_input_output_buffer.resize(sizeof(CONSOLE_GETCONSOLEINPUT_MSG));
            auto *out_req = reinterpret_cast<CONSOLE_GETCONSOLEINPUT_MSG *>(_console_input_output_buffer.data());
            *out_req = *req;
            out_req->NumRecords = 0;
            miniio::prepare_completion(m, 0, sizeof(CONSOLE_GETCONSOLEINPUT_MSG));
            m.complete.Write.Data = _console_input_output_buffer.data();
            m.complete.Write.Size = sizeof(CONSOLE_GETCONSOLEINPUT_MSG);

            auto comp = m.complete;
            _pending.clear();

            if (_io.can_complete())
                _io.complete(comp);
            return;
        }

        prepare_console_input_completion(m, req, (req->Flags & CONSOLE_READ_NOREMOVE) != 0, max_count);

        auto comp = m.complete;
        _pending.clear();

        if (_io.can_complete())
            _io.complete(comp);
    }

    friend class pipe_bridge_testable;

  public:
    // Console API ExpungeConsoleCommandHistory 调用：清空 bridge 维护的当前
    // 会话命令历史，不影响 console_state 中的历史配置。
    void api_clear_history()
    {
        _history.clear();
    }

    // Console API SetNumberOfConsoleCommands 调用：调整实际命令历史容量，并按
    // 新容量裁剪已保存命令。
    void api_set_history_capacity(size_t max_commands)
    {
        _history.set_capacity(max_commands);
    }

    // 返回历史命令只读视图，供 L3 GetConsoleCommandHistory 和测试导出。
    const std::vector<std::u32string> &history_commands() const noexcept
    {
        return _history.commands();
    }
};

} // namespace conpty
