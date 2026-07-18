// ── conpty/conpty.hpp ─────────────────────────────
// ConPTY 入口 (char32_t 版本): 组装所有模块并进入 I/O 循环
//
// 实现见 conpty.cpp
#pragma once
#include <windows.h>
#include "win32/handle.hpp"
#include "text_measurement_mode.hpp"

namespace corehost::conpty
{

struct conpty_session_config
{
    // 0 表示调用方没有提供有效尺寸，run_conpty_session 使用 default_console_size。
    // 正数表示初始字符列/行数。
    short width = 0;
    short height = 0;

    // true 时启动后向终端查询 CPR，并用应答初始化控制台光标。
    bool inherit_cursor = false;

    // 控制 char32_t 文本如何换算为终端列宽。
    text_measurement_mode text_measurement = text_measurement_mode::graphemes;

    // true 时 East Asian Width=A 的字符按 2 列计算。
    bool ambiguous_is_wide = false;

    // 0 表示没有预附加进程；非 0 会预填进程列表，供 GetConsoleProcessList 返回。
    DWORD attached_process_id = 0;
};

// server: ConDrv \Server 句柄，READ_IO/COMPLETE_IO 和创建客户端 \Input/\Output 都通过它完成。
// event: ConDrv InputAvailableEvent；corehost 不把它当作键盘输入事件，只用它判断
//        completion 提交前是否需要先刷新 VT 输出。
// condrv_input/condrv_output: 已 accept 的 \Input/\Output 连接句柄。defterm
//        headless 路径可能提前填入；普通 conpty/comserver 路径传空句柄，让首个 CONNECT 创建。
// vt_in: 终端到 corehost 的 UTF-8/VT 输入流，包含键盘输入、CPR、resize 等终端回应。
// vt_out: corehost 到终端的 UTF-8/VT 输出流，所有 Console API 输出最终写到这里。
// signal_pipe: WT PtySignal 控制管道；空句柄表示没有 resize/clear/close 通知通道。
// config: 会话策略值，只在组装状态机时读取，之后具体模块从 console_state/bridge
//         中读取派生后的运行状态。
void run_conpty_session(win32::handle_view server, win32::handle_view event, win32::handle condrv_input,
                        win32::handle condrv_output, win32::handle_view vt_in, win32::handle_view vt_out,
                        win32::handle signal_pipe, const conpty_session_config &config);

// conpty_entry 是兼容旧调用点的薄入口；它把离散参数打包成
// conpty_session_config 后交给 run_conpty_session。
void conpty_entry(win32::handle_view server, win32::handle_view event, win32::handle condrv_input,
                  win32::handle condrv_output, win32::handle_view vt_in, win32::handle_view vt_out,
                  win32::handle signal_pipe, short width, short height, bool inherit_cursor,
                  text_measurement_mode text_measurement, bool ambiguous_is_wide = false);

} // namespace corehost::conpty
