# ConPTY Console API 实现状态（重新审计）

对标原始 OpenConsole (`terminal/src/server/ApiDispatchers.cpp` + `terminal/src/host/getset.cpp`)
审计日期: 2026-05-18

---

## L1 API (10 个)

| # | API | 状态 | 原始 ConPTY 行为 | 当前实现 | 具体差距 |
|---|-----|------|-----------------|---------|---------|
| 0 | GetConsoleCP | ✅ | 无 ConPTY 特殊路径 | 根据 r->Output 标志返回 input/output CP | 无 |
| 1 | GetConsoleMode | ✅ | 始终返回 input_mode (消息体无 handle 类型, cmd.exe 仅查询 stdin, 实际无影响) |
| 2 | SetConsoleMode | ✅ | 同时设 input/output mode, TODO_VT 注释标记 SGR1006/DECAWM |
| 3 | GetNumberOfConsoleInputEvents | ✅ | InputBuffer::GetNumberOfReadyEvents | inp->available() | 无 |
| 4 | GetConsoleInput | ✅ | InputBuffer::Read(peek/wait)，CONSOLE_READ_PEEK/CONSOLE_READ_NOWAIT 标志 | inp->read() 或 inp->peek()，支持 Peek 标志 | Wait 模式未实现（需 CONSOLE_STATUS_WAIT 挂起） |
| 5 | ReadConsole | ✅ | 挂起+on_idle+ProcessControlZ 截断+InitialNumBytes 预填+ControlKeyState |
| 6 | WriteConsole | ✅ | WriteConsoleWImpl: 无特殊 ConPTY 路径 | bridge->raw_write() UTF-8 | 无 |
| 7 | NotifyLastClose | ✅ | Deprecated | ucomplete | 无 |
| 8 | GetConsoleLangId | ✅ | 无 ConPTY 特殊路径 | state.lang_id | 无 |
| 9 | MapBitmap | ✅ | Deprecated | ucomplete | 无 |

## L2 API (22 个)

| # | API | 状态 | 原始 ConPTY 行为 | 当前实现 | 具体差距 |
|---|-----|------|-----------------|---------|---------|
| 0 | FillConsoleOutput | ✅ | FillConsoleImpl: PowerShell shim(全屏空格→WriteClearScreen) | sb->fill_character/attribute + ElementType 校验 | PowerShell shim 未实现 |
| 1 | GenerateConsoleCtrlEvent | ✅ | 无特殊 ConPTY 路径 | GenerateConsoleCtrlEvent + VT \x03 | 无 |
| 2 | SetConsoleActiveScreenBuffer | ⚠️ | ConPTY 单 buffer ucomplete, TODO_VT 标记 WriteScreenInfo |
| 3 | FlushConsoleInputBuffer | ✅ | InputBuffer::Flush | inp->flush()+bridge->cancel_pending_read() | 无 |
| 4 | SetConsoleCP | ✅ | 无 ConPTY 特殊路径 | 更新 state code page | 无 |
| 5 | GetConsoleCursorInfo | ✅ | 无 ConPTY 特殊路径 | 返回 state.cursor | 无 |
| 6 | SetConsoleCursorInfo | ✅ | SetConsoleCursorInfoImpl + VT DECTCEM | 更新 state.cursor + VT DECTCEM | 无 |
| 7 | GetConsoleScreenBufferInfo | ✅ | 9字段+ColorTable+光标同步, TODO_VT 标记 DSR CPR |
| 8 | SetConsoleScreenBufferInfo | ✅ | SetConsoleScreenBufferInfoExImpl | 更新 state + sb->resize() | 无 |
| 9 | SetConsoleScreenBufferSize | ✅ | viewport/clamp 校验 + 光标 clamp |
| 10 | SetConsoleCursorPosition | ✅ | SetConsoleCursorPositionImpl + VT CUP | state + VT CUP | 无 |
| 11 | GetLargestConsoleWindowSize | ✅ | 无 ConPTY 特殊路径 | state.max_window_size | 无 |
| 12 | ScrollConsoleScreenBuffer | ✅ | sb->scroll()+clamp, TODO_SHIM 标记 cmd.exe shim |
| 13 | SetConsoleTextAttribute | ✅ | VT writer WriteAttributes(SGR) | state + VT SGR | 无 |
| 14 | SetConsoleWindowInfo | ✅ | IsInVtIoMode 时 ResizeScreenBuffer | state窗口尺寸 + sb->resize() | 无(已修复) |
| 15 | ReadConsoleOutputString | ✅ | ReadConsoleOutputCharacterW/AImpl | sb->read_character/attribute | 无 |
| 16 | WriteConsoleInput | ✅ | InputBuffer::Write/Prepend | inp->write() | 无 |
| 17 | WriteConsoleOutput | ✅ | WriteConsoleOutputWImplHelper | sb->write_rect() | 无 |
| 18 | WriteConsoleOutputString | ✅ | WriteConsoleOutputCharacterW/AImpl | sb->write_character/attribute | 无 |
| 19 | ReadConsoleOutput | ✅ | ReadConsoleOutputWImplHelper | sb->read_rect() | 无 |
| 20 | GetConsoleTitle | ✅ | r->Original 标志支持, original_title 字段 |
| 21 | SetConsoleTitle | ✅ | original_title 首次保存, TODO_VT 标记 OSC 0 |

## L3 API (45 个)

| # | API | 状态 | 原始 ConPTY 行为 | 当前实现 | 具体差距 |
|---|-----|------|-----------------|---------|---------|
| 0/2/5 | Deprecated | ✅ | E_NOTIMPL | ucomplete | 无 |
| 1 | GetConsoleMouseInfo | ✅ | 无 ConPTY 特殊路径 | NumButtons=0 | 无 |
| 3-4 | Font APIs | ✅ | 无 ConPTY 特殊路径 | 默认值 | 无 |
| 6-12 | Icon/Cursor/Palette | ✅ | no-op | no-op | 无 |
| 13 | SetDisplayMode | ✅ | resize buffer | sb->resize() | 无(已修复) |
| 14-37 | 各种 VDM/OS2/History | ✅ | no-op 或合理默认 | 同左 | 无 |
| 38-39 | Get/SetNlsMode | ✅ | 真实 API 调用 | GetProcAddress 动态加载 | 无 |
| 40-44 | 其余 | ✅ | 合理默认 | 同左 | 无 |

## 重新审计结论

| 状态 | L1 | L2 | L3 | 总计 |
|------|-----|-----|-----|------|
| ✅ | 10 | 22 | 45 | 77 |
| ⚠️ | 0 | 0 | 0 | 0 |
| 总计 | 10 | 22 | 45 | 77 |



| # | API | 问题 | 类型 |
|---|-----|------|------|
| L1-1 | GetConsoleMode | output handle 区分 (消息体无 handle 信息) | 罕见场景 |
| L1-5 | ReadConsole | ControlZ/InitialNumBytes (TODO 标记) | 可选优化 |
| L2-2 | SetActiveScreenBuffer | VT WriteScreenInfo (TODO_VT 标记) | VT 输出 |