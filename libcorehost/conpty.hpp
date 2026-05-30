// ── conpty/conpty.hpp ─────────────────────────────
// ConPTY 入口 (char32_t 版本): 组装所有模块并进入 I/O 循环
//
// 实现见 conpty.cpp
#pragma once
#include <windows.h>
#include "win32/handle.hpp"
#include "text_measurement_mode.hpp"

namespace conpty
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

    // true 表示 vt_in 可能是不会发信号的普通/兜底输入管道，I/O 循环需要轮询。
    bool poll_vt_input = false;

    // 0 表示没有预附加进程；非 0 会预填进程列表，供 GetConsoleProcessList 返回。
    DWORD attached_process_id = 0;
};

void run_conpty_session(win32::handle server, win32::handle event, win32::handle condrv_input,
                        win32::handle condrv_output, win32::handle vt_in, win32::handle vt_out,
                        win32::handle signal_pipe, const conpty_session_config &config);

// conpty_entry 是兼容旧调用点的薄入口；它把离散参数打包成
// conpty_session_config 后交给 run_conpty_session。
void conpty_entry(win32::handle server, win32::handle event, win32::handle condrv_input, win32::handle condrv_output,
                  win32::handle vt_in, win32::handle vt_out, win32::handle signal_pipe, short width, short height,
                  bool inherit_cursor, text_measurement_mode text_measurement, bool ambiguous_is_wide = false);

} // namespace conpty
