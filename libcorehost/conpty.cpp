// ── conpty/conpty.cpp ─────────────────────────────
// ConPTY 入口实现: 组装所有模块并进入 I/O 循环

#include "conpty.hpp"
#include <memory>
#include "win32/thread.hpp"
#include "io_loop.hpp"
#include "console_state.hpp"
#include "screen_buffer.hpp"
#include "input_buffer.hpp"
#include "io_state.hpp"
#include "pipe_bridge.hpp"
#include "api_router.hpp"
#include "api_handlers.hpp"
#include "message_router.hpp"
#include "signal.hpp"
#include "utility/log.hpp"
#include "default_console_size.hpp"

namespace conpty
{

void conpty_entry(win32::handle server, win32::handle event, win32::handle condrv_input, win32::handle condrv_output,
                  win32::handle vt_in, win32::handle vt_out, win32::handle signal_pipe, short width, short height,
                  bool inherit_cursor, text_measurement_mode text_measurement, bool ambiguous_is_wide)
{
    LOG("conpty::conpty_entry: s=%p vi=%p vo=%p ev=%p w=%d h=%d sig=%p ambi=%d", server.get(), vt_in.get(),
        vt_out.get(), event.get(), width, height, signal_pipe.get(), ambiguous_is_wide);

    // ── Layer 4: 控制台状态 ──
    console_state state;
    state.screen_buffer_size.X = width > 0 ? width : default_console_size.X;
    state.screen_buffer_size.Y = height > 0 ? height : default_console_size.Y;
    state.current_window_size = state.screen_buffer_size;
    state.max_window_size = state.screen_buffer_size;
    state.cursor.position = {0, 0};
    state.text_measurement = text_measurement;
    state.ambiguous_is_wide = ambiguous_is_wide;
    state.init_tab_stops();

    // ── Layer 4: 屏幕缓冲区 ──
    screen_buffer sbuf(state.screen_buffer_size);
    sbuf.clear(state.default_attributes);

    // ── Layer 4: 备用屏幕缓冲区 ──
    screen_buffer alt_sbuf(state.screen_buffer_size);
    alt_sbuf.clear(state.default_attributes);

    // ── Layer 4: 输入缓冲区 ──
    input_buffer ibuf;
    ibuf.init_event();

    // ── Layer 2: I/O 状态 ──
    io_state io;
    io.set_server(server.view());
    io.condrv_input = std::move(condrv_input);
    io.condrv_output = std::move(condrv_output);

    // ── Layer 2: pipe bridge ──
    pipe_bridge bridge{ibuf, state, sbuf};
    bridge.vt_in = vt_in.view();
    bridge.vt_out = vt_out.view();
    bridge.server = server.view();

    // ── Layer 2: api router ──
    api_router api{state, sbuf, alt_sbuf, ibuf, io, bridge};

    // ── Layer 1: message router ──
    message_router router{io, bridge, api};

    // ── PtySignal 信号线程 ──
    win32::basic_thread sig_thread;
    if (signal_pipe.valid())
    {
        auto tp = std::make_unique<pty_signal_thread_params>(
            std::move(signal_pipe), win32::duplicate_handle(vt_in.view()), bridge.pipe_broken_flag(), state, sbuf);
        sig_thread = win32::basic_thread{pty_signal_thread_proc, tp.release()};
        LOG("conpty::conpty_entry: signal thread started");
    }

    // ── 继承光标位置（对标原始 VtIo::StartIfNeeded + WriteDSRCPR）──
    // 终端 CPR 应答会在主 I/O 循环的 on_idle() 中统一读取并处理。
    if (inherit_cursor && bridge.vt_out.valid())
    {
        bridge.set_pending_inherit_cursor();
        bridge.vt_write_dsr_cpr();
        bridge.vt_flush();
        LOG("conpty::conpty_entry: inherit_cursor DSR CPR sent");
    }

    // ── 初始 VT 握手：通知终端进入 Win32 Input Mode ──
    // 对标原始 conhost VtIo::Start()。
    // 缺失 \x1b[?9001h → 终端不会发送键盘数据 → 打字不回显。
    if (bridge.vt_out.valid())
    {
        bridge.vt_append_str("\x1b[?9001h");
        bridge.vt_flush();
        LOG("conpty::conpty_entry: sent Win32Input init sequence");
    }

    // ── 进入 I/O 循环 ──
    LOG("conpty::conpty_entry: entering io loop");
    conpty::run_io_loop_no_setup(server.view(), event.view(), router);
    LOG("conpty::conpty_entry: loop returned");
}

} // namespace conpty
