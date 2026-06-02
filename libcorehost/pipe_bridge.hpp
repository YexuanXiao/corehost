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
#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include <string_view>
#include <vector>
#include "win32/handle.hpp"
#include "miniio/io_thread.hpp"
#include "os/Console/ntcon.h"
#include "os/Console/conmsgl1.h"
#include "os/Console/conmsgl2.h"
#include "conpty_vt_parser.hpp"
#include "conpty_vt_input_engine.hpp"
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
    input_buffer &inp;
    console_state &cstate;
    screen_buffer &sbuf;
    screen_buffer *_active_screen = nullptr;

    pipe_bridge(input_buffer &input, console_state &state, screen_buffer &screen) noexcept
        : inp(input), cstate(state), sbuf(screen)
    {
        _active_screen = &screen;
        _history.set_capacity(state.history_buffer_size);
    }

    void set_active_screen_buffer(screen_buffer &screen) noexcept
    {
        _active_screen = &screen;
    }

    screen_buffer &active_screen_buffer() const noexcept
    {
        return *_active_screen;
    }

    void set_vt_output(win32::handle_view output) noexcept
    {
        _vt_output.set_output(output);
    }

    void set_server(win32::handle_view server) noexcept
    {
        _io.set_server(server);
    }

    void set_vt_input(win32::handle_view input) noexcept
    {
        _io.set_vt_input(input);
    }

    void set_process_list(std::span<const DWORD> processes) noexcept
    {
        _processes.assign(processes);
    }

    size_t process_count() const noexcept
    {
        return _processes.count();
    }

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
    utf8_stream_decoder _utf8_decoder;

    // ── 原始字节缓冲 ──
    // _read_total 是 _readbuf 中有效字节数。
    std::array<char8_t, sizeof(miniio::io_msg::body)> _readbuf{};
    raw_u8_buffer _input_payload_buffer;
    DWORD _read_total = 0;
    bool _line_found = false; // process_input 发现行终止符 → 跳过 scan_for_line 重复扫描
    bizwen::deque<char8_t> _queued_vt_input;

    pipe_bridge_io _io;
    vt_output_buffer _vt_output;
    conversion_buffers _conversion;
    process_list_snapshot _processes;
    terminal_cursor_state _terminal;
    pending_io_state _pending;
    command_history_state _history;

    size_t console_input_max_records(const miniio::io_msg &msg) const noexcept
    {
        // OutputSize 不含 CONSOLE_MSG_HEADER；记录数组前还要扣掉 L1 描述符。
        const auto output_buffer = msg.descriptor.OutputSize > sizeof(CONSOLE_GETCONSOLEINPUT_MSG)
                                       ? msg.descriptor.OutputSize - sizeof(CONSOLE_GETCONSOLEINPUT_MSG)
                                       : 0;
        const auto requested = output_buffer / sizeof(INPUT_RECORD);
        const auto local_capacity =
            (sizeof(msg.body) - sizeof(CONSOLE_MSG_HEADER) - sizeof(CONSOLE_GETCONSOLEINPUT_MSG)) /
            sizeof(INPUT_RECORD);
        return std::min<size_t>(requested, local_capacity);
    }

    size_t raw_read_capacity(const miniio::io_msg &msg) const noexcept
    {
        // RAW_READ completion 只写客户端缓冲区字节，不附加 API 描述符。
        return std::min<size_t>(msg.descriptor.OutputSize, sizeof(msg.body));
    }

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
    bool has_pending() const noexcept
    {
        return _pending.has_pending();
    }
    bool should_exit() const noexcept
    {
        return _pending.vt_eof() && !_pending.has_pending();
    }

    void set_signal_shutdown_event(win32::handle_view event) noexcept
    {
        _io.set_shutdown_event(event);
    }

    [[nodiscard]] bool is_signal_shutdown_signaled() const noexcept
    {
        return _io.shutdown_signaled();
    }

    [[nodiscard]] bool peek_vt_input(DWORD &avail) noexcept
    {
        return _io.peek_available(avail);
    }

    enum class vt_read_status
    {
        bytes,
        empty,
        eof,
        full,
    };

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

    void process_new_vt_input(DWORD old_total)
    {
        // old_total 是读取前的有效字节数。_readbuf 中旧字节已经被解析过，
        // 但可能仍要保留给 RawRead/ReadConsole completion 返回。
        _line_found = false;
        process_input(_readbuf.data() + old_total, _read_total - old_total);
        vt_flush();
    }

    void complete_pending_with_eof()
    {
        // EOF 必须先记录到 pending 状态，再走正常 completion。这样 RawRead 和
        // ReadConsole 共享同一套“返回 0 字节/0 记录”的完成语义。
        _pending.set_vt_eof(true);
        complete_pending();
    }

    [[nodiscard]] bool wait_for_signal_shutdown_slice()
    {
        // shutdown event 只代表关闭/轮询时间片；VT 输入不会唤醒它。
        return _io.wait_shutdown_slice(pending_vt_input_wait_ms);
    }

    // ── 持久转换缓冲区访问器 ──
    raw_u32_buffer &conv_u32() noexcept
    {
        return _conversion.u32();
    }
    raw_wide_buffer &conv_wstr() noexcept
    {
        return _conversion.wide();
    }
    std::span<const BYTE> read_input_payload(const miniio::io_msg &msg, size_t offset)
    {
        if (offset >= msg.descriptor.InputSize)
            return {};

        const auto size = static_cast<size_t>(msg.descriptor.InputSize) - offset;
        if (offset <= sizeof(msg.body) && size <= sizeof(msg.body) - offset)
            return {msg.body + offset, size};

        auto &buffer = _input_payload_buffer;
        buffer.resize(size);
        _io.read_input(msg.descriptor.Identifier, static_cast<ULONG>(offset),
                       std::span{buffer.data(), buffer.size()});
        return {reinterpret_cast<const BYTE *>(buffer.data()), buffer.size()};
    }

    vt_parser &output_parser() noexcept
    {
        return _output_parser;
    }

    // ── Enter 后换行标志: api_write_console 在输出"hello"等文本前检测,
    //     若为 true 则先发 CUP 到保存的换行目标并清标志 ──
    bool consume_enter_newline()
    {
        // Enter 的本地回显已经把终端推进到下一行，但应用随后的输出通常
        // 仍从 Console 光标位置开始。这里把“下一次输出前先 CUP”的一次性
        // 动作交给 api_write_console 消费，避免空行被重复插入。
        const auto dest = _terminal.enter_dest();
        if (!_terminal.consume_enter_newline())
        {
            LOG("[bridge] consume_enter: false");
            return false;
        }
        LOG("[bridge] consume_enter: TRUE, dest=(%d,%d)", dest.X, dest.Y);
        return true;
    }
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

    // ── inherit_cursor: 发送 DSR CPR 前设置，cpr_response 处理时清除 ──
    void set_pending_inherit_cursor() noexcept
    {
        _terminal.set_pending_inherit_cursor();
    }

    // WriteConsole 完成后调用，同步终端光标并重置输入边界
    void sync_cursor_after_write(COORD pos)
    {
        // Console 输出是新的行编辑边界：后续 ReadConsole 的左右移动不得越过
        // 本次输出结束位置，否则用户可以删到 shell prompt 或上一条输出。
        const auto terminal_pos = active_screen_buffer().viewport.relative_position(pos);
        const auto old_cursor = _terminal.cursor();
        LOG("[bridge] sync_cursor_after_write: pos=(%d,%d) was_tc=(%d,%d) was_col_start=%d was_col_end=%d enter_nl=%d",
            pos.X, pos.Y, old_cursor.X, old_cursor.Y, _terminal.input_column_start(), _terminal.input_column_end(),
            _terminal.enter_newline_pending());
        term_cursor_set(terminal_pos);
        bounds_reset(terminal_pos.X);
    }

    // ════════════════════════════════════════════════════
    //  VT 输出器 (无 snprintf, 直接构建 char 缓冲)
    // ════════════════════════════════════════════════════

    // flush: 写入管道并清空缓冲
    void vt_flush()
    {
        _vt_output.flush();
    }

    [[nodiscard]] bool has_buffered_vt_output() const noexcept
    {
        return _vt_output.buffered_size() != 0;
    }

    [[nodiscard]] bool should_flush_vt_output() const noexcept
    {
        return _vt_output.should_flush();
    }

    // ── 缓冲追加方法 ──

    // 追加字面字符串
    void vt_append_str(std::string_view s)
    {
        _vt_output.append(s);
    }

    // 追加单字符
    void vt_append_char(char c)
    {
        _vt_output.append(c);
    }

    // 追加整数 (无 snprintf, 自写 itoa)
    void vt_append_int(int n)
    {
        _vt_output.append_int(n);
    }

    void vt_append_sgr_param(bool &first, int value)
    {
        if (!first)
            vt_append_char(';');
        first = false;
        vt_append_int(value);
    }

    void vt_append_hex_byte(uint8_t value)
    {
        constexpr char hex_digits[] = "0123456789abcdef";
        vt_append_char(hex_digits[value >> 4]);
        vt_append_char(hex_digits[value & 15]);
    }

    void vt_append_raw_sequence(std::u32string_view text)
    {
        COREHOST_PERF_SCOPE_AMOUNT(vt_raw_passthrough, text.size());
        _vt_output.append_utf32(text);
    }

    // ── 高层 VT 序列 ──

    void vt_write_cup(SHORT row, SHORT col)
    {
        // 内部坐标沿用 Console 的 0-based COORD；VT CUP 参数是 1-based。
        vt_append_str("\x1b["sv);
        vt_append_int(static_cast<int>(row) + 1);
        vt_append_char(';');
        vt_append_int(static_cast<int>(col) + 1);
        vt_append_char('H');
    }

    void vt_write_cup_buffer(COORD buffer_position)
    {
        const auto terminal_position = active_screen_buffer().viewport.clamped_relative_position(buffer_position);
        vt_write_cup(terminal_position.Y, terminal_position.X);
    }

    [[nodiscard]] bool terminal_cursor_matches_buffer(COORD buffer_position) const noexcept
    {
        if (!_terminal.cursor_valid())
            return false;
        const auto terminal_position = active_screen_buffer().viewport.clamped_relative_position(buffer_position);
        const auto cursor = _terminal.cursor();
        return cursor.X == terminal_position.X && cursor.Y == terminal_position.Y;
    }

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

    // vt_write_cell: char32_t → UTF-8 追加
    void vt_write_cell(char32_t ch)
    {
        // screen_buffer 用 0 表示空单元格。终端没有“空字符”，重绘时用空格
        // 清除对应列，避免旧字形残留。
        if (ch == 0)
            ch = U' ';
        _vt_output.append_cell(ch);
    }

    void vt_write_clear_screen()
    {
        vt_append_str("\x1b[2J\x1b[H"sv);
    }

    void vt_write_dsr_cpr()
    {
        vt_append_str("\x1b[6n"sv);
    }

    // vt_write_window_title: OSC 0/2 设置终端标题
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

    void vt_write_text(std::u32string_view text)
    {
        COREHOST_PERF_SCOPE_AMOUNT(vt_msg_send_text, text.size());
        _vt_output.append_utf32(text);
    }

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
            vt_write_cup(static_cast<SHORT>(msg.payload.position.row - 1), static_cast<SHORT>(msg.payload.position.col - 1));
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
        LOG("[bridge] handle_console_read: vt_eof=%d proc_z=%d init_bytes=%lu", _pending.vt_eof(), proc_z, init_bytes);
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

        _pending.begin_console_read(msg, req->Unicode != 0, proc_z);
        _read_total = 0;
        _line_found = false;
        _cooked_buf.clear();
        _cooked_cursor = 0;
        _history.reset_browse();
        LOG("[bridge] handle_console_read: pending ConsoleRead, unicode=%d", _pending.unicode());

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
            LOG("[bridge] handle_console_read: seeded %lu init bytes, cooked=%zu", init_bytes, _cooked_buf.size());
        }

        if (consume_input_buffer_for_console_read())
        {
            LOG("[bridge] handle_console_read: completed from input_buffer");
            return true;
        }

        if (accumulate_from_pipe())
        {
            LOG("[bridge] handle_console_read: sync complete");
            return true;
        }
        LOG("[bridge] handle_console_read: pending, returning false");
        return false;
    }

    bool handle_console_input(miniio::io_msg &msg)
    {
        // GetConsoleInput 直接服务于 input_buffer。PEEK 不消费记录；NOWAIT
        // 在没有记录时必须同步返回 0，而不是挂起。
        prepare_console_input_events();

        auto *req = reinterpret_cast<CONSOLE_GETCONSOLEINPUT_MSG *>(msg.body + sizeof(CONSOLE_MSG_HEADER));
        auto *out = reinterpret_cast<INPUT_RECORD *>(msg.body + sizeof(CONSOLE_MSG_HEADER) +
                                                     sizeof(CONSOLE_GETCONSOLEINPUT_MSG));
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
        const auto count = peek ? inp.peek(out, max_count) : inp.read(out, max_count);

        if (count == 0 && wait_allowed)
        {
            // 只有“可等待且当前无记录”的请求进入 pending；后续 emit_key 会
            // 调 complete_pending_console_input 唤醒它。
            _pending.begin_console_input(msg);
            return false;
        }

        req->NumRecords = static_cast<ULONG>(count);
        const auto size = static_cast<ULONG>(sizeof(CONSOLE_GETCONSOLEINPUT_MSG) + count * sizeof(INPUT_RECORD));
        miniio::prepare_completion(msg, 0, size);
        msg.complete.Write.Data = msg.body + sizeof(CONSOLE_MSG_HEADER);
        msg.complete.Write.Size = size;
        return true;
    }

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
    void on_idle()
    {
        vt_flush();

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
                LOG("[bridge] on_idle: signal shutdown event set, marking EOF");
                _pending.set_vt_eof(true);
                if (_pending.has_pending())
                    complete_pending();
                return;
            }
            return;
        }

        LOG("[bridge] on_idle: avail=%lu kind=%d total=%lu", avail, static_cast<int>(_pending.kind()), _read_total);
        if (!_pending.has_pending())
        {
            queue_available_vt_input();
            return;
        }

        if (accumulate_from_pipe())
            return;
    }

    void cancel_pending_read()
    {
        // DISCONNECT 或外部关闭需要释放当前 ConDrv 请求；completion 的具体
        // 形态仍由 complete_pending 根据 pending 类型决定。
        if (_pending.has_pending())
            complete_pending();
    }

    // ── raw_write — 公共, 供 api_write_console 使用 ──
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
            if (cp == code_page_gbk)
            {
                _vt_output.append_gbk(std::string_view{s, bytes});
                return;
            }

            auto &wide = _conversion.wide();
            convert_ansi_to_wstr(s, bytes, cp, wide);
            _vt_output.append_utf16(wide_view(wide));
        }
    }

    // ════════════════════════════════════════════════════
    //  Layer 1 — 独立状态操作 (每个函数只改一个状态域)
    // ════════════════════════════════════════════════════

    // ── 编辑缓冲 (_cooked_buf, _cooked_cursor) ──
    void cooked_append(char32_t ch)
    {
        // _cooked_cursor 是插入点而不是字符索引缓存；插入后移动到新字符后方。
        _cooked_buf.insert(_cooked_cursor, 1, ch);
        ++_cooked_cursor;
    }
    void cooked_pop_before()
    {
        if (_cooked_cursor == 0)
            return;
        _cooked_buf.erase(--_cooked_cursor, 1);
    }
    void cooked_pop_at()
    {
        if (_cooked_cursor >= _cooked_buf.size())
            return;
        _cooked_buf.erase(_cooked_cursor, 1);
    }
    void cooked_clear()
    {
        _cooked_buf.clear();
        _cooked_cursor = 0;
    }
    void cooked_set_pos(size_t p)
    {
        _cooked_cursor = p;
    }
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
    void term_cursor_set(COORD c)
    {
        _terminal.set_cursor(c);
    }
    void term_cursor_advance()
    {
        _terminal.advance();
    }
    void term_cursor_retreat()
    {
        _terminal.retreat();
    }
    SHORT term_cursor_col() const noexcept
    {
        return _terminal.column_for_offset(_cooked_cursor);
    };

    void bounds_reset(SHORT x)
    {
        _terminal.reset_bounds(x);
    }
    void bounds_extend()
    {
        _terminal.extend_bounds();
    }
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
    void cup_to(SHORT row, SHORT col)
    {
        LOG("[vt] cup_to(%d,%d)", row, col);
        vt_write_cup(row, col);
        vt_flush();
    }
    void repaint_suffix()
    {
        // 光标位于行中间插入/删除时，只重绘光标后的后缀。调用者随后会
        // CUP 回编辑光标，避免把用户光标留在行尾。
        auto sf = std::u32string_view(_cooked_buf).substr(_cooked_cursor);
        for (char32_t cp : sf)
            vt_write_cell(cp);
    }
    void repaint_full_line()
    {
        // 历史导航替换整行时，从输入起始列清到行尾再写新内容。这里不清理
        // prompt 左侧内容，因为输入起始列是行编辑的左边界。
        if (!_terminal.cursor_valid())
        {
            LOG("[history] repaint_full_line: SKIP tc_valid=0");
            return;
        }
        const auto cursor = _terminal.cursor();
        LOG("[history] repaint_full_line: cup_to(%d,%d) cooked_sz=%zu", cursor.Y, _terminal.input_column_start(),
            _cooked_buf.size());
        cup_to(cursor.Y, _terminal.input_column_start());
        vt_append_str("\x1b[K"sv);
        for (char32_t cp : _cooked_buf)
            vt_write_cell(cp);
        vt_flush();
        _terminal.set_cursor_x(_terminal.input_column_end());
    }
    void load_history_line()
    {
        // 加载后光标移动到行尾，和 Windows 控制台历史浏览行为一致。
        cooked_set_pos(_cooked_buf.size());
        _terminal.set_bounds_end_for_length(_cooked_buf.size());
        const auto cursor = _terminal.cursor();
        LOG("[history] load_history_line: tc=(%d,%d) col_start=%d col_end=%d cooked_sz=%zu", cursor.X, cursor.Y,
            _terminal.input_column_start(), _terminal.input_column_end(), _cooked_buf.size());
        repaint_full_line();
        const auto done_cursor = _terminal.cursor();
        LOG("[history] load_history_line: done tc=(%d,%d)", done_cursor.X, done_cursor.Y);
    }

    // ── ConsoleRead 路径: 行编辑 ──

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
    void edit_insert_codepoint(char32_t ch)
    {
        LOG(L"[in] EDIT_CP ch=U+%04X cooked_sz=%zu", (unsigned)ch, _cooked_buf.size());
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
    void edit_home()
    {
        cooked_set_pos(0);
        if (_terminal.cursor_valid())
        {
            cup_to(_terminal.cursor().Y, _terminal.input_column_start());
            _terminal.set_cursor_x(_terminal.input_column_start());
        }
    }
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
    void history_push()
    {
        _history.push(_cooked_buf);
    }
    void history_break_browse()
    {
        _history.break_browse();
    }
    void history_up()
    {
        const auto cursor = _terminal.cursor();
        LOG("[history] history_up: tc=(%d,%d) col_start=%d col_end=%d history_sz=%zu idx=%zu", cursor.X, cursor.Y,
            _terminal.input_column_start(), _terminal.input_column_end(), _history.size(), _history.browse_index());
        if (!_history.browse_up(_cooked_buf, _cooked_buf))
        {
            LOG("[history] history_up: empty, return");
            return;
        }
        LOG("[history] history_up: loading idx=%zu cooked_sz=%zu", _history.browse_index(), _cooked_buf.size());
        load_history_line();
    }
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
        size_t we = 0;
        while (we < _cooked_buf.size() && _cooked_buf[we] != U' ')
            ++we;
        std::wstring wk;
        wk.reserve(we);
        for (size_t i = 0; i < we; ++i)
            wk.push_back(static_cast<wchar_t>(_cooked_buf[i]));
        auto it = cstate.aliases.find(wk);
        if (it == cstate.aliases.end())
        {
            LOG("[bridge] alias not found");
            return;
        }
        std::u32string ex;
        ex.reserve(it->second.size() + _cooked_buf.size() - we);
        for (wchar_t wc : it->second)
            ex.push_back(static_cast<char32_t>(wc));
        if (we < _cooked_buf.size())
            ex.append(_cooked_buf.substr(we));
        _cooked_buf = std::move(ex);
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
    void input_printable(char32_t ch, BYTE raw)
    {
        WORD vk = ascii_to_vk(static_cast<WCHAR>(ch));
        LOG(L"[in] PRINTABLE ch=U+%04X vk=0x%X uc=0x%X", (unsigned)ch, vk, (unsigned)(WCHAR)ch);
        // 非 ConsoleRead 模式下应用从 GetConsoleInput 获取按键事件；同时维护
        // cooked_buf 只用于本层对 Enter/历史等兼容行为的内部判断。
        emit_key_pair(vk, static_cast<WCHAR>(ch));
        if (ch != U'\r')
            cooked_append(ch);
        else
            cooked_clear();
    }
    void input_enter()
    {
        LOG("[bridge] input ENTER");
        // PSReadLine 只需要 KEY_DOWN Enter 即可触发提交。这里不补 KEY_UP，
        // 避免某些 shell 把一组 Enter 解释为两次输入状态变化。
        emit_key(VK_RETURN, L'\r'); // D only — PSReadLine accepts Enter on KEY_DOWN
    }

    // ════════════════════════════════════════════════════
    //  兼容层 (process_input / 测试 / 旧调用者 使用)
    // ════════════════════════════════════════════════════
    void _edit_insert(char32_t ch, BYTE raw)
    {
        edit_insert_char(ch, raw);
    }
    void _edit_backspace()
    {
        edit_backspace();
    }
    void _edit_delete()
    {
        edit_delete();
    }
    void _edit_move_left()
    {
        edit_move_left();
    }
    void _edit_move_right()
    {
        edit_move_right();
    }
    void _edit_home()
    {
        edit_home();
    }
    void _edit_end()
    {
        edit_end();
    }
    void _edit_history_up()
    {
        const auto cursor = _terminal.cursor();
        LOG("[history] _edit_history_up: tc=(%d,%d) col_start=%d col_end=%d cook_sz=%zu cook_pos=%zu", cursor.X,
            cursor.Y, _terminal.input_column_start(), _terminal.input_column_end(), _cooked_buf.size(), _cooked_cursor);
        history_up();
    }
    void _edit_history_down()
    {
        history_down();
    }
    void _expand_alias()
    {
        expand_alias();
    }
    void _write_char_key_event(char32_t ch, BYTE raw)
    {
        LOG(L"[in] WRITE_KEY ch=U+%04X raw=0x%02X", (unsigned)ch, raw);
        input_printable(ch, raw);
    }
    void _write_enter_key_event()
    {
        input_enter();
    }
    void _write_key_event_pair(const INPUT_RECORD &t)
    {
        emit_key_pair(t.Event.KeyEvent.wVirtualKeyCode, t.Event.KeyEvent.uChar.UnicodeChar);
    }

  private:
    // ════════════════════════════════════════════════════
    //  内部管道
    // ════════════════════════════════════════════════════

    bool consume_queued_vt_input(DWORD room)
    {
        if (_queued_vt_input.empty())
            return false;

        const auto count = std::min<size_t>(room, _queued_vt_input.size());
        for (size_t i = 0; i < count; ++i)
        {
            _readbuf[_read_total + i] = _queued_vt_input.front();
            _queued_vt_input.pop_front();
        }
        _read_total += static_cast<DWORD>(count);
        LOG("[bridge] consume_queued_vt_input: consumed=%zu remaining=%zu", count, _queued_vt_input.size());
        return true;
    }

    bool queue_available_vt_input()
    {
        DWORD read = 0;
        auto result = _io.read_available(std::span{_readbuf}, read);
        if (result == vt_pipe_read_status::bytes)
        {
            _queued_vt_input.insert(_queued_vt_input.end(), _readbuf.data(), _readbuf.data() + read);
            LOG("[bridge] queue_available_vt_input: read=%lu total=%zu", read, _queued_vt_input.size());
            return true;
        }

        if (result == vt_pipe_read_status::eof)
            _pending.set_vt_eof(true);
        return false;
    }

    void queue_unprocessed_vt_input(const char8_t *bytes, DWORD consumed, DWORD len)
    {
        if (consumed >= len)
            return;

        const auto tail_size = static_cast<size_t>(len - consumed);
        for (auto it = bytes + len; it != bytes + consumed;)
            _queued_vt_input.push_front(*--it);

        const auto readbuf_begin = reinterpret_cast<std::uintptr_t>(_readbuf.data());
        const auto readbuf_end = readbuf_begin + _readbuf.size();
        const auto batch_begin = reinterpret_cast<std::uintptr_t>(bytes);
        if (batch_begin >= readbuf_begin && batch_begin <= readbuf_end)
        {
            const auto absolute_consumed = static_cast<DWORD>((batch_begin - readbuf_begin) + consumed);
            if (absolute_consumed < _read_total)
                _read_total = absolute_consumed;
        }

        LOG("[bridge] queue_unprocessed_vt_input: queued_tail=%zu total=%zu", tail_size, _queued_vt_input.size());
    }

    // ── _echo_byte: 向终端输出单个字节并跟踪光标（经 VT 缓冲批量写入）──
    void _echo_byte(char8_t b)
    {
        // should_echo_last 只用于控制字符/ESC 路径。普通文本由 edit_* 处理，
        // 否则同一字节会同时进入 echo 和 cooked line，造成重复显示。
        vt_append_char(static_cast<char>(b));
        _terminal.apply_echo_byte(static_cast<BYTE>(b));
    }

    bool raw_read_echo_enabled() const noexcept
    {
        return _pending.kind() == PendingKind::RawRead && (cstate.input_mode & ENABLE_ECHO_INPUT) != 0;
    }

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
                LOG("[bridge] accumulate: buffer full");
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

            // ── 单遍处理：process_input 内部检测 \r/\n/Ctrl+Z，不再需要二次 scan_for_line ──
            _line_found = false;
            process_input(_readbuf.data() + old_total, _read_total - old_total);
            // 批量 echo 后必须在本批输入结束时刷新，否则普通打字会滞留在 VT 输出缓冲，
            // 直到后续控制序列/应用输出/缓冲满才显示，表现为终端输入卡顿。
            vt_flush();

            if (_line_found)
                return true;
        }
    }

    bool process_input_win32_key(PendingKind pending_kind, DWORD i, DWORD len, const char8_t *bytes)
    {
        auto &m = _input_parser.get();
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
            LOG("[bridge] ENTER_Win32Input was_tc=(%d,%d)", old_cursor.X, old_cursor.Y);
            _line_found = true;
            _terminal.crlf();
            _terminal.mark_enter_newline_at_cursor();
            vt_append_str("\r\n"sv);
            const auto cursor = _terminal.cursor();
            LOG("[bridge] ENTER_Win32Input done tc=(%d,%d)", cursor.X, cursor.Y);
        }
        else if (pending_kind != PendingKind::RawRead && m.payload.win32_key.key_down)
        {
            const auto cursor = _terminal.cursor();
            LOG("[bridge] Win32Input write_input: vk=%d uc=0x%04X cs=0x%X tc=(%d,%d)", m.payload.win32_key.vk, m.payload.win32_key.uc,
                m.payload.win32_key.control_state, cursor.X, cursor.Y);
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

    template <vt_message_id key_id>
    void process_input_key_event(const vt_message &msg)
    {
        INPUT_RECORD rec;
        if (_engine.convert(key_id, msg, rec))
            _write_key_event_pair(rec);
    }

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

    void process_input_cpr_response()
    {
        auto &m = _input_parser.get();
        if (_terminal.pending_inherit_cursor() && m.payload.cpr.row > 0 && m.payload.cpr.col > 0)
        {
            const COORD terminal_position{static_cast<SHORT>(m.payload.cpr.col - 1), static_cast<SHORT>(m.payload.cpr.row - 1)};
            cstate.cursor.position = active_screen_buffer().viewport.absolute_position(terminal_position);
            cstate.clamp_cursor_to_buffer();
            _terminal.finish_inherit_cursor(terminal_position);
            LOG("[bridge] cpr_response: inherit cursor (%d,%d)", cstate.cursor.position.X, cstate.cursor.position.Y);
        }
    }

    void process_input_tab()
    {
        INPUT_RECORD ir{};
        ir.EventType = KEY_EVENT;
        ir.Event.KeyEvent.wRepeatCount = 1;
        ir.Event.KeyEvent.wVirtualKeyCode = VK_TAB;
        ir.Event.KeyEvent.uChar.UnicodeChar = L'\t';
        _write_key_event_pair(ir);
    }

    void process_input_resize_window(const vt_message &msg)
    {
        COORD new_size{msg.payload.resize.cols, msg.payload.resize.rows};
        if (new_size.X <= 0 || new_size.Y <= 0)
            return;

        LOG("[bridge] resize_window: old=(%d,%d) new=(%d,%d)", cstate.screen_buffer_size.X, cstate.screen_buffer_size.Y,
            new_size.X, new_size.Y);
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
        for (SHORT y = 0; y < new_size.Y; ++y)
        {
            vt_write_cup(y, 0);
            for (SHORT x = 0; x < new_size.X; ++x)
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

    void process_input_text(PendingKind pending_kind)
    {
        auto &tm = _input_parser.get();
        LOG(L"[in] TEXT_MSG len=%zu", tm.payload.text.size());
        for (char32_t tc : tm.payload.text)
        {
            if (tc <= 0x1F || tc == 0x7F)
                continue;
            LOG(L"[in] TEXT_DISP ch=U+%04X", (unsigned)tc);
            if (pending_kind == PendingKind::ConsoleRead)
                edit_insert_codepoint(tc);
            else
                _write_char_key_event(tc, static_cast<BYTE>(tc & 0xFF));
        }
    }

    // ── process_input: 解码 → 解析 → echo → 分发 ──
    // 内部检测 \r/\n/Ctrl+Z 并设置 _line_found，消除 scan_for_line 的二次扫描
    void process_input(const char8_t *bytes, DWORD len)
    {
        if (len > 0)
            LOG_HEX("input", bytes, len);
        // _utf8_decoder 是流式状态机；多字节序列跨 ReadFile 边界时，前几次
        // 调用会产生 continuation，直到完整 codepoint 才交给 VT parser。
        for (DWORD i = 0; i < len; ++i)
        {
            char8_t b = bytes[i];
            echo_raw_read_text_byte(b);

            // ── Ctrl+Z 即时检测 ──
            if (_pending.process_control_z() && b == static_cast<char8_t>(0x1A)) [[unlikely]]
            {
                LOG("[bridge] process_input: Ctrl+Z at offset %lu", i);
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
            vt_message_id id = _input_parser.parse(ch);

            if (id == vt_message_id::continue_text) [[likely]]
            {
                // parser 把普通地面态文本累积在 msg.payload.text，但交互输入需要逐字符
                // 响应编辑键和回显，所以这里立即消费并重置文本累积。
                const auto pending_kind = _pending.kind();
                LOG(L"[in] TEXT ch=U+%04X raw=0x%02X kind=%d", (unsigned)ch, static_cast<unsigned>(b),
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
            LOG(L"[in] MSG id=%d echo=%d kind=%d", (int)id, (int)_input_parser.should_echo_last(), (int)pending_kind);
            if (_input_parser.should_echo_last() && pending_kind == PendingKind::ConsoleRead)
                _echo_byte(b);

            auto &msg = _input_parser.get();
            switch (id)
            {
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
            case vt_message_id::win32_input_key: {
                if (process_input_win32_key(pending_kind, i, len, bytes))
                {
                    _input_parser.reset<vt_message_id::win32_input_key>();
                    return;
                }
                _input_parser.reset<vt_message_id::win32_input_key>();
                break;
            }
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
                process_input_cpr_response();
                _input_parser.reset<vt_message_id::cpr_response>();
                break;
            }
            case vt_message_id::cursor_forward_tab: {
                process_input_tab();
                _input_parser.reset<vt_message_id::cursor_forward_tab>();
                break;
            }
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
                process_input_text(pending_kind);
                _input_parser.reset<vt_message_id::text>();
                break;
            }
            case vt_message_id::unknown_sequence: {
                _input_parser.reset<vt_message_id::unknown_sequence>();
                break;
            }
            default: {
                _input_parser.reset<vt_message_id::continue_>();
                break;
            }
            }
        }
    }
    void _on_line_terminator(bool is_cr, DWORD i, DWORD len, const char8_t *bytes)
    {
        // 行终止符处理只消费当前行；i/len/bytes 描述当前 process_input 批次，
        // 用于识别 CRLF 是否跨 ReadFile 边界。
        _line_found = true;
        auto consumed = i + 1;

        if (is_cr)
        {
            bool has_lf = false;
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

            if (!has_lf && !raw_read_echo_enabled())
                vt_append_char('\n');
        }

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
            LOG("[bridge] LINE_TERM enter_nl=1 dest=(%d,%d)", dest.X, dest.Y);
        }

        LOG(L"[in] LINE_TERM cooked=[%.*ls]", static_cast<int>(_cooked_buf.size() < 200 ? _cooked_buf.size() : 200),
            _cooked_buf.data());
        complete_pending();
    }

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
        if (_pending.unicode())
        {
            auto *utf16_out = reinterpret_cast<wchar_t *>(db);
            auto max_chars = maxd / sizeof(wchar_t);
            size_t n = convert_u32_to_wide_raw(_cooked_buf, utf16_out, max_chars);
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
            size_t written =
                convert_u32_to_ansi_raw(_cooked_buf, cstate.input_code_page, ansi_out, maxd, _conversion.wide());
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
        LOG(L"[bridge] complete_pending: kind=%d total=%lu cooked_len=%zu vt_eof=%d cooked=[%.*ls]",
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
            LOG("[bridge] complete_pending: synced state cursor to (%d,%d)", cursor.X, cursor.Y);
        }

        LOG("[bridge] complete_pending: done kind=%d cooked_len=%zu vt_eof=%d", static_cast<int>(_pending.kind()),
            _cooked_buf.size(), _pending.vt_eof());

        if (comp_ptr)
        {
            // 挂起请求已经从原始 READ_IO 返回 false，必须额外发送
            // CD_IO_COMPLETE；同步完成的请求则由 io_loop 直接带回。
            LOG("[bridge] complete_pending: sending CD_IO_COMPLETE");
            _io.complete(*comp_ptr);
        }
    }

    void complete_pending_console_input()
    {
        if (_pending.kind() != PendingKind::ConsoleInput || !_pending.console_input())
            return;

        // ConsoleInput pending 只等待 input_buffer 出现记录。completion 使用
        // 当前请求的 PEEK/READ 标志重新取数，避免 pending 期间写入多条记录时
        // 只返回触发唤醒的那一条。
        auto &m = *_pending.console_input();
        auto *req = reinterpret_cast<CONSOLE_GETCONSOLEINPUT_MSG *>(m.body + sizeof(CONSOLE_MSG_HEADER));
        auto *out =
            reinterpret_cast<INPUT_RECORD *>(m.body + sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_GETCONSOLEINPUT_MSG));
        const auto max_count = console_input_max_records(m);
        if (max_count == 0)
        {
            req->NumRecords = 0;
            miniio::prepare_completion(m, 0, sizeof(CONSOLE_GETCONSOLEINPUT_MSG));
            m.complete.Write.Data = m.body + sizeof(CONSOLE_MSG_HEADER);
            m.complete.Write.Size = sizeof(CONSOLE_GETCONSOLEINPUT_MSG);

            auto comp = m.complete;
            _pending.clear();

            _io.complete(comp);
            return;
        }

        const auto count = (req->Flags & CONSOLE_READ_NOREMOVE) ? inp.peek(out, max_count) : inp.read(out, max_count);
        req->NumRecords = static_cast<ULONG>(count);
        const auto size = static_cast<ULONG>(sizeof(CONSOLE_GETCONSOLEINPUT_MSG) + count * sizeof(INPUT_RECORD));
        miniio::prepare_completion(m, 0, size);
        m.complete.Write.Data = m.body + sizeof(CONSOLE_MSG_HEADER);
        m.complete.Write.Size = size;

        auto comp = m.complete;
        _pending.clear();

        _io.complete(comp);
    }

    friend class pipe_bridge_testable;

  public:
    void api_clear_history()
    {
        _history.clear();
    }

    void api_set_history_capacity(size_t max_commands)
    {
        _history.set_capacity(max_commands);
    }

    const std::vector<std::u32string> &history_commands() const noexcept
    {
        return _history.commands();
    }
};

} // namespace conpty
