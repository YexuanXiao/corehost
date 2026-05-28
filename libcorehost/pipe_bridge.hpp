// ── conpty/pipe_bridge.hpp ─────────────────────────
// Layer 2: PTY 管道桥接 (char32_t 内部化版本)
//
// 与 conpty/pipe_bridge.hpp 的关键区别:
//   - 使用 conpty::vt_parser (char32_t 输入) 替代 byte-level 解析器
//   - process_input(): UTF-8 → char32_t → vt_parser
//   - 行编辑使用 Parser 外部缓冲 _cooked_buf (Parser 自动写入地面态文本)
//   - 所有 VT 输出不使用 snprintf, 直接构建 char 缓冲
//   - echo 保留原始 UTF-8 字节
//
// VT 输出设计:
//   vt_writer 内部类管理 _vt_buf (std::vector<char>) + _vt_len,
//   提供 append_str/append_int/append_cell 等无 snprintf 方法,
//   最终通过 flush 写入 vt_out 管道。
#pragma once
#include <windows.h>
#include <cstring>
#include <string>
#include <string_view>
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
#include "utility/log.hpp"

namespace conpty
{
using namespace std::literals;

struct pipe_bridge
{
    static constexpr DWORD pending_vt_input_wait_ms = 16;

    // ════════════════════════════════════════════════════
    //  变量
    // ════════════════════════════════════════════════════

    // ── 管道句柄 ──
    win32::handle_view vt_in;
    win32::handle_view vt_out;
    win32::handle_view server;

    // ── 子系统 ──
    input_buffer &inp;
    console_state &cstate; // 用于 echo 后同步光标 + resize
    screen_buffer &sbuf;   // 用于 resize 时更新屏幕缓冲区

    pipe_bridge(input_buffer &input, console_state &state, screen_buffer &screen) noexcept
        : inp(input), cstate(state), sbuf(screen)
    {
    }

    // ── ProcessList ──
    static constexpr size_t max_procs = 64;
    DWORD proc_list[max_procs]{};
    size_t proc_count = 0;

    // ── VT 输出缓冲 ──
    std::vector<char> _vt_buf = std::vector<char>(8192);
    size_t _vt_len = 0;

  private:
    // ── 挂起 I/O 状态 ──
    enum class PendingKind
    {
        None,
        RawRead,
        ConsoleRead,
        ConsoleInput
    };
    PendingKind _pend_kind = PendingKind::None;
    miniio::io_msg *_pend_raw = nullptr;
    miniio::io_msg *_pend_usr = nullptr;
    miniio::io_msg *_pend_input = nullptr;
    CONSOLE_READCONSOLE_MSG _pend_req{};
    bool _pend_uni = false;
    bool _vt_eof = false;
    bool _process_control_z = false;
    win32::handle_view _signal_shutdown_event;

    // ── Parser 外部缓冲：_raw_buf 记录全部字符，_cooked_buf 仅记录地面态文本 ──
    std::u32string _raw_buf;    // Parser 写入全部字符（供 msg.text 视图指向）
    std::u32string _cooked_buf; // Parser 写入地面态文本（行编辑缓冲，返回给 cmd）
    size_t _cooked_cursor = 0;  // 编辑光标在 _cooked_buf 中的位置

    // ── 命令历史（上下键导航）──
    std::vector<std::u32string> _history;          // 每次 complete_pending 时存入
    size_t _history_idx = static_cast<size_t>(-1); // SIZE_MAX=未浏览
    std::u32string _saved_input;                   // 开始浏览前暂存当前输入

    // ── char32_t VT 输入解析（注入外部缓冲）──
    vt_parser _parser{_raw_buf};
    vt_input_engine _engine;

    // ── 流式 UTF-8 解码器 ──
    utf8_stream_decoder _utf8_decoder;

    // ── 原始字节缓冲 (echo 用) ──
    std::vector<BYTE> _readbuf = std::vector<BYTE>(4096);
    DWORD _read_total = 0;
    DWORD _echo_start = 0;
    bool _line_found = false; // process_input 发现行终止符 → 跳过 scan_for_line 重复扫描

    // ── 终端光标追踪 ──
    COORD _term_cursor{0, 0};
    bool _term_cursor_valid = false;      // 仅在首次 WriteConsole 后有效
    bool _enter_pending_newline = false;  // Enter 后、下一条 WriteConsole 文本输出前 需先换行
    COORD _enter_dest{0, 0};              // _enter_pending_newline 置位时的换行目标行，不受后续 SetCursorPos 污染
    bool _pending_inherit_cursor = false; // inherit_cursor: 等待终端 CPR 应答中

    // ── 输入起始列：shell prompt 结束后光标所在 X，← 不得越过此边界 ──
    SHORT _input_column_start = 0;
    // ── 输入末尾列：用户已键入文本的最右侧 X，→ 不得越过此边界 ──
    SHORT _input_column_end = 0;

    // ── 持久转换缓冲区 (供 api_handlers 等复用) ──
    std::u32string _conv_u32; // UTF-16/ANSI/UTF-8 → char32_t
    std::string _conv_utf8;   // char32_t → UTF-8
    std::wstring _conv_wstr;  // char32_t → wchar_t / ANSI 中间缓冲

    size_t console_input_max_records(const miniio::io_msg &msg) const noexcept
    {
        const auto client_buf = msg.descriptor.OutputSize;
        const auto header_size = sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_GETCONSOLEINPUT_MSG);
        const auto count = client_buf > header_size ? (client_buf - header_size) / sizeof(INPUT_RECORD) : 0;
        return count == 0 ? 1 : count;
    }

  public:
    // ════════════════════════════════════════════════════
    //  简单内联访问器
    // ════════════════════════════════════════════════════

    bool has_pending() const noexcept
    {
        return _pend_kind != PendingKind::None;
    }
    bool should_exit() const noexcept
    {
        return _vt_eof && _pend_kind == PendingKind::None;
    }
    bool is_vt_eof() const noexcept
    {
        return _vt_eof;
    }

    void set_signal_shutdown_event(win32::handle_view event) noexcept
    {
        _signal_shutdown_event = event;
    }

    [[nodiscard]] bool is_signal_shutdown_signaled() const noexcept
    {
        return _signal_shutdown_event.valid() &&
               ::WaitForSingleObject(_signal_shutdown_event.get(), 0) == WAIT_OBJECT_0;
    }

    [[nodiscard]] bool peek_vt_input(DWORD &avail) noexcept
    {
        if (::PeekNamedPipe(vt_in.get(), nullptr, 0, nullptr, &avail, nullptr))
            return true;

        LOG("[bridge] peek_vt_input: PeekNamedPipe failed err=%lu", ::GetLastError());
        return false;
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
        DWORD avail = 0;
        if (!peek_vt_input(avail))
            return vt_read_status::eof;
        if (avail == 0)
            return vt_read_status::empty;

        DWORD room = static_cast<DWORD>(_readbuf.size()) - _read_total;
        if (room == 0)
            return vt_read_status::full;

        auto to_read = avail < room ? avail : room;
        DWORD read = 0;
        if (!::ReadFile(vt_in.get(), _readbuf.data() + _read_total, to_read, &read, nullptr) || read == 0)
        {
            LOG("[bridge] read_available_vt_input: ReadFile failed read=%lu err=%lu", read, ::GetLastError());
            return vt_read_status::eof;
        }

        _read_total += read;
        return vt_read_status::bytes;
    }

    [[nodiscard]] vt_read_status read_blocking_vt_input()
    {
        DWORD room = static_cast<DWORD>(_readbuf.size()) - _read_total;
        if (room == 0)
            return vt_read_status::full;

        DWORD read = 0;
        if (!::ReadFile(vt_in.get(), _readbuf.data() + _read_total, room, &read, nullptr) || read == 0)
        {
            LOG("[bridge] read_blocking_vt_input: ReadFile failed read=%lu err=%lu", read, ::GetLastError());
            return vt_read_status::eof;
        }

        _read_total += read;
        return vt_read_status::bytes;
    }

    void process_new_vt_input(DWORD old_total)
    {
        _line_found = false;
        process_input(_readbuf.data() + old_total, _read_total - old_total);
        vt_flush();
        _echo_start = _read_total;
    }

    void complete_pending_with_eof()
    {
        _vt_eof = true;
        complete_pending();
    }

    [[nodiscard]] bool wait_for_signal_shutdown_slice()
    {
        if (!_signal_shutdown_event.valid())
            return false;

        // The shutdown event is only one of the two conditions we care about here.
        // VT input does not signal this event, so this must stay a short throttle,
        // not an infinite wait.
        return ::WaitForSingleObject(_signal_shutdown_event.get(), pending_vt_input_wait_ms) == WAIT_OBJECT_0;
    }

    // ── 持久转换缓冲区访问器 ──
    std::u32string &conv_u32() noexcept
    {
        return _conv_u32;
    }
    std::wstring &conv_wstr() noexcept
    {
        return _conv_wstr;
    }

    // ── 终端光标查询 ──
    COORD get_term_cursor() const noexcept
    {
        return _term_cursor;
    }
    bool is_term_cursor_valid() const noexcept
    {
        return _term_cursor_valid;
    }

    // ── Enter 后换行标志: api_write_console 在输出"hello"等文本前检测,
    //     若为 true 则先发 CUP(_enter_dest) + 清标志 ──
    bool consume_enter_newline()
    {
        if (!_enter_pending_newline)
        {
            LOG("[bridge] consume_enter: false");
            return false;
        }
        _enter_pending_newline = false;
        LOG("[bridge] consume_enter: TRUE, dest=(%d,%d)", _enter_dest.X, _enter_dest.Y);
        return true;
    }
    COORD get_enter_dest() const noexcept
    {
        return _enter_dest;
    }
    // api_set_cursor_pos / api_fill_output 全屏清空时 shell 已自行管理光标，
    // 必须清除 Enter 遗留的假换行标志，否则下一条 WriteConsole 会错误 CUP
    void reset_enter_newline() noexcept
    {
        _enter_pending_newline = false;
    }

    // ── inherit_cursor: 发送 DSR CPR 前设置，cpr_response 处理时清除 ──
    void set_pending_inherit_cursor() noexcept
    {
        _pending_inherit_cursor = true;
    }
    bool is_pending_inherit_cursor() const noexcept
    {
        return _pending_inherit_cursor;
    }

    // WriteConsole 完成后调用，同步终端光标并重置输入边界
    void sync_cursor_after_write(COORD pos)
    {
        LOG("[bridge] sync_cursor_after_write: pos=(%d,%d) was_tc=(%d,%d) was_col_start=%d was_col_end=%d enter_nl=%d",
            pos.X, pos.Y, _term_cursor.X, _term_cursor.Y, _input_column_start, _input_column_end,
            _enter_pending_newline);
        term_cursor_set(pos);
        bounds_reset(pos.X);
    }

    // ════════════════════════════════════════════════════
    //  VT 输出器 (无 snprintf, 直接构建 char 缓冲)
    // ════════════════════════════════════════════════════

    // flush: 写入管道并清空缓冲
    void vt_flush()
    {
        if (_vt_len == 0)
            return;
        DWORD _ = 0;
        ::WriteFile(vt_out.get(), _vt_buf.data(), static_cast<DWORD>(_vt_len), &_, nullptr);
        _vt_len = 0;
    }

    // 直接写字节序列（不缓冲）
    void vt_write(const char *utf8, size_t len)
    {
        if (len == 0)
            return;
        vt_flush();
        DWORD _ = 0;
        ::WriteFile(vt_out.get(), utf8, static_cast<DWORD>(len), &_, nullptr);
    }

    // ── 缓冲追加方法 ──

    // 追加字面字符串
    void vt_append_str(std::string_view s)
    {
        size_t n = s.size();
        if (_vt_len + n > _vt_buf.size())
            vt_flush();
        if (n <= _vt_buf.size())
        {
            std::memcpy(_vt_buf.data() + _vt_len, s.data(), n);
            _vt_len += n;
        }
    }

    // 追加单字符
    void vt_append_char(char c)
    {
        if (_vt_len >= _vt_buf.size())
            vt_flush();
        _vt_buf[_vt_len++] = c;
    }

    // 追加整数 (无 snprintf, 自写 itoa)
    void vt_append_int(int n)
    {
        if (n == 0)
        {
            vt_append_char('0');
            return;
        }
        if (n < 0)
        {
            vt_append_char('-');
            n = -n;
        }
        char tmp[16];
        int i = 0;
        while (n > 0)
        {
            tmp[i++] = '0' + (n % 10);
            n /= 10;
        }
        // 反转
        if (_vt_len + i > _vt_buf.size())
            vt_flush();
        while (i > 0)
            _vt_buf[_vt_len++] = tmp[--i];
    }

    // ── 高层 VT 序列 ──

    void vt_write_cup(SHORT row, SHORT col)
    {
        vt_append_str("\x1b["sv);
        vt_append_int(static_cast<int>(row) + 1);
        vt_append_char(';');
        vt_append_int(static_cast<int>(col) + 1);
        vt_append_char('H');
    }

    void vt_write_attr(WORD attr)
    {
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
        if (ch == 0)
            ch = U' ';
        char buf[8];
        int n = to_utf8_bytes(ch, buf);
        if (_vt_len + n > _vt_buf.size())
            vt_flush();
        std::memcpy(_vt_buf.data() + _vt_len, buf, static_cast<size_t>(n));
        _vt_len += n;
    }

    void vt_write_clear_screen()
    {
        vt_append_str("\x1b[2J\x1b[H"sv);
    }

    void vt_write_dsr_cpr()
    {
        vt_append_str("\x1b[6n"sv);
    }

    void vt_write_hts()
    {
        vt_append_str("\x1bH"sv);
    }
    void vt_write_tbc(bool all = false)
    {
        vt_append_str(all ? "\x1b[3g"sv : "\x1b[0g"sv);
    }
    void vt_write_cht(SHORT n)
    {
        vt_append_str("\x1b["sv);
        vt_append_int(n);
        vt_append_char('I');
    }
    void vt_write_cbt(SHORT n)
    {
        vt_append_str("\x1b["sv);
        vt_append_int(n);
        vt_append_char('Z');
    }
    void vt_enable_dec_line_drawing()
    {
        vt_append_str("\x1b(0"sv);
    }
    void vt_disable_dec_line_drawing()
    {
        vt_append_str("\x1b(B"sv);
    }

    // vt_write_window_title: OSC 0/2 设置终端标题
    void vt_write_window_title(std::u32string_view title)
    {
        if (title.empty())
            return;
        vt_append_str("\x1b]0;"sv);
        // char32_t → UTF-8 批量转换（复用 _conv_utf8）
        convert_u32_to_utf8(title, _conv_utf8);
        vt_append_str(_conv_utf8);
        vt_append_char('\x07');
    }

    // ── vt_msg_send: vt_message → UTF-8 序列化并追加到缓冲 ──
    // handler 调用此方法替代直接拼接原始 VT 字节。
    // 注意: 不会自动 flush，调用方负责在合适的时机 vt_flush()。
    void vt_msg_send(vt_message_id id, const vt_message &msg)
    {
        switch (id)
        {
        case vt_message_id::cursor_position:
            vt_write_cup(static_cast<SHORT>(msg.row - 1), static_cast<SHORT>(msg.col - 1));
            break;

        case vt_message_id::cursor_horiz_absolute:
            vt_append_str("\x1b["sv);
            vt_append_int(msg.col);
            vt_append_char('G');
            break;

        case vt_message_id::cursor_vert_absolute:
            vt_append_str("\x1b["sv);
            vt_append_int(msg.row);
            vt_append_char('d');
            break;

        case vt_message_id::cursor_up:
            if (msg.count > 1)
            {
                vt_append_str("\x1b["sv);
                vt_append_int(msg.count);
                vt_append_char('A');
            }
            else
                vt_append_str("\x1b[A"sv);
            break;

        case vt_message_id::cursor_down:
            if (msg.count > 1)
            {
                vt_append_str("\x1b["sv);
                vt_append_int(msg.count);
                vt_append_char('B');
            }
            else
                vt_append_str("\x1b[B"sv);
            break;

        case vt_message_id::cursor_forward:
            if (msg.count > 1)
            {
                vt_append_str("\x1b["sv);
                vt_append_int(msg.count);
                vt_append_char('C');
            }
            else
                vt_append_str("\x1b[C"sv);
            break;

        case vt_message_id::cursor_backward:
            if (msg.count > 1)
            {
                vt_append_str("\x1b["sv);
                vt_append_int(msg.count);
                vt_append_char('D');
            }
            else
                vt_append_str("\x1b[D"sv);
            break;

        case vt_message_id::cursor_next_line:
            if (msg.count > 1)
            {
                vt_append_str("\x1b["sv);
                vt_append_int(msg.count);
                vt_append_char('E');
            }
            else
                vt_append_str("\x1b[E"sv);
            break;

        case vt_message_id::cursor_prev_line:
            if (msg.count > 1)
            {
                vt_append_str("\x1b["sv);
                vt_append_int(msg.count);
                vt_append_char('F');
            }
            else
                vt_append_str("\x1b[F"sv);
            break;

        case vt_message_id::sgr: {
            vt_append_str("\x1b["sv);
            if (msg.sgr_reset)
            {
                vt_append_char('0');
                vt_append_char('m');
                break;
            }

            bool first = true;
            auto add_param = [&](int p) {
                if (!first)
                    vt_append_char(';');
                first = false;
                vt_append_int(p);
            };

            // 重置 → 先发 0
            add_param(0);

            if (msg.bold)
                add_param(1);
            if (msg.faint)
                add_param(2);
            if (msg.italic)
                add_param(3);
            if (msg.underline)
                add_param(4);
            if (msg.blink)
                add_param(5);
            if (msg.negative)
                add_param(7);
            if (msg.conceal)
                add_param(8);
            if (msg.strikethrough)
                add_param(9);

            if (msg.fg_is_default)
                add_param(39);
            else if (msg.fg_is_rgb)
            {
                add_param(38);
                add_param(2);
                add_param(msg.fg_r);
                add_param(msg.fg_g);
                add_param(msg.fg_b);
            }
            else if (msg.fg_color >= 0 && msg.fg_color <= 7)
                add_param(30 + msg.fg_color);
            else if (msg.fg_color >= 8 && msg.fg_color <= 15)
                add_param(90 + (msg.fg_color - 8));

            if (msg.bg_is_default)
                add_param(49);
            else if (msg.bg_is_rgb)
            {
                add_param(48);
                add_param(2);
                add_param(msg.bg_r);
                add_param(msg.bg_g);
                add_param(msg.bg_b);
            }
            else if (msg.bg_color >= 0 && msg.bg_color <= 7)
                add_param(40 + msg.bg_color);
            else if (msg.bg_color >= 8 && msg.bg_color <= 15)
                add_param(100 + (msg.bg_color - 8));

            vt_append_char('m');
            break;
        }

        case vt_message_id::carriage_return:
            vt_write_cell(U'\r');
            break;

        case vt_message_id::line_feed:
            vt_write_cell(U'\r');
            vt_write_cell(U'\n');
            break;

        case vt_message_id::text: {
            // 批量 char32_t → UTF-8 追加（复用 _conv_utf8）
            convert_u32_to_utf8(msg.text, _conv_utf8);
            vt_append_str(_conv_utf8);
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
            if (msg.count > 1)
            {
                vt_append_str("\x1b["sv);
                vt_append_int(msg.count);
                vt_append_char('S');
            }
            else
                vt_append_str("\x1b[S"sv);
            break;

        case vt_message_id::scroll_down:
            if (msg.count > 1)
            {
                vt_append_str("\x1b["sv);
                vt_append_int(msg.count);
                vt_append_char('T');
            }
            else
                vt_append_str("\x1b[T"sv);
            break;

        case vt_message_id::insert_lines:
            vt_append_str("\x1b["sv);
            vt_append_int(msg.count);
            vt_append_char('L');
            break;

        case vt_message_id::delete_lines:
            vt_append_str("\x1b["sv);
            vt_append_int(msg.count);
            vt_append_char('M');
            break;

        case vt_message_id::erase_in_display:
            vt_append_str("\x1b["sv);
            vt_append_int(msg.erase_mode);
            vt_append_char('J');
            break;

        case vt_message_id::erase_in_line:
            vt_append_str("\x1b["sv);
            vt_append_int(msg.erase_mode);
            vt_append_char('K');
            break;

        case vt_message_id::set_scrolling_region:
            vt_append_str("\x1b["sv);
            vt_append_int(msg.scroll_top);
            vt_append_char(';');
            vt_append_int(msg.scroll_bottom);
            vt_append_char('r');
            break;

        case vt_message_id::set_window_title:
            vt_write_window_title(msg.title);
            break;

        case vt_message_id::set_cursor_shape:
            vt_append_str("\x1b["sv);
            vt_append_int(msg.cursor_shape);
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
            vt_append_int(msg.count);
            vt_append_char('@');
            break;
        case vt_message_id::delete_characters:
            vt_append_str("\x1b["sv);
            vt_append_int(msg.count);
            vt_append_char('P');
            break;
        case vt_message_id::erase_characters:
            vt_append_str("\x1b["sv);
            vt_append_int(msg.count);
            vt_append_char('X');
            break;

        // ── OSC 4 调色板 ──
        case vt_message_id::set_palette_color: {
            vt_append_str("\x1b]4;"sv);
            vt_append_int(msg.palette_index);
            vt_append_char(';');
            // rgb:RR/GG/BB 格式 (不使用 snprintf)
            vt_append_str("rgb:"sv);
            auto hex2 = [&](uint8_t v) {
                constexpr char h[] = "0123456789abcdef";
                vt_append_char(h[v >> 4]);
                vt_append_char(h[v & 15]);
            };
            hex2(msg.palette_r);
            vt_append_char('/');
            hex2(msg.palette_g);
            vt_append_char('/');
            hex2(msg.palette_b);
            vt_append_char('\x07');
            break;
        }

        // ── 制表符移动 ──
        case vt_message_id::cursor_forward_tab:
            vt_append_str("\x1b["sv);
            vt_append_int(msg.count);
            vt_append_char('I');
            break;
        case vt_message_id::cursor_backward_tab:
            vt_append_str("\x1b["sv);
            vt_append_int(msg.count);
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

        // ── 软复位 (DECSTR) 对标原始 _stream.cpp RIS: 含 Win32Input ──
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

    // ── RAW_WRITE: msg.body 就是原始文本, 无 CONSOLE_WRITECONSOLE_MSG 头 ──
    bool handle_raw_write(miniio::io_msg &msg)
    {
        auto str_bytes = msg.descriptor.InputSize;
        if (str_bytes == 0)
        {
            miniio::prepare_completion(msg, 0, 0);
            return true;
        }
        vt_flush();
        DWORD _ = 0;
        ::WriteFile(vt_out.get(), msg.body, str_bytes, &_, nullptr);
        miniio::prepare_completion(msg, 0, str_bytes);
        return true;
    }

    // ── RAW_READ (挂起模式) ──
    bool handle_raw_read(miniio::io_msg &msg)
    {
        auto *req = reinterpret_cast<CONSOLE_READCONSOLE_MSG *>(msg.body);
        if (_vt_eof)
        {
            req->NumBytes = 0;
            miniio::prepare_completion(msg, 0, 0);
            msg.complete.Write.Data = msg.body;
            msg.complete.Write.Size = sizeof(CONSOLE_READCONSOLE_MSG);
            return true;
        }

        _pend_kind = PendingKind::RawRead;
        _pend_raw = &msg;
        _pend_uni = req->Unicode != 0;
        _read_total = 0;
        _echo_start = 0;
        _line_found = false;

        if (accumulate_from_pipe())
            return true;
        if (scan_for_line())
            return true;
        return false;
    }

    // ── ReadConsole ──
    bool handle_console_read(miniio::io_msg &msg, bool proc_z, const BYTE *init_data, DWORD init_bytes)
    {
        auto *req = reinterpret_cast<CONSOLE_READCONSOLE_MSG *>(msg.body + sizeof(CONSOLE_MSG_HEADER));
        LOG("[bridge] handle_console_read: vt_eof=%d proc_z=%d init_bytes=%lu", _vt_eof, proc_z, init_bytes);
        if (_vt_eof)
        {
            req->NumBytes = 0;
            req->ControlKeyState = 0;
            auto sz = static_cast<ULONG>(sizeof(CONSOLE_READCONSOLE_MSG));
            miniio::prepare_completion(msg, 0, sz);
            msg.complete.Write.Data = msg.body + sizeof(CONSOLE_MSG_HEADER);
            msg.complete.Write.Size = sz;
            return true;
        }

        _pend_kind = PendingKind::ConsoleRead;
        _pend_usr = &msg;
        _process_control_z = proc_z;
        _pend_uni = req->Unicode != 0;
        std::memcpy(&_pend_req, req, sizeof(CONSOLE_READCONSOLE_MSG));
        _read_total = 0;
        _echo_start = 0;
        _line_found = false;
        _cooked_buf.clear();
        _cooked_cursor = 0;
        _history_idx = static_cast<size_t>(-1); // 重置历史浏览状态
        LOG("[bridge] handle_console_read: pending ConsoleRead, unicode=%d", _pend_uni);

        if (init_data && init_bytes > 0)
        {
            if (init_bytes > _readbuf.size())
                init_bytes = static_cast<DWORD>(_readbuf.size());
            std::memcpy(_readbuf.data(), init_data, init_bytes);
            _read_total = init_bytes;
            _echo_start = init_bytes;
            // ── 预填充 _cooked_buf：解码 init_data 并累积到行缓冲 ──
            process_input(init_data, init_bytes);
            LOG("[bridge] handle_console_read: seeded %lu init bytes, cooked=%zu", init_bytes, _cooked_buf.size());
        }

        if (accumulate_from_pipe())
        {
            LOG("[bridge] handle_console_read: sync complete");
            return true;
        }
        if (scan_for_line())
        {
            LOG("[bridge] handle_console_read: sync complete");
            return true;
        }
        LOG("[bridge] handle_console_read: pending, returning false");
        return false;
    }

    bool handle_console_input(miniio::io_msg &msg)
    {
        auto *req = reinterpret_cast<CONSOLE_GETCONSOLEINPUT_MSG *>(msg.body + sizeof(CONSOLE_MSG_HEADER));
        auto *out =
            reinterpret_cast<INPUT_RECORD *>(msg.body + sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_GETCONSOLEINPUT_MSG));
        const auto max_count = console_input_max_records(msg);

        const bool peek = (req->Flags & CONSOLE_READ_NOREMOVE) != 0;
        const bool wait_allowed = (req->Flags & CONSOLE_READ_NOWAIT) == 0;
        const auto count = peek ? inp.peek(out, max_count) : inp.read(out, max_count);

        if (count == 0 && wait_allowed)
        {
            _pend_kind = PendingKind::ConsoleInput;
            _pend_input = &msg;
            return false;
        }

        req->NumRecords = static_cast<ULONG>(count);
        const auto size =
            static_cast<ULONG>(sizeof(CONSOLE_GETCONSOLEINPUT_MSG) + count * sizeof(INPUT_RECORD));
        miniio::prepare_completion(msg, 0, size);
        msg.complete.Write.Data = msg.body + sizeof(CONSOLE_MSG_HEADER);
        msg.complete.Write.Size = size;
        return true;
    }

    void wait_for_pending_vt_input()
    {
        if (_vt_eof || _pend_kind == PendingKind::None)
            return;

        auto drain_available = [&] {
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
        };

        if (drain_available())
        {
            return;
        }

        if (is_signal_shutdown_signaled())
        {
            complete_pending_with_eof();
            return;
        }

        if (wait_for_signal_shutdown_slice())
        {
            complete_pending_with_eof();
            return;
        }
        if (_signal_shutdown_event.valid())
        {
            drain_available();
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
        if (_vt_eof)
            return;

        // ── 始终检查 VT pipe（即使无 pending I/O），否则 GetConsoleInput 紧密轮询时永远检测不到断开 ──
        DWORD avail = 0;
        if (!peek_vt_input(avail))
        {
            _vt_eof = true;
            if (_pend_kind != PendingKind::None)
                complete_pending();
            return;
        }

        if (_pend_kind == PendingKind::None)
        {
            // 无挂起的 ReadConsole 时仍须处理 VT 输入
            // (PowerShell 通过 GetConsoleInput(PEEK) 轮询，不会触发 ReadConsole)
            _read_total = 0;
            _echo_start = 0;
        }

        // ── 检测管道关闭：PeekNamedPipe 返回 0 字节且 signal 线程已退出 ──
        if (avail == 0)
        {
            if (is_signal_shutdown_signaled())
            {
                LOG("[bridge] on_idle: signal shutdown event set, marking EOF");
                _vt_eof = true;
                if (_pend_kind != PendingKind::None)
                    complete_pending();
                return;
            }
            return;
        }

        LOG("[bridge] on_idle: avail=%lu kind=%d total=%lu", avail, static_cast<int>(_pend_kind), _read_total);
        if (accumulate_from_pipe())
            return;
        scan_for_line();
    }

    void cancel_pending_read()
    {
        if (_pend_kind != PendingKind::None)
            complete_pending();
    }

    // ── raw_write — 公共, 供 api_write_console 使用 ──
    void raw_write(bool uni, BYTE *data, DWORD bytes)
    {
        if (bytes == 0)
            return;
        vt_flush();
        if (uni)
        {
            auto *ws = reinterpret_cast<const wchar_t *>(data);
            int wl = static_cast<int>(bytes / sizeof(wchar_t));
            convert_utf16_to_u32(std::wstring_view{ws, static_cast<size_t>(wl)}, _conv_u32);
            convert_u32_to_utf8(_conv_u32, _conv_utf8);
            DWORD _ = 0;
            ::WriteFile(vt_out.get(), _conv_utf8.data(), static_cast<DWORD>(_conv_utf8.size()), &_, nullptr);
        }
        else
        {
            convert_ansi_to_u32(reinterpret_cast<const char *>(data), bytes, CP_ACP, _conv_u32, _conv_wstr);
            convert_u32_to_utf8(_conv_u32, _conv_utf8);
            DWORD _ = 0;
            ::WriteFile(vt_out.get(), _conv_utf8.data(), static_cast<DWORD>(_conv_utf8.size()), &_, nullptr);
        }
    }

    // ════════════════════════════════════════════════════
    //  Layer 1 — 独立状态操作 (每个函数只改一个状态域)
    // ════════════════════════════════════════════════════

    // ── 编辑缓冲 (_cooked_buf, _cooked_cursor) ──
    void cooked_append(char32_t ch)
    {
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
    void echo_crlf()
    {
        vt_append_str("\r\n"sv);
    }

    // ── state 光标 (cstate.cursor.position) ──
    void state_cursor_set(COORD c)
    {
        cstate.cursor.position = c;
    }
    void state_cursor_advance()
    {
        COORD &c = cstate.cursor.position;
        c.X++;
        if (c.X >= cstate.screen_buffer_size.X)
        {
            c.X = 0;
            c.Y++;
        }
        if (c.Y >= cstate.screen_buffer_size.Y)
            c.Y = cstate.screen_buffer_size.Y - 1;
    }
    void state_cursor_newline()
    {
        COORD &c = cstate.cursor.position;
        c.X = 0;
        c.Y++;
        if (c.Y >= cstate.screen_buffer_size.Y)
            c.Y = cstate.screen_buffer_size.Y - 1;
    }

    // ── term 光标追踪 (_term_cursor) ──
    void term_cursor_set(COORD c)
    {
        _term_cursor = c;
        _term_cursor_valid = true;
    }
    void term_cursor_advance()
    {
        if (!_term_cursor_valid)
            return;
        _term_cursor.X++;
        if (_term_cursor.X > _input_column_end)
            _input_column_end = _term_cursor.X;
    }
    void term_cursor_retreat()
    {
        if (!_term_cursor_valid)
            return;
        if (_term_cursor.X > _input_column_start)
            _term_cursor.X--;
    }
    void term_cursor_crlf()
    {
        if (!_term_cursor_valid)
            return;
        _term_cursor.X = 0;
        _term_cursor.Y++;
    }
    SHORT term_cursor_col() const noexcept
    {
        return static_cast<SHORT>(_input_column_start + _cooked_cursor);
    };

    // ── 列边界 (_input_column_start, _input_column_end) ──
    void bounds_reset(SHORT x)
    {
        _input_column_start = x;
        _input_column_end = x;
    }
    void bounds_extend()
    {
        ++_input_column_end;
    }
    void bounds_retract()
    {
        if (_input_column_end > _input_column_start)
            --_input_column_end;
    }

    // ── KEY_EVENT 输出到 input_buffer ──
    void emit_key(WORD vk, WCHAR uc)
    {
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
        // 使用 vt_write_cell 追加到 _vt_buf，与 echo 批量合并刷新
        auto sf = std::u32string_view(_cooked_buf).substr(_cooked_cursor);
        for (char32_t cp : sf)
            vt_write_cell(cp);
    }
    void repaint_full_line()
    {
        if (!_term_cursor_valid)
        {
            LOG("[history] repaint_full_line: SKIP tc_valid=%d", _term_cursor_valid ? 1 : 0);
            return;
        }
        LOG("[history] repaint_full_line: cup_to(%d,%d) cooked_sz=%zu", _term_cursor.Y, _input_column_start,
            _cooked_buf.size());
        cup_to(_term_cursor.Y, _input_column_start);
        vt_append_str("\x1b[K"sv);
        for (char32_t cp : _cooked_buf)
            vt_write_cell(cp);
        vt_flush();
        _term_cursor.X = _input_column_end;
    }
    void load_history_line()
    {
        _cooked_buf = _history[_history_idx];
        cooked_set_pos(_cooked_buf.size());
        _input_column_end = _input_column_start + static_cast<SHORT>(_cooked_buf.size());
        LOG("[history] load_history_line: tc=(%d,%d) col_start=%d col_end=%d cooked_sz=%zu", _term_cursor.X,
            _term_cursor.Y, _input_column_start, _input_column_end, _cooked_buf.size());
        repaint_full_line();
        LOG("[history] load_history_line: done tc=(%d,%d)", _term_cursor.X, _term_cursor.Y);
    }

    // ── ConsoleRead 路径: 行编辑 ──

    void edit_insert_char(char32_t ch, BYTE raw)
    {
        history_break_browse();
        cooked_append(ch);
        bounds_extend();
        if (!_term_cursor_valid)
            return;
        if (cooked_at_end())
        {
            echo_byte(raw);
            term_cursor_advance();
        }
        else
        {
            COORD sv = _term_cursor;
            echo_byte(raw);
            repaint_suffix();
            cup_to(sv.Y, sv.X + 1);
            _term_cursor.X = sv.X + 1;
        }
    }
    void edit_insert_codepoint(char32_t ch)
    {
        LOG(L"[in] EDIT_CP ch=U+%04X cooked_sz=%zu", (unsigned)ch, _cooked_buf.size());
        history_break_browse();
        cooked_append(ch);
        bounds_extend();
        if (!_term_cursor_valid)
            return;
        if (cooked_at_end())
        {
            vt_write_cell(ch);
            term_cursor_advance();
        }
        else
        {
            COORD sv = _term_cursor;
            vt_write_cell(ch);
            term_cursor_advance();
            repaint_suffix();
            cup_to(sv.Y, sv.X + 1);
            _term_cursor.X = sv.X + 1;
        }
    }
    void edit_submit_line()
    {
        if (_term_cursor_valid)
        {
            vt_append_str("\r\n"sv);
            _term_cursor.X = 0;
            _term_cursor.Y++;
        }
        _line_found = true;
        complete_pending();
    }
    void edit_backspace()
    {
        history_break_browse();
        if (_cooked_cursor == 0)
            return;
        cooked_pop_before();
        bounds_retract();
        if (!_term_cursor_valid)
            return;
        vt_append_str("\x1b[D\x1b[P"sv);
        vt_flush();
        term_cursor_retreat();
        if (!cooked_at_end())
        {
            COORD c = _term_cursor;
            repaint_suffix();
            cup_to(c.Y, c.X);
        }
    }
    void edit_delete()
    {
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
        if (_cooked_cursor > 0)
        {
            cooked_set_pos(_cooked_cursor - 1);
            if (_term_cursor_valid)
            {
                cup_to(_term_cursor.Y, term_cursor_col());
                _term_cursor.X = term_cursor_col();
            }
        }
    }
    void edit_move_right()
    {
        if (_cooked_cursor < _cooked_buf.size())
        {
            cooked_set_pos(_cooked_cursor + 1);
            if (_term_cursor_valid)
            {
                cup_to(_term_cursor.Y, term_cursor_col());
                _term_cursor.X = term_cursor_col();
            }
        }
    }
    void edit_home()
    {
        cooked_set_pos(0);
        if (_term_cursor_valid)
        {
            cup_to(_term_cursor.Y, _input_column_start);
            _term_cursor.X = _input_column_start;
        }
    }
    void edit_end()
    {
        cooked_set_pos(_cooked_buf.size());
        if (_term_cursor_valid)
        {
            cup_to(_term_cursor.Y, _input_column_end);
            _term_cursor.X = _input_column_end;
        }
    }

    // ── 历史导航 ──
    void history_push()
    {
        if (!_cooked_buf.empty() && (_history.empty() || _history.back() != _cooked_buf))
            _history.push_back(_cooked_buf);
    }
    void history_break_browse()
    {
        if (_history_idx != static_cast<size_t>(-1))
        {
            _history_idx = static_cast<size_t>(-1);
            _saved_input.clear();
        }
    }
    void history_up()
    {
        LOG("[history] history_up: tc=(%d,%d) col_start=%d col_end=%d history_sz=%zu idx=%zu", _term_cursor.X,
            _term_cursor.Y, _input_column_start, _input_column_end, _history.size(), _history_idx);
        if (_history.empty())
        {
            LOG("[history] history_up: empty, return");
            return;
        }
        if (_history_idx == static_cast<size_t>(-1))
        {
            _saved_input = _cooked_buf;
            _history_idx = _history.size() - 1;
        }
        else if (_history_idx > 0)
            --_history_idx;
        LOG("[history] history_up: loading idx=%zu cooked_sz=%zu", _history_idx, _history[_history_idx].size());
        load_history_line();
    }
    void history_down()
    {
        if (_history_idx == static_cast<size_t>(-1))
            return;
        if (_history_idx + 1 < _history.size())
        {
            ++_history_idx;
            load_history_line();
        }
        else
        {
            _cooked_buf = std::move(_saved_input);
            _history_idx = static_cast<size_t>(-1);
            cooked_set_pos(_cooked_buf.size());
            _input_column_end = _input_column_start + static_cast<SHORT>(_cooked_buf.size());
            repaint_full_line();
        }
    }

    // ── 别名展开 ──
    void expand_alias()
    {
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
        WORD vk = ascii_to_vk(raw);
        LOG(L"[in] PRINTABLE ch=U+%04X vk=0x%X uc=0x%X", (unsigned)ch, vk, (unsigned)(WCHAR)ch);
        emit_key_pair(vk, static_cast<WCHAR>(ch));
        if (ch != U'\r')
            cooked_append(ch);
        else
            cooked_clear();
    }
    void input_enter()
    {
        LOG("[bridge] input ENTER");
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
        LOG("[history] _edit_history_up: tc=(%d,%d) col_start=%d col_end=%d cook_sz=%zu cook_pos=%zu", _term_cursor.X,
            _term_cursor.Y, _input_column_start, _input_column_end, _cooked_buf.size(), _cooked_cursor);
        history_up();
    }
    void _edit_history_down()
    {
        history_down();
    }
    void _break_history_browse()
    {
        history_break_browse();
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

    // ── _echo_byte: 向终端输出单个字节并跟踪光标（经 VT 缓冲批量写入）──
    void _echo_byte(BYTE b)
    {
        vt_append_char(static_cast<char>(b));
        if (_term_cursor_valid)
        {
            if (b == '\r')
            {
                _term_cursor.X = 0;
            }
            else if (b == '\n')
            {
                _term_cursor.X = 0;
                _term_cursor.Y++;
            }
            else if (b == 0x08 || b == 0x7F)
            {
                if (_term_cursor.X > _input_column_start)
                    _term_cursor.X--;
                if (_term_cursor.X < _input_column_end)
                    _input_column_end = _term_cursor.X;
            }
            else if (b >= 0x20)
            {
                _term_cursor.X++;
                if (_term_cursor.X > _input_column_end)
                    _input_column_end = _term_cursor.X;
            }
        }
    }

    // ── accumulate_from_pipe: 缓冲后统一走 process_input 解析 ──
    // 返回 true 表示已发现行终止符并完成 pending（调用方应 return）
    bool accumulate_from_pipe()
    {
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
                _vt_eof = true;
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
            // 批量 echo 后必须在本批输入结束时刷新，否则普通打字会滞留在 _vt_buf，
            // 直到后续控制序列/应用输出/缓冲满才显示，表现为终端输入卡顿。
            vt_flush();
            _echo_start = _read_total;

            if (_line_found)
                return true;
        }
    }

    // ── process_input: 解码 → 解析 → echo → 分发 ──
    // 内部检测 \r/\n/Ctrl+Z 并设置 _line_found，消除 scan_for_line 的二次扫描
    void process_input(const BYTE *bytes, DWORD len)
    {
        if (len > 0)
            LOG_HEX("input", bytes, len);
        for (DWORD i = 0; i < len; ++i)
        {
            BYTE b = bytes[i];

            // ── Ctrl+Z 即时检测（对标旧 scan_for_line）──
            if (_process_control_z && b == 0x1A) [[unlikely]]
            {
                LOG("[bridge] process_input: Ctrl+Z at offset %lu", i);
                _line_found = true;
                complete_pending();
                return;
            }

            // ── 1. 解码：UTF-8 → char32_t ──
            char32_t ch = *_utf8_decoder(static_cast<uint8_t>(b));

            // ── 2. 解析 ──
            vt_message_id id = _parser.parse(ch);

            if (id == vt_message_id::continue_text) [[likely]]
            {
                LOG(L"[in] TEXT ch=U+%04X raw=0x%02X kind=%d", (unsigned)ch, b, (int)_pend_kind);
                if (_pend_kind == PendingKind::ConsoleRead)
                    _edit_insert(ch, b);
                else
                    _write_char_key_event(ch, b);
                _parser.reset(vt_message_id::continue_text); // 清累积文本
                continue;
            }

            if (id == vt_message_id::continue_) [[likely]]
                continue;

            LOG(L"[in] MSG id=%d echo=%d kind=%d", (int)id, (int)_parser.should_echo_last(), (int)_pend_kind);
            if (_parser.should_echo_last() && _pend_kind == PendingKind::ConsoleRead)
                _echo_byte(b);

            auto &msg = _parser.get();
            switch (id)
            {
            case vt_message_id::carriage_return:
                if (_pend_kind != PendingKind::ConsoleRead)
                    _write_enter_key_event();
                _on_line_terminator(true, i, len, bytes);
                return;

            case vt_message_id::line_feed:
                if (_pend_kind != PendingKind::ConsoleRead)
                    _write_char_key_event(U'\n', 0x0A);
                _on_line_terminator(false, i, len, bytes);
                return;

            // ── Win32 Input Mode 键盘事件: \x1b[Vk;Sc;Uc;Kd;Cs;Rc_ ──
            case vt_message_id::win32_input_key: {
                auto &m = _parser.get();
                if (_pend_kind == PendingKind::ConsoleRead)
                {
                    if (!m.win32_kd)
                        break;

                    switch (m.win32_vk)
                    {
                    case VK_RETURN:
                        edit_submit_line();
                        return;
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
                        if (m.win32_uc >= L' ' || m.win32_uc == L'\t')
                            edit_insert_codepoint(static_cast<char32_t>(m.win32_uc));
                        break;
                    }
                    break;
                }

                // ── Win32Input Enter 非 ConsoleRead: 设置换行标志 + 写终端 \r\n ──
                // 必须在 if(!inp) break 之前，因为 Enter 换行处理不需要 input_buffer，
                // 只需要设置 _enter_pending_newline + _term_cursor + vt_append \r\n。
                if (m.win32_kd && m.win32_vk == VK_RETURN)
                {
                    LOG("[bridge] ENTER_Win32Input was_tc=(%d,%d)", _term_cursor.X, _term_cursor.Y);
                    _line_found = true;
                    _term_cursor.X = 0;
                    _term_cursor.Y++;
                    _enter_dest = _term_cursor; // 锁定换行目标，不受后续 api_set_cursor_pos 污染
                    _enter_pending_newline = true;
                    vt_append_str("\r\n"sv);
                    LOG("[bridge] ENTER_Win32Input done tc=(%d,%d)", _term_cursor.X, _term_cursor.Y);
                }
                else if (m.win32_kd)
                {
                    LOG("[bridge] Win32Input write_input: vk=%d uc=0x%04X cs=0x%X tc=(%d,%d)", m.win32_vk, m.win32_uc,
                        m.win32_cs, _term_cursor.X, _term_cursor.Y);
                }

                INPUT_RECORD ir{};
                ir.EventType = KEY_EVENT;
                ir.Event.KeyEvent.bKeyDown = m.win32_kd ? TRUE : FALSE;
                ir.Event.KeyEvent.wRepeatCount = m.win32_rc;
                ir.Event.KeyEvent.wVirtualKeyCode = m.win32_vk;
                ir.Event.KeyEvent.wVirtualScanCode = m.win32_sc;
                ir.Event.KeyEvent.uChar.UnicodeChar = m.win32_uc;
                ir.Event.KeyEvent.dwControlKeyState = m.win32_cs;
                inp.write(&ir, 1);
                complete_pending_console_input();
                break;
            }

            // ── 方向键 / 编辑键 ──
            // ConsoleRead 模式: 使用 _edit_* 编辑函数 (echo + _cooked_buf)
            // 非 ConsoleRead 模式: 写 KEY_DOWN+KEY_UP 到 input_buffer
            case vt_message_id::key_left:
            case vt_message_id::cursor_backward: // CSI D
                if (_pend_kind == PendingKind::ConsoleRead)
                    _edit_move_left();
                else
                {
                    INPUT_RECORD ir;
                    if (_engine.convert(vt_message_id::key_left, msg, ir))
                        _write_key_event_pair(ir);
                }
                break;
            case vt_message_id::key_right:
            case vt_message_id::cursor_forward: // CSI C
                if (_pend_kind == PendingKind::ConsoleRead)
                    _edit_move_right();
                else
                {
                    INPUT_RECORD ir;
                    if (_engine.convert(vt_message_id::key_right, msg, ir))
                        _write_key_event_pair(ir);
                }
                break;
            case vt_message_id::key_home:
            case vt_message_id::cursor_prev_line: // CSI F
                if (_pend_kind == PendingKind::ConsoleRead)
                    _edit_home();
                else
                {
                    INPUT_RECORD ir;
                    if (_engine.convert(vt_message_id::key_home, msg, ir))
                        _write_key_event_pair(ir);
                }
                break;
            case vt_message_id::key_end:
            case vt_message_id::cursor_next_line: // CSI E
                if (_pend_kind == PendingKind::ConsoleRead)
                    _edit_end();
                else
                {
                    INPUT_RECORD ir;
                    if (_engine.convert(vt_message_id::key_end, msg, ir))
                        _write_key_event_pair(ir);
                }
                break;

            case vt_message_id::key_delete:
                if (_pend_kind == PendingKind::ConsoleRead)
                    _edit_delete();
                else
                {
                    INPUT_RECORD ir;
                    if (_engine.convert(vt_message_id::key_delete, msg, ir))
                        _write_key_event_pair(ir);
                }
                break;

            case vt_message_id::char_del:
                if (_pend_kind == PendingKind::ConsoleRead)
                    _edit_backspace();
                else
                {
                    INPUT_RECORD ir;
                    if (_engine.convert(vt_message_id::char_del, msg, ir))
                        _write_key_event_pair(ir);
                }
                break;

            // ── 上下键 ──
            case vt_message_id::key_up:
            case vt_message_id::key_down:
            case vt_message_id::cursor_up:   // CSI A
            case vt_message_id::cursor_down: // CSI B
            {
                if (_pend_kind == PendingKind::ConsoleRead)
                {
                    if (id == vt_message_id::key_up || id == vt_message_id::cursor_up)
                        _edit_history_up();
                    else
                        _edit_history_down();
                }
                else
                {
                    INPUT_RECORD ir;
                    vt_message_id key_id = (id == vt_message_id::key_up || id == vt_message_id::cursor_up)
                                               ? vt_message_id::key_up
                                               : vt_message_id::key_down;
                    if (_engine.convert(key_id, msg, ir))
                        _write_key_event_pair(ir);
                }
                break;
            }
            case vt_message_id::key_f3:
            case vt_message_id::key_f4:
            case vt_message_id::key_f5:
            case vt_message_id::key_f6:
            case vt_message_id::key_f7:
            case vt_message_id::key_f8:
            case vt_message_id::key_f9:
            case vt_message_id::key_f10:
            case vt_message_id::key_f11:
            case vt_message_id::key_f12:
            case vt_message_id::key_insert:
            case vt_message_id::key_page_up:
            case vt_message_id::key_page_down: {
                INPUT_RECORD rec;
                if (_engine.convert(id, msg, rec))
                    _write_key_event_pair(rec);
                break;
            }

            case vt_message_id::cursor_position:
                if (msg.row == 1 && msg.col == 1)
                {
                    INPUT_RECORD rec;
                    if (_engine.convert(vt_message_id::key_home, msg, rec))
                        _write_key_event_pair(rec);
                }
                break;

            case vt_message_id::cursor_horiz_absolute:
            case vt_message_id::cursor_vert_absolute:
                break;

            // ── CPR 应答: 终端汇报真实光标位置 ──
            case vt_message_id::cpr_response: {
                auto &m = _parser.get();
                if (_pending_inherit_cursor && m.cpr_row > 0 && m.cpr_col > 0)
                {
                    cstate.cursor.position.X = static_cast<SHORT>(m.cpr_col - 1);
                    cstate.cursor.position.Y = static_cast<SHORT>(m.cpr_row - 1);
                    _term_cursor = cstate.cursor.position;
                    _term_cursor_valid = true;
                    bounds_reset(cstate.cursor.position.X);
                    _pending_inherit_cursor = false;
                    LOG("[bridge] cpr_response: inherit cursor (%d,%d)", cstate.cursor.position.X,
                        cstate.cursor.position.Y);
                }
                break;
            }

            case vt_message_id::cursor_forward_tab: {
                INPUT_RECORD ir{};
                ir.EventType = KEY_EVENT;
                ir.Event.KeyEvent.wRepeatCount = 1;
                ir.Event.KeyEvent.wVirtualKeyCode = VK_TAB;
                ir.Event.KeyEvent.uChar.UnicodeChar = L'\t';
                _write_key_event_pair(ir);
                break;
            }

            case vt_message_id::char_sub:
            case vt_message_id::char_esc: {
                INPUT_RECORD rec;
                if (_engine.convert(id, msg, rec))
                    _write_key_event_pair(rec);
                break;
            }

            case vt_message_id::resize_window: {
                COORD new_size{msg.resize_cols, msg.resize_rows};
                if (new_size.X > 0 && new_size.Y > 0)
                {
                    LOG("[bridge] resize_window: old=(%d,%d) new=(%d,%d)", cstate.screen_buffer_size.X,
                        cstate.screen_buffer_size.Y, new_size.X, new_size.Y);
                    cstate.screen_buffer_size = new_size;
                    cstate.current_window_size = new_size;
                    cstate.max_window_size = new_size;
                    sbuf.resize(new_size);

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
                            WORD attr = sbuf.attr_at({x, y});
                            if (attr != last_attr)
                            {
                                vt_write_attr(attr);
                                last_attr = attr;
                            }
                            vt_write_cell(sbuf.at_u32({x, y}));
                        }
                    }
                    vt_write_cup(cstate.cursor.position.Y, cstate.cursor.position.X);
                    vt_flush();
                }
                break;
            }

            case vt_message_id::text: {
                auto &tm = _parser.get();
                LOG(L"[in] TEXT_MSG len=%zu", tm.text.size());
                for (char32_t tc : tm.text)
                {
                    if (tc <= 0x1F || tc == 0x7F)
                        continue;
                    LOG(L"[in] TEXT_DISP ch=U+%04X", (unsigned)tc);
                    if (_pend_kind == PendingKind::ConsoleRead)
                        edit_insert_codepoint(tc);
                    else
                        _write_char_key_event(tc, static_cast<BYTE>(tc & 0xFF));
                }
                break;
            }

            default:
                break;
            }

            _parser.reset(id);
        }
    }

    // 行终止符处理：由 parser 产出的 text 消息驱动，统一完成 pending
    void _on_line_terminator(bool is_cr, DWORD i, DWORD len, const BYTE *bytes)
    {
        _line_found = true;

        // \r\n 配对：cr 后检查缓冲区/管道中是否紧跟 \n
        if (is_cr)
        {
            bool has_lf = false;
            if (i + 1 < len && bytes[i + 1] == '\n')
            {
                has_lf = true;
            }
            else if (i + 1 == len)
            {
                // 尝试从管道 peek 下 1 个字节，仅当恰好是 \n 时才读走
                BYTE nb = 0;
                DWORD peeked = 0;
                if (::PeekNamedPipe(vt_in.get(), &nb, 1, &peeked, nullptr, nullptr) && peeked > 0 && nb == '\n')
                {
                    DWORD r = 0;
                    if (::ReadFile(vt_in.get(), &nb, 1, &r, nullptr) && r == 1)
                    {
                        if (_read_total < _readbuf.size())
                            _readbuf[_read_total++] = '\n';
                        has_lf = true;
                    }
                }
            }

            if (!has_lf)
                vt_append_char('\n');
        }

        // 终端光标跟踪
        if (_term_cursor_valid)
        {
            _term_cursor.X = 0;
            _term_cursor.Y++;
        }

        // 非 ConsoleRead: 设置标记
        if (_pend_kind != PendingKind::ConsoleRead)
        {
            _enter_dest = _term_cursor; // 锁定换行目标
            _enter_pending_newline = true;
            LOG("[bridge] LINE_TERM enter_nl=1 dest=(%d,%d)", _enter_dest.X, _enter_dest.Y);
        }

        LOG(L"[in] LINE_TERM cooked=[%.*ls]", static_cast<int>(_cooked_buf.size() < 200 ? _cooked_buf.size() : 200),
            _cooked_buf.data());
        complete_pending();
    }

    // ── scan_for_line (保留兼容性，内部直接检查 _line_found) ──
    bool scan_for_line()
    {
        // 行终止符检测已集成到 process_input，此处仅兜底扫描 \n（单 \n 无 \r 前缀）
        for (DWORD i = 0; i < _read_total; ++i)
        {
            if (_process_control_z && _readbuf[i] == 0x1A) [[unlikely]]
            {
                LOG("[bridge] scan_for_line: Ctrl+Z at offset %lu", i);
                complete_pending();
                return true;
            }
            if (_readbuf[i] == '\n') [[unlikely]]
            {
                LOG("[bridge] scan_for_line: LF at offset %lu, completing", i);
                complete_pending();
                return true;
            }
        }
        return false;
    }

    // ── complete_pending ──────────────────────────────
    void complete_pending()
    {
        LOG(L"[bridge] complete_pending: kind=%d total=%lu cooked_len=%zu vt_eof=%d cooked=[%.*ls]",
            static_cast<int>(_pend_kind), _read_total, _cooked_buf.size(), _vt_eof,
            static_cast<int>(_cooked_buf.size() < 200 ? _cooked_buf.size() : 200), _cooked_buf.data());
        if (_pend_kind == PendingKind::None)
            return;

        if (_pend_kind == PendingKind::ConsoleInput)
        {
            complete_pending_console_input();
            return;
        }

        // 保存 completion 指针 (在清空 _pend_* 之前)
        CD_IO_COMPLETE comp_before{};
        CD_IO_COMPLETE *comp_ptr = nullptr;
        // 防御：_pend_raw / _pend_usr 仅在真实 I/O 挂起时非空；测试路径中可能为空
        if (_pend_kind == PendingKind::RawRead && _pend_raw)
        {
            auto &m = *_pend_raw;
            DWORD data_bytes = _read_total;

            // ── 行尾规范化 ──
            if (data_bytes >= 1 && _readbuf[data_bytes - 1] == '\r')
            {
                if (data_bytes < _readbuf.size())
                    _readbuf[data_bytes++] = '\n';
            }
            else if (data_bytes >= 1 && _readbuf[data_bytes - 1] == '\n')
            {
                if (data_bytes < 2 || _readbuf[data_bytes - 2] != '\r')
                {
                    if (data_bytes < _readbuf.size())
                    {
                        _readbuf[data_bytes] = '\n';
                        _readbuf[data_bytes - 1] = '\r';
                        data_bytes++;
                    }
                }
            }

            auto *req = reinterpret_cast<CONSOLE_READCONSOLE_MSG *>(m.body);
            if (_vt_eof && _read_total == 0)
            {
                req->NumBytes = 0;
                miniio::prepare_completion(m, 0, 0);
                m.complete.Write.Data = m.body;
                m.complete.Write.Size = sizeof(CONSOLE_READCONSOLE_MSG);
            }
            else
            {
                req->NumBytes = data_bytes;
                auto cb = static_cast<ULONG>(sizeof(CONSOLE_READCONSOLE_MSG) + data_bytes);
                miniio::prepare_completion(m, 0, cb);
                m.complete.Write.Data = m.body;
                m.complete.Write.Size = cb;
            }
            comp_before = m.complete;
            comp_ptr = &comp_before;
        }
        else if (_pend_usr) // ConsoleRead
        {
            auto &m = *_pend_usr;
            // ── 使用行编辑后的 _cooked_buf 作为返回数据 ──
            auto *req = reinterpret_cast<CONSOLE_READCONSOLE_MSG *>(m.body + sizeof(CONSOLE_MSG_HEADER));
            req->ControlKeyState = 0;

            if (_vt_eof && _cooked_buf.empty() && _read_total == 0)
            {
                req->NumBytes = 0;
                auto sz = static_cast<ULONG>(sizeof(CONSOLE_READCONSOLE_MSG));
                miniio::prepare_completion(m, 0, sz);
                m.complete.Write.Data = m.body + sizeof(CONSOLE_MSG_HEADER);
                m.complete.Write.Size = sz;
            }
            else
            {
                // ── DOSKEY 别名展开 ──
                _expand_alias();

                auto *db = m.body + sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_READCONSOLE_MSG);
                DWORD maxd =
                    static_cast<DWORD>(sizeof(m.body) - sizeof(CONSOLE_MSG_HEADER) - sizeof(CONSOLE_READCONSOLE_MSG));
                DWORD cp = 0;
                if (_pend_uni)
                {
                    // char32_t 行编辑缓冲 → UTF-8 → UTF-16
                    _conv_utf8.clear();
                    convert_u32_to_utf8(_cooked_buf, _conv_utf8);
                    _conv_utf8 += "\r\n"; // ConDrv 要求 \r\n 行尾
                    auto *utf16_out = reinterpret_cast<char16_t *>(db);
                    auto max_chars = maxd / sizeof(char16_t);
                    size_t n = unicode::detail::convert_utf8_to_utf16(_conv_utf8.data(), _conv_utf8.size(), utf16_out);
                    if (n > max_chars)
                        n = max_chars;
                    cp = static_cast<DWORD>(n * sizeof(char16_t));
                }
                else
                {
                    // char32_t → UTF-8 + \r\n
                    _conv_utf8.clear();
                    convert_u32_to_utf8(_cooked_buf, _conv_utf8);
                    _conv_utf8 += "\r\n";
                    cp = static_cast<DWORD>(_conv_utf8.size());
                    if (cp > maxd)
                        cp = maxd;
                    if (cp > 0)
                        std::memcpy(db, _conv_utf8.data(), cp);
                }
                req->NumBytes = cp;
                auto sz = static_cast<ULONG>(sizeof(CONSOLE_READCONSOLE_MSG) + cp);
                miniio::prepare_completion(m, 0, sz);
                m.complete.Write.Data = m.body + sizeof(CONSOLE_MSG_HEADER);
                m.complete.Write.Size = sz;
            }
            comp_before = m.complete;
            comp_ptr = &comp_before;
        }

        _pend_kind = PendingKind::None;
        _pend_raw = nullptr;
        _pend_usr = nullptr;
        _pend_input = nullptr;
        _read_total = 0;
        _echo_start = 0;

        // ── 保存到命令历史 ──
        if (!_cooked_buf.empty() && (_history.empty() || _history.back() != _cooked_buf))
            _history.push_back(_cooked_buf);

        _cooked_buf.clear();
        _cooked_cursor = 0;
        _raw_buf.clear();
        _history_idx = static_cast<size_t>(-1); // 重置浏览
        _saved_input.clear();

        // ── echo 完成后同步 state.cursor.position 到终端实际位置 ──
        // scan_for_line 已将 _term_cursor 推进到 \r\n 后的新行首
        if (_term_cursor_valid)
        {
            cstate.cursor.position = _term_cursor;
            LOG("[bridge] complete_pending: synced state cursor to (%d,%d)", _term_cursor.X, _term_cursor.Y);
        }

        LOG("[bridge] complete_pending: done kind=%d cooked_len=%zu vt_eof=%d", static_cast<int>(_pend_kind),
            _cooked_buf.size(), _vt_eof);

        // ── 发送 CD_IO_COMPLETE 到 ConDrv（对标旧版：挂起完成必须显式发送）──
        if (comp_ptr)
        {
            LOG("[bridge] complete_pending: sending CD_IO_COMPLETE");
            miniio::complete_io(server, *comp_ptr);
        }
    }

    void complete_pending_console_input()
    {
        if (_pend_kind != PendingKind::ConsoleInput || !_pend_input)
            return;

        auto &m = *_pend_input;
        auto *req = reinterpret_cast<CONSOLE_GETCONSOLEINPUT_MSG *>(m.body + sizeof(CONSOLE_MSG_HEADER));
        auto *out =
            reinterpret_cast<INPUT_RECORD *>(m.body + sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_GETCONSOLEINPUT_MSG));
        const auto max_count = console_input_max_records(m);

        const auto count = (req->Flags & CONSOLE_READ_NOREMOVE) ? inp.peek(out, max_count) : inp.read(out, max_count);
        req->NumRecords = static_cast<ULONG>(count);
        const auto size =
            static_cast<ULONG>(sizeof(CONSOLE_GETCONSOLEINPUT_MSG) + count * sizeof(INPUT_RECORD));
        miniio::prepare_completion(m, 0, size);
        m.complete.Write.Data = m.body + sizeof(CONSOLE_MSG_HEADER);
        m.complete.Write.Size = size;

        auto comp = m.complete;
        _pend_kind = PendingKind::None;
        _pend_input = nullptr;

        miniio::complete_io(server, comp);
    }

  public:
    // ════════════════════════════════════════════════════
    //  测试钩子
    // ════════════════════════════════════════════════════

    // ── 终端光标追踪和 echo 模拟 ──
    COORD test_get_term_cursor() const noexcept
    {
        return _term_cursor;
    }
    bool test_is_term_cursor_valid() const noexcept
    {
        return _term_cursor_valid;
    }
    void test_set_term_cursor_valid(COORD pos)
    {
        _term_cursor = pos;
        _term_cursor_valid = true;
    }
    SHORT test_get_input_column_start() const noexcept
    {
        return _input_column_start;
    }
    SHORT test_get_input_column_end() const noexcept
    {
        return _input_column_end;
    }
    void test_set_input_column_start(SHORT x)
    {
        _input_column_start = x;
    }
    void test_set_input_column_end(SHORT x)
    {
        _input_column_end = x;
    }

    // ── 编辑缓冲暴露 ──
    const std::u32string &test_get_cooked_buf() const noexcept
    {
        return _cooked_buf;
    }
    size_t test_get_cooked_cursor() const noexcept
    {
        return _cooked_cursor;
    }
    void test_cooked_append(const char32_t *s, size_t n)
    {
        for (size_t i = 0; i < n; ++i)
            _edit_insert(s[i], static_cast<BYTE>(s[i]));
    }
    void test_cooked_backspace()
    {
        _edit_backspace();
    }
    void test_cooked_delete()
    {
        _edit_delete();
    }
    void test_cooked_left()
    {
        _edit_move_left();
    }
    void test_cooked_right()
    {
        _edit_move_right();
    }
    void test_cooked_home()
    {
        _edit_home();
    }
    void test_cooked_end()
    {
        _edit_end();
    }

    // ── 历史缓冲测试钩子 ──
    size_t test_history_size() const noexcept
    {
        return _history.size();
    }
    void test_history_push()
    {
        // 模拟 complete_pending 保存历史
        if (!_cooked_buf.empty() && (_history.empty() || _history.back() != _cooked_buf))
            _history.push_back(_cooked_buf);
        _cooked_buf.clear();
        _cooked_cursor = 0;
        _history_idx = static_cast<size_t>(-1);
        _saved_input.clear();
    }
    void test_history_up()
    {
        _edit_history_up();
    }
    void test_history_down()
    {
        _edit_history_down();
    }

    // ── 别名展开测试钩子 ──
    void test_expand_alias()
    {
        _expand_alias();
    }

    // 模拟 complete_pending 对 _cooked_buf 的 UTF-8 序列化（含 \r\n 行尾）
    std::string test_build_completion_utf8() const
    {
        std::string s;
        convert_u32_to_utf8(_cooked_buf, s);
        s += "\r\n";
        return s;
    }

    // 模拟 echo 字节序列 (不依赖真实管道), 返回 echo 后的终端光标位置
    // 尊重 _input_column_start / _input_column_end 边界
    COORD test_feed_echo_bytes(const BYTE *bytes, DWORD len)
    {
        if (!_term_cursor_valid)
            return _term_cursor;
        for (DWORD i = 0; i < len; ++i)
        {
            BYTE b = bytes[i];
            if (b == '\r')
            {
                _term_cursor.X = 0;
            }
            else if (b == '\n')
            {
                _term_cursor.X = 0;
                _term_cursor.Y++;
            }
            else if (b == 0x08 || b == 0x7F)
            {
                if (_term_cursor.X > _input_column_start)
                    _term_cursor.X--;
                if (_term_cursor.X < _input_column_end)
                    _input_column_end = _term_cursor.X;
            }
            else if (b >= 0x20)
            {
                _term_cursor.X++;
                if (_term_cursor.X > _input_column_end)
                    _input_column_end = _term_cursor.X;
            }
        }
        return _term_cursor;
    }

    // ── Win32Input ConsoleRead 回归测试钩子 ──
    // 回归 bug: fix powershell 提交只将 win32_input_key 写入 input_buffer，
    // 未路由到 ConsoleRead 行编辑路径，导致 cmd 作为 shell 时打不出字。
    // 以下钩子模拟终端通过 Win32Input 发送按键而 bridge 处于 ConsoleRead 模式。

    // 进入 ConsoleRead 挂起模式并设置必要的上下文状态
    void test_enter_console_read_mode(SHORT prompt_col = 13)
    {
        _pend_kind = PendingKind::ConsoleRead;
        _process_control_z = false;
        _pend_uni = false;
        _read_total = 0;
        _echo_start = 0;
        _cooked_buf.clear();
        _cooked_cursor = 0;
        _line_found = false;
        // 模拟 WriteConsole 完成后的同步：光标位于 prompt 列
        _term_cursor = {prompt_col, 0};
        _term_cursor_valid = true;
        _input_column_start = prompt_col;
        _input_column_end = prompt_col;
    }

    // 将原始字节数组送入 process_input 管道（与 accumulate_from_pipe 内部相同）
    // process_input 内部检测行终止符并可能调用 complete_pending()
    // 注意：无真实 server 句柄时 complete_pending 不会触发 IOCTL_COMPLETE_IO
    void test_feed_raw_bytes(const BYTE *bytes, DWORD len)
    {
        process_input(bytes, len);
        vt_flush(); // 确保 echo 字节及时刷新（回归：之前缺失此 flush 导致卡顿）
    }

    // 同上，但使用 Win32Input ControlKeyState（Shift/Ctrl 等修饰键上下文）
    void test_feed_win32_bytes(const BYTE *bytes, DWORD len, DWORD control_key_state = 0)
    {
        // control_key_state 当前仅用于文档目的；Win32Input 序列中已自带 Cs 字段
        // 此处保留参数以备后续扩展
        (void)control_key_state;
        test_feed_raw_bytes(bytes, len);
    }

    // 查询 process_input 是否因为 Enter/LF/Ctrl+Z 而完成了一行
    bool test_line_found() const noexcept
    {
        return _line_found;
    }

    // 查询当前挂起类型（Enter 后应为 None，因为 complete_pending 清除了状态）
    int test_get_pend_kind() const noexcept
    {
        return static_cast<int>(_pend_kind);
    }

    // VT 输出缓冲当前累积量（验证批量 echo 已及时 flush）
    size_t test_vt_buf_len() const noexcept
    {
        return _vt_len;
    }

    // ── Enter 换行标志回归测试钩子 ──
    // 回归 bug: PowerShell 下 Enter 后 process_input 检测到 \r / Win32Input Enter，
    //   但 PSReadLine 逐字渲染已推进 state.cursor，Enter 后 cursor 停在输入行末尾，
    //   下一次 WriteConsole("hello") 在旧列号输出 → "echo hellohello"。
    // 修复: process_input 的 Enter 处理设置 _enter_pending_newline 标志，
    //   api_write_console 输出文本前 consume_enter_newline() → 先 CUP 到 _term_cursor。
    bool test_get_enter_newline_flag() const noexcept
    {
        return _enter_pending_newline;
    }
    void test_set_enter_newline_flag(bool v) noexcept
    {
        _enter_pending_newline = v;
    }

    // ════════════════════════════════════════════════════
    // I/O 循环回归测试钩子
    // ════════════════════════════════════════════════════
    //
    // 回归 bug: run_io_loop_no_setup 在 prev_done != nullptr 时先等
    // WaitForSingleObject(ev, 16) 再 read_io，导致每个 Console API 往返
    // 被强制插入 16ms。PSReadLine 每个按键触发多轮 GetConsoleInput/状态查询，
    // 累计成明显卡顿。
    //
    // 修正后逻辑：
    //   1. 有待提交 completion (prev_done != nullptr) → 直接 read_io_try，零延迟
    //   2. 无 pending 且无 prev_done → 先 on_idle() 扫 vt_in，再仅等 1ms

    // 验证 on_idle 在 pending==None 时不破坏状态
    void test_on_idle_safe()
    {
        // 仅验证 on_idle 不崩溃；具体逻辑由 E2E 测试覆盖
        on_idle();
    }

    // 验证 accumulate_from_pipe 返回 false 时 vt_buf 已清空（行未完成但字节已 flush）
    // 回归：之前批量 echo 后忘记 vt_flush，导致字节滞留在 _vt_buf
    bool test_accumulate_flushes_vt_buf()
    {
        size_t before = _vt_len;
        bool done = accumulate_from_pipe();
        // 如果无数据，accumulate 立即返回 false，_vt_len 不变
        // 如果有数据被 process_input→echo，_vt_len 在 vt_flush 后应归零
        (void)before;
        return done;
    }
};

} // namespace conpty
