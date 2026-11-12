// ── conpty/vt_input_state.hpp ─────────────────────────────
// 输入线程与会话锁共享的输入状态（见文件末尾的所有权说明）。
//
// A4 起，vt_in 的读取、UTF-8 解码、VT 解析、键盘事件生成与 cooked 行编辑
// 主要由输入线程完成（见 doc/design/0004）。输入线程独占 vt_in 的读取；
// 但主线程在处理新的 ReadConsole/RawRead 时，会在会话锁内消费"上一批次的
// 残留字节"（pipe_bridge::consume_queued_vt_input）。因此这些状态不是
// 无锁的线程私有数据，而是由会话锁串行访问的会话状态。
//
// pipe_bridge 仍保留原有的成员名，但它们是绑定到这里的引用别名：历史代码
// 一处不用改，状态的实际所有者变成了 vt_input_thread 持有的对象。
#pragma once
#include <windows.h>
#include <array>
#include <cstddef>
#include <string>
#include "condrv_io.hpp"
#include "vt_parser.hpp"
#include "input_engine.hpp"
#include "char_convert.hpp"
#include "raw_byte_allocator.hpp"
#include "deque.hpp"

namespace corehost::conpty
{

struct vt_input_state
{
    // ── 原始字节与残留 ───────────────────────────────
    // _readbuf 保存本轮 pending 读取已经从 vt_in 取得的原始字节；RawRead
    // completion 直接从这里返回，ReadConsole 则同时把它解码进 cooked 缓冲。
    std::array<char8_t, sizeof(condrv_io::io_msg::body)> readbuf{};
    // readbuf 中有效字节数，范围 0..readbuf.size()。
    DWORD read_total = 0;
    // true 表示 process_input 已经遇到行终止符或 Ctrl+Z 并完成当前 pending 读。
    bool line_found = false;
    // 上一批次以 CR 结尾：下一批次首个 LF 属于同一条 CRLF，应被吞掉而不是
    // 当成新的空行终止（CRLF 跨 ReadFile 边界）。
    bool swallow_lf_after_cr = false;
    // 没有 pending 时提前到达的 vt_in 字节，或当前批次完成一行后剩余的尾部。
    bizwen::deque<char8_t> queued_input;

    // ── 解码与解析 ───────────────────────────────────
    // 解码后的 UTF-32 原文缓冲。vt_parser 把当前消息的原文写到这里，并让
    // vt_message::text/title 指向其中的切片；消费消息并 reset 后才能复用。
    raw_u32_buffer input_raw_buf;
    // VT 输入解析器。状态必须跨 ReadFile 边界保留。
    vt_parser input_parser{input_raw_buf};
    // 键盘修饰键状态与 VT 消息到 INPUT_RECORD 的转换。
    vt_input_engine engine;
    // 流式 UTF-8 解码器，保留跨 ReadFile 边界的半个多字节序列。
    utf8_stream_decoder utf8_decoder;

    // ── cooked 行编辑 ────────────────────────────────
    // 当前 cooked ReadConsole 行编辑文本，只包含可返回给控制台程序的地面态
    // 输入字符，不包含 ESC/CSI/OSC 控制序列原文。
    std::u32string cooked_buf;
    // 编辑光标在 cooked_buf 中的位置，范围 0..cooked_buf.size()。
    size_t cooked_cursor = 0;
};

} // namespace corehost::conpty
