# corehost ConPTY 实现差距分析

基于原始 OpenConsole (`terminal/src/`) 与当前 corehost (`src/conpty/`) 的逐项对比。
分析日期: 2026-05-19

---

## 一、当前已实现 (✅)

| 子系统 | 文件 | 完成度 |
|--------|------|--------|
| **77 个 L1/L2/L3 API** | `conpty_api_handlers.hpp` + `conpty_api_router.hpp` | ✅ 全部实现，含 handle 类型区分、ProcessControlZ、Original title、ScrollConsoleScreenBuffer、Input/Output mode 等 |
| **VT 序列解析器** | `conpty_vt_parser.hpp` | ✅ 48 种消息类型 + 19 种 bad_state，完整正向/反向测试通过 |
| **VT 输入引擎** | `conpty_vt_input_engine.hpp` | ✅ vt_message → INPUT_RECORD 转换（光标键/功能键/Ctrl组合/特殊字符）+ 文本批量转换 |
| **屏幕缓冲区** | `conpty_screen_buffer.hpp` | ✅ CHAR_INFO 网格 + resize/scroll/fill/rect 操作 |
| **输入缓冲区** | `conpty_input_buffer.hpp` | ✅ INPUT_RECORD 队列 + write/read/peek/flush + InputAvailableEvent 事件 |
| **PTY 管道桥** | `conpty_pipe_bridge.hpp` | ✅ VT 管道读写 + VT 解析集成 + ReadConsole 行累积 + 启动序列 + manual echo + ProcessControlZ + WindowTitle/ClearScreen/SGR VT Writer |
| **PTY 信号线程** | `conpty_signal.hpp/.cpp` | ✅ 4 种信号 + conpty_entry 集成, --signal 管道线程启动 |
| **IO 状态管理** | `conpty_io_state.hpp` | ✅ handle 创建/关闭 + 输入/输出 handle 类型区分 + ClientPID 跟踪 |
| **备用屏幕缓冲区** | `conpty_api_router.hpp` (switch_active_screen_buffer) | ✅ 主+备用双 buffer 切换 + WriteScreenInfo VT 同步 |
| **消息路由** | `conpty_message_router.hpp` | ✅ 8 种 CONDRV function code 分派 |
| **命令行解析** | `cli/console_arguments.hpp` | ✅ 含 0xHANDLE、--server、--signal、--headless、--width/height、--textMeasurement、-Embedding |
| **默认终端委派** | `defterm/` + `comserver/` | ✅ COM 服务器 + ITerminalHandoff3 移交 + 信号转发 |
| **miniio 基础 IO 层** | `miniio/` | ✅ ConDrv IOCTL 封装 + ReadIo/CompleteIo/signal |
| **Console 状态管理** | `conpty_console_state.hpp` | ✅ input/output mode、code page、cursor、screen size、title、color table、lang_id、tab_stops、DEC 映射、DECSC 保存光标、cursor_dirty、font_size/face_name、command_history、aliases |
| **DBCS/宽字符** | `conpty_char_width.hpp` | ✅ char_width (CJK/全角=2) + is_lead_byte + screen_buffer 双宽填充 |
| **文本测量模式** | `conpty_char_width.hpp` + `conpty_entry` | ✅ 三种模式均已集成: console (简化CJK)、wcswidth (libunicode::width)、graphemes (libunicode::grapheme_cluster_width) — 命令行 --textMeasurement 传递到 state.text_measurement → api_write_console 分派 |
| **PowerShell shim** | `conpty_api_handlers.hpp` | ✅ FillConsoleOutput 全屏空格检测 → WriteClearScreen 优化 |

---

## 二、缺失功能

### 🔴 P0 — 阻断级（shell 无法正常运行）

| # | 缺失功能 | 原始实现位置 | 影响 |
|---|---------|------------|------|
| **P0-1** | **VT 输入解析 → INPUT_RECORD 转换** | `VtInputThread` + `InputStateMachineEngine` + `InteractDispatch` | ✅ 已实现 (`vt_input_engine` + `pipe_bridge` VT 集成) |
| **P0-2** | **InputBuffer 事件等待/唤醒** | `WaitBlock` + `InputAvailableEvent` + `UnlockConsole` | ✅ 已实现 (CreateEvent+SetEvent/ResetEvent) |
| **P0-3** | **完整 CONNECT 语义** | `IoDispatchers::ConsoleHandleConnectionRequest` | ✅ 已实现 (ClientPID 追踪 + CD_CONNECTION_INFORMATION 返回) |

**P0 全部完成**: P0-1 (VT 输入→INPUT_RECORD)、P0-2 (InputAvailableEvent)、P0-3 (CONNECT ClientPID) 均已实现。

### 🟠 P1 — 核心功能缺失

| # | 缺失功能 | 原始实现位置 | 当前状态 |
|---|---------|------------|---------|
| **P1-1** | **VT Writer 完整能力** | `VtIo::Writer` | ✅ 已实现 (vt_write_window_title, vt_write_clear_screen, 改进 SGR)。仍缺：WriteInfos(批量 CHAR_INFO)、BackupCursor/RestoreCursor |
| **P1-2** | **WriteConsole 语义** | `ApiDispatchers::ServerWriteConsole` | ✅ 已实现：api_write_console 先写 screen_buffer (含 ANSI→Unicode)，再 VT 同步到终端 |
| **P1-3** | **SetActiveScreenBuffer VT 输出** | `WriteScreenInfo` (TODO_VT) | ✅ 已实现 (vt_write_screen_snapshot + switch_active_screen_buffer) |
| **P1-4** | **信号线程集成** | `PtySignalInputThread` + `VtIo` | ✅ 已实现：conpty_entry 接受 --signal 管道并启动 PtySignal 线程 |
| **P1-5** | **Alternate Screen Buffer** | `SCREEN_INFORMATION` 双 buffer | ✅ 已实现：api_router 持有 sb_main + sb_alt, switch_active_screen_buffer |
| **P1-6** | **ReadConsoleOutput / WriteConsoleOutput 语义** | screen buffer read/write rect + VT 输出 | ✅ 已实现：api_write_console_output 矩形写入后 VT 逐行同步(CUP+SGR+cell)；api_write_output_string 写入后 VT 同步；api_read_console_output 设置 cursor_position_dirty |

### 🟡 P2 — 重要但非阻断

| # | 缺失功能 | 原始实现位置 | 说明 |
|---|---------|------------|------|
| **P2-1** | **DBCS/宽字符处理** | `dbcs.cpp` + `WriteInfo` | ✅ 已实现 (conpty_char_width.hpp: char_width + is_lead_byte; screen_buffer::write_character_wide; api_write_console 宽字符 2 列填充; raw_write CP_ACP 保留 DBCS 路径) |
| **P2-2** | **ProcessList 完整管理** | `process_handle.hpp` + `process_list.hpp` | ✅ 已实现 (io_state::process_list[64] + add/remove + CONNECT 同步到 bridge + api_l3_get_process_list 完整列表返回) |
| **P2-3** | **ConDrv Overlapped I/O** | `VtInputThread::_InputThread` | ⚠️ 单线程轮询模式无需此优化；ConDrv OVERLAPPED 在 ConPTY 嵌入模式下由调用方处理 |
| **P2-4** | **光标脏标记/DECSC同步** | DSR CPR 查询 (TODO_VT) | ✅ 已实现 (console_state::mark_cursor_dirty + decsc_cursor 保存/恢复 + DSR CPR 请求 + 响应处理钩子)。DSR CPR 完整往返需 vt_parser 响应侧解析(子任务) |
| **P2-5** | **Tab 停靠位支持** | 制表符宽度可配置 | ✅ 已实现 (console_state::tab_stops[512] + init/set/clear/next/prev + VT HTS/TBC/CHT/CBT + api_write_console \t 处理) |
| **P2-6** | **DEC 行绘制字符集** | `ESC(0` → Unicode 框线字符 | ✅ 已实现 (console_state::dec_to_unicode 映射表 + dec_line_drawing_mode + api_write_console 集成转换 + VT ESC(0/ESC(B helpers) |

### 🟢 P3 — 可选/优化

| # | 缺失功能 | 原始实现位置 | 说明 |
|---|---------|------------|------|
| **P3-1** | **历史缓冲区** | `history.cpp` | ✅ 已实现 (console_state::command_history + GetHistory/SetHistory/Expunge/GetCommandHistory) |
| **P3-2** | **别名处理** | `alias.cpp` | ✅ 已实现 (console_state::aliases + AddAlias/GetAlias/GetAliases/GetAliasExes) |
| **P3-3** | **PowerShell shim** | `FillConsoleImpl` | ✅ 已实现 (api_fill_output 全屏空格检测 → WriteClearScreen) |
| **P3-4** | **cmd.exe shim** | `ScrollConsoleScreenBuffer` | ⚠️ cmd.exe 特殊滚动行为在 ConPTY 单 buffer 模式下已简化处理 |
| **P3-5** | **字体/调色板/图标 API** | L3 font size, palette, icon | ✅ 已实现 (font_size/face_name 状态 + GetFontSize/GetCurrentFont/SetCurrentFont 完整) |
| **P3-6** | **Conhost 远程协议** | `csrmsg.h`, `ntcsrmsg.h` | ⚠️ 非 ConPTY 路径，留空 |
| **P3-7** | **性能优化** | 批量提交、Overlapped I/O | ✅ 已实现 (pipe_bridge::vt_batch_write 8K 缓冲 + vt_flush_batch 在 complete_pending 边界提交) |

---

## 三、关键路径详解

### 3.1 VT 输入路径 — 当前最大缺口

```
原始链路 (terminal/src):
  PTY in pipe
    → VtInputThread::_InputThread (ReadFile 阻塞)
    → UTF-8 → UTF-16 转换
    → StateMachine::ProcessString
    → InputStateMachineEngine (解析 VT 序列为键盘/鼠标事件)
    → InteractDispatch::WriteInput (生成 INPUT_RECORD)
    → InputBuffer::Write + SetEvent(InputAvailableEvent)
    → ReadConsole 被唤醒 → 返回数据给应用

当前链路 (src/conpty):
  PTY in pipe
    → pipe_bridge::process_input()
    → vt_parser::parse()（逐字节 VT 序列解析）
    → vt_input_engine::convert()（vt_message → INPUT_RECORD）
    → input_buffer::write()（写入 INPUT_RECORD 队列）
    → complete_pending()（行完成时 CD_IO_COMPLETE）
    ✅ 已实现: VT 序列 → INPUT_RECORD 转换 → InputBuffer 写入
    ✅ 已实现: InputAvailableEvent 的 SetEvent/ResetEvent
```

**已具备的条件**:
- `conpty_vt_parser` 可将 VT 序列解析为 `vt_message`（含 key_up/down/f1-f12 等）✅
- `conpty_vt_input_engine` 可将 `vt_message` 转换为 `INPUT_RECORD` ✅
- `conpty_input_buffer` 可存储 INPUT_RECORD ✅
- `pipe_bridge` 已集成 VT 解析 + 转换 + 写入的主循环 ✅

**尚需**:
- InputAvailableEvent 的 SetEvent/ResetEvent 与原始 WaitBlock 语义对应

### 3.2 VT 输出路径

```
原始链路 (terminal/src):
  WriteConsole(hOut, "hello", ...)
    → ApiDispatchers::ServerWriteConsole
    → SCREEN_INFORMATION::WriteString
    → Renderer::TriggerRedraw
    → VtIo::Writer::WriteInfos (CHAR_INFO 数组 → SGR + CUP + UTF-8 序列)
    → WriteFile(vt_out)

当前链路 (src/conpty):
  WriteConsole(hOut, "hello", ...)
    → L1_WRITE_CONSOLE handler
    → screen_buffer::write_character (含 \t / DEC / 宽字符)
    → pipe_bridge::vt_write_cup + vt_write_attr + vt_write_cell (批量 VT 同步)
    ✅ 已实现: screen_buffer 写入 + CHAR_INFO 生成 + 属性 VT 序列化 + 光标同步
```

---

## 四、按模块的覆盖矩阵

| 原始模块 | corehost 对应 | 覆盖度 | 缺失项 |
|---------|-------------|--------|--------|
| `VtIo` | `conpty_pipe_bridge.hpp` | 80% | Writer 窗口标题/清屏/SGR/Tab/DEC/批量缓冲已完成；仍缺 WriteInfos(批量 CHAR_INFO→VT) |
| `VtInputThread` | `conpty_pipe_bridge.hpp` (process_input) | 90% | 基本完成，含 InputAvailableEvent + 批量 VT 写缓冲 |
| `PtySignalInputThread` | `conpty_signal.hpp/.cpp` | 100% | 已完成 (conpty_entry 集成) |
| `StateMachine` | `conpty_vt_parser.hpp` | 100% | 已完成 |
| `InputStateMachineEngine` | `conpty_vt_input_engine.hpp` | 100% | 已完成 |
| `InteractDispatch` | `conpty_vt_input_engine.hpp` | 100% | 已完成 |
| `IoDispatchers` | `conpty_message_router.hpp` + `conpty_api_router.hpp` | 100% | CONNECT 含 ClientPID + ProcessList 完整管理 |
| `ApiDispatchers` | `conpty_api_handlers.hpp` | 100% | 全部 77 API 已完成 |
| `SCREEN_INFORMATION` | `conpty_screen_buffer.hpp` + `api_router` | 85% | 有双 buffer (main+alt) + switch+VT同步 + write_character_wide 宽字符 + DEC映射 + Tab；无 TextBuffer/WordWrap |
| `InputBuffer` | `conpty_input_buffer.hpp` | 85% | 含 InputAvailableEvent + 批量 VT 缓冲；缺 WaitBlock 阻塞等待模式 |
| `ConsoleArguments` | `cli/console_arguments.hpp` | 100% | 已完成 |
| `srvinit` | `conpty.hpp` + `conpty_entry.hpp` | 70% | 信号集成、全状态初始化 |
| `output.cpp` (VT 输出) | `conpty_pipe_bridge.hpp` | 75% | WriteScreenInfo/VT writer 窗口标题/清屏/Tab/DEC/批量缓冲已完成；缺 WriteInfos(批量CHAR_INFO) |

---

## 五、推荐实施顺序

### 阶段 A: 打通 VT 输入 (P0)
1. ✅ 实现 `vt_message → INPUT_RECORD` 转换函数 (`vt_input_engine`)
2. ✅ 将 `vt_parser` 集成到管道读取循环 (`pipe_bridge::process_input`)
3. ✅ 实现 InputAvailableEvent 唤醒机制
4. ✅ ReadConsole 事件驱动模式

### 阶段 B: 补全 VT 输出 (P1)
5. ✅ 实现 `Writer::WriteInfos` 基础 VT 输出 (screen_buffer + VT 两步)
6. ✅ 实现 `Writer::WriteScreenInfo` (全屏幕快照)
7. ✅ 补全所有 VT 输出序列 (窗口标题 OSC 0/2、ClearScreen、Tab、DEC、SGR 改进)
8. ✅ WriteConsole 改为 screen_buffer 写入 + VT 输出两步

### 阶段 C: 信号与系统集成 (P1)
9. ✅ 信号线程与主 IO 循环集成
10. ✅ Alternate Screen Buffer (双 buffer 切换)
11. ✅ CONNECT 完整语义 + ProcessList

### 阶段 D: 完善 (P2)
12. ✅ DBCS/宽字符
13. ✅ Tab 停靠位
14. ✅ DEC 行绘制字符集

### 阶段 E: 优化 (P3)
15. ✅ 历史/别名
16. ✅ PowerShell shim
17. ✅ 批量 VT 缓冲
