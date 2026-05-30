// ── conpty/conpty.cpp ─────────────────────────────
// ConPTY 会话装配。
//
// 功能分解：
// 1. 初始化 console_state、主/备用 screen_buffer 和 input_buffer。
// 2. 把 ConDrv 句柄、VT 输入输出管道和 API/router/bridge 连接成一条会话。
// 3. 可选启动 PtySignal 线程，接收 WT 的 resize/clear 控制消息。
// 4. 发送 Win32 Input Mode 初始化序列后进入 ConDrv I/O 循环。

#include "conpty.hpp"
#include <cstring>
#include <memory>
#include "win32/event.hpp"
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

void copy_process_list(io_state &io, pipe_bridge &bridge)
{
    // bridge.proc_list 是 io.process_list 的快照；长度由 process_count 限制在
    // 两个数组共同的 max_processes 范围内。
    bridge.proc_count = io.process_count;
    std::memcpy(bridge.proc_list, io.process_list, io.process_count * sizeof(DWORD));
}

void run_conpty_session(win32::handle server, win32::handle event, win32::handle condrv_input,
                        win32::handle condrv_output, win32::handle vt_in, win32::handle vt_out,
                        win32::handle signal_pipe, const conpty_session_config &config)
{
    // 此日志记录会话的外部输入：ConDrv server/event、VT 管道、信号管道和
    // config。后续代码只在本函数内展开这些输入，不再重新查询环境。
    LOG("conpty::run_conpty_session: s=%p vi=%p vo=%p ev=%p w=%d h=%d sig=%p ambi=%d pollVt=%d attachedPid=%lu",
        server.get(), vt_in.get(), vt_out.get(), event.get(), config.width, config.height, signal_pipe.get(),
        config.ambiguous_is_wide, config.poll_vt_input, config.attached_process_id);

    // ── Layer 4: 控制台状态 ──
    console_state state;

    // screen_buffer_size 是控制台 API 看到的主尺寸。config.width/height <= 0
    // 代表“未指定”，统一回退到 default_console_size，避免不同入口产生
    // 80x25/120x30 的分裂默认值。
    state.screen_buffer_size.X = config.width > 0 ? config.width : default_console_size.X;
    state.screen_buffer_size.Y = config.height > 0 ? config.height : default_console_size.Y;

    // current_window_size/max_window_size 当前与缓冲区等大；libcorehost 不实现
    // 传统 conhost 的独立滚动缓冲区窗口。
    state.current_window_size = state.screen_buffer_size;
    state.max_window_size = state.screen_buffer_size;

    // 会话初始光标总是 0-based (0,0)。inherit_cursor=true 时后面会通过 CPR
    // 覆盖这个值。
    state.cursor.position = {0, 0};

    // 文本测量策略写入 state，后续 api_handlers/vt_msg_dispatch 不再读取 config。
    state.text_measurement = config.text_measurement;
    state.ambiguous_is_wide = config.ambiguous_is_wide;

    // console_state 构造函数已经初始化过 tab stops；这里在尺寸确定后再调用
    // 一次，确保 resize 前后默认 tab 布局一致。
    state.init_tab_stops();

    // ── Layer 4: 屏幕缓冲区 ──
    // 主缓冲区尺寸必须等于 state.screen_buffer_size；Console API 默认读写它。
    screen_buffer sbuf(state.screen_buffer_size);

    // screen_buffer 构造时已填空格；clear 使用 state.default_attributes，
    // 让初始属性与 GetConsoleScreenBufferInfo 返回值一致。
    sbuf.clear(state.default_attributes);

    // ── Layer 4: 备用屏幕缓冲区 ──
    // 备用缓冲区服务 DECSET 1049。它和主缓冲区同尺寸，切换时由 api_router
    // 把 active buffer 重绘到终端。
    screen_buffer alt_sbuf(state.screen_buffer_size);
    alt_sbuf.clear(state.default_attributes);

    // ── Layer 4: 输入缓冲区 ──
    // input_buffer 持有 Console API 可见的 INPUT_RECORD 队列。init_event 必须
    // 在 API handler 使用前调用，否则 GetNumberOfConsoleInputEvents 无法等待。
    input_buffer ibuf;
    ibuf.init_event();

    // ── Layer 2: I/O 状态 ──
    // io_state 管理 ConDrv 连接对象和进程列表。condrv_input/output 可能已经
    // 由 deftermv2 headless 路径提前 accept；为空时首个 CONNECT 会创建它们。
    io_state io;
    io.set_server(server.view());
    io.condrv_input = std::move(condrv_input);
    io.condrv_output = std::move(condrv_output);

    // ── Layer 2: pipe bridge ──
    // pipe_bridge 是 VT 输入输出和 Console API 状态的交汇点。它直接引用
    // ibuf/state/sbuf，因此主缓冲区和 API 查询能看到同一份光标与文本状态。
    pipe_bridge bridge{ibuf, state, sbuf};

    // vt_in 是终端到 corehost 的输入/控制字节流；vt_out 是 corehost 到终端
    // 的 VT 输出字节流；server 用于 bridge 完成挂起 ConDrv I/O。
    bridge.vt_in = vt_in.view();
    bridge.vt_out = vt_out.view();
    bridge.server = server.view();

    // ── Layer 2: api router ──
    // api_router 把 USER_DEFINED Console API 分派给 api_handlers。传入主/备用
    // 两个缓冲区，是为了在 DECSET 1049 后让同一套 handler 操作 active buffer。
    api_router api{state, sbuf, alt_sbuf, ibuf, io, bridge};

    // ── Layer 1: message router ──
    // message_router 是 ConDrv Function 号的第一层分派：对象管理进 io_state，
    // raw I/O 进 pipe_bridge，Console API 进 api_router。
    message_router router{io, bridge, api};

    // ── PtySignal 信号线程 ──
    // signal_pipe 为空表示当前会话没有 WT 信号通道，resize/close 信号不会
    // 从 PtySignal 线程进入。
    win32::basic_thread sig_thread;
    win32::event signal_shutdown_event;
    if (signal_pipe.valid())
    {
        // signal_shutdown_event 由信号线程退出时置位。bridge 在等待 pending
        // VT 输入时检查它，避免 WT 已关闭后仍阻塞在输入等待路径。
        signal_shutdown_event = win32::event{win32::create_tag, true, false};
        bridge.set_signal_shutdown_event(signal_shutdown_event.view());

        // 信号线程需要自己的 event 句柄；主线程继续保留 signal_shutdown_event
        // 用于等待/查询。
        auto signal_thread_event = win32::event{win32::duplicate_handle(signal_shutdown_event.view())};

        // pty_signal_thread_params 里保存 state/sbuf 引用，因此线程必须在
        // run_conpty_session 返回前结束；basic_thread 析构负责等待线程。
        auto tp = std::make_unique<pty_signal_thread_params>(std::move(signal_pipe), std::move(signal_thread_event),
                                                             state, sbuf);
        sig_thread = win32::basic_thread{pty_signal_thread_proc, tp.release()};
        LOG("conpty::run_conpty_session: signal thread started shutdownEvent=%p", signal_shutdown_event.get());
    }

    win32::event vt_input_poll_event;
    if (config.poll_vt_input && !signal_pipe.valid())
    {
        // 没有 signal_pipe 时，bridge 没有可等待的关闭信号。这里给它一个永不
        // 主动置位的 event，使 wait_for_signal_shutdown_slice 退化为 16ms 轮询。
        vt_input_poll_event = win32::event{win32::create_tag, true, false};
        bridge.set_signal_shutdown_event(vt_input_poll_event.view());
    }

    // ── 继承光标位置（对标原始 VtIo::StartIfNeeded + WriteDSRCPR）──
    // 终端 CPR 应答会在主 I/O 循环的 on_idle() 中统一读取并处理。
    if (config.inherit_cursor)
    {
        // pending 标志让 bridge 在收到 CPR response 时把终端 1-based 坐标
        // 写回 state.cursor.position。
        bridge.set_pending_inherit_cursor();

        // DSR CPR 查询写入 vt_out 后必须立即 flush，否则 I/O 循环可能在收到
        // 应答前就开始处理依赖光标位置的输出。
        bridge.vt_write_dsr_cpr();
        bridge.vt_flush();
        LOG("conpty::run_conpty_session: inherit_cursor DSR CPR sent");
    }

    // ── 初始 VT 握手：通知终端进入 Win32 Input Mode ──
    // 对标原始 conhost VtIo::Start()。
    // 缺失 \x1b[?9001h → 终端不会发送键盘数据 → 打字不回显。
    // 9001 是 Windows Terminal 的 Win32 Input Mode 私有模式号。
    bridge.vt_append_str("\x1b[?9001h"sv);
    bridge.vt_flush();
    LOG("conpty::run_conpty_session: sent Win32Input init sequence");

    if (config.attached_process_id != 0)
    {
        // deftermv2 headless 路径在进入 libcorehost 前已经 accept 首个 CONNECT。
        // 该 pid 不会再经过 io_state::handle_connect，因此这里手动补进列表。
        io.add_process(config.attached_process_id);
        copy_process_list(io, bridge);
    }

    // ── 进入 I/O 循环 ──
    // router 持有所有分派入口；run_io_loop_no_setup 只负责 READ_IO 时序、
    // completion 提交和 pending 等待。
    LOG("conpty::run_conpty_session: entering io loop");
    conpty::run_io_loop_no_setup(server.view(), event.view(), router);
    LOG("conpty::run_conpty_session: loop returned");
}

void conpty_entry(win32::handle server, win32::handle event, win32::handle condrv_input, win32::handle condrv_output,
                  win32::handle vt_in, win32::handle vt_out, win32::handle signal_pipe, short width, short height,
                  bool inherit_cursor, text_measurement_mode text_measurement, bool ambiguous_is_wide)
{
    LOG("conpty::conpty_entry: s=%p vi=%p vo=%p ev=%p w=%d h=%d sig=%p ambi=%d", server.get(), vt_in.get(),
        vt_out.get(), event.get(), width, height, signal_pipe.get(), ambiguous_is_wide);

    // conpty_entry 是旧入口形态：会话策略以独立标量传入，实际执行路径只接受
    // conpty_session_config。这里不解释句柄所有权，只做参数规约，避免两个入口
    // 维护两套默认值。
    conpty_session_config config;

    // width/height <= 0 表示调用方没有给出有效字符尺寸。这里原样写入 config，
    // 由 run_conpty_session 统一回退到 default_console_size。
    config.width = width;
    config.height = height;

    // inherit_cursor=true 时会话启动后发送 DSR CPR，等待终端报告现有光标。
    // false 时从 (0,0) 开始模拟传统新控制台。
    config.inherit_cursor = inherit_cursor;

    // text_measurement 决定 screen_buffer 和 VT 文本推进的列宽计算规则；
    // ambiguous_is_wide 只在 wcswidth/graphemes 路径中影响 EAW=A 字符。
    config.text_measurement = text_measurement;
    config.ambiguous_is_wide = ambiguous_is_wide;

    // run_conpty_session 是唯一会实际组装状态机并进入 I/O 循环的入口。
    // conpty_entry 不再访问这些 moved-from 句柄。
    run_conpty_session(std::move(server), std::move(event), std::move(condrv_input), std::move(condrv_output),
                       std::move(vt_in), std::move(vt_out), std::move(signal_pipe), config);
}

} // namespace conpty
