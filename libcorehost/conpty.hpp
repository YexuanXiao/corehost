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

void conpty_entry(win32::handle server, win32::handle event, win32::handle condrv_input, win32::handle condrv_output,
                  win32::handle vt_in, win32::handle vt_out, win32::handle signal_pipe, short width, short height,
                  bool inherit_cursor, text_measurement_mode text_measurement, bool ambiguous_is_wide = false);

} // namespace conpty
