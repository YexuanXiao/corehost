# corehost 字符串数据流与编码转换全景

> 从用户程序到终端 GUI，穿越 corehost 的完整步骤。
> 分析日期: 2026-05-19

---

## 一、整体架构概览

```
┌──────────────┐     ConDrv IOCTL     ┌──────────────┐    VT (UTF-8)    ┌──────────────┐
│  用户程序     │ ←─────────────────→ │   corehost   │ ←─────────────→ │  WT 终端      │
│  (cmd.exe)   │   CD_IO_DESCRIPTOR   │              │   PTY pipe      │  (GUI)        │
└──────────────┘                      └──────────────┘                  └──────────────┘
   ↓ WriteConsole                        ↓ api_write_console               ↓ 渲染 VT
   ↓ ReadConsole                         ↓ api_read_console                ↓ 用户键盘输入
```

corehost 是中间层：左端对接 ConDrv 内核驱动（Windows 控制台协议），右端对接 PTY 伪终端管道（VT 序列）。

---

## 二、路径 1: WriteConsole — 用户程序输出到终端

### 2.1 调用链

```
cmd.exe 调用 WriteConsoleW(hOut, L"hello", ...)
  │
  ├─ [1] user32!WriteConsoleW → CSRSS → ConDrv → CD_IO_DESCRIPTOR
  │      Function = CONSOLE_IO_USER_DEFINED (0x07)
  │      ApiNumber = L1=6 (WriteConsole)
  │      InputSize = sizeof(CONSOLE_MSG_HEADER) + sizeof(CONSOLE_WRITECONSOLE_MSG) + string_bytes
  │      Body = CONSOLE_MSG_HEADER + CONSOLE_WRITECONSOLE_MSG{Unicode, NumBytes} + string_data
  │
  ├─ [2] miniio::run_io_loop → read_io(prev, &msg)
  │      → conpty_message_router::dispatch(msg) → case CONSOLE_IO_USER_DEFINED
  │      → api_router::handle_user_defined(msg) → dispatch_L1 → case 6
  │
  └─ [3] api_write_console(msg, state, sb, bridge)
           │
           ├── 判断 req->Unicode 标志
           │    ├─ Unicode=1 → 直接使用 UTF-16 LE 数据 (wchar_t[])
           │    └─ Unicode=0 → [编码转换 A] MultiByteToWideChar(state.output_code_page)
           │                      ANSI/OEM → UTF-16 LE
           │
           ├── Step 1: 写入 screen_buffer (CHAR_INFO 网格)
           │    遍历每个 wchar_t:
           │    ├─ DEC 行绘制模式? → dec_to_unicode() 映射
           │    ├─ \t? → next_tab_stop() 计算空格填充
           │    ├─ 宽字符 (cw==2)? → 主格 + 后随 0x0000 (COMMON_LVB_TRAILING_BYTE)
           │    └─ 普通字符 → sb->write_character(pos, &ch, 1)
           │    更新 state.cursor.position
           │
           └── Step 2: VT 同步到终端
                为每个字符调用 bridge->vt_write_cell(wch)
                │
                └── [编码转换 B] WideCharToMultiByte(CP_UTF8, wch) → UTF-8
                       → bridge->vt_write(utf8_bytes) → WriteFile(vt_out)
```

### 2.2 编码转换点

| 编号 | 位置 | 源编码 | 目标编码 | API |
|------|------|--------|---------|-----|
| **A** | `api_write_console` L193-211 | ANSI (CP_ACP/OEMCP) | UTF-16 LE | `MultiByteToWideChar` |
| **B** | `vt_write_cell` L332 | UTF-16 LE (wchar_t) | UTF-8 | `WideCharToMultiByte(CP_UTF8)` |

### 2.3 子路径: RAW_WRITE (CONSOLE_IO_RAW_WRITE, Function=0x05)

某些场景（如早期启动阶段），用户程序直接通过 ConDrv RAW_WRITE 写入，不走 Console API：

```
ConDrv → CD_IO_DESCRIPTOR{Function=CONSOLE_IO_RAW_WRITE}
  → conpty_message_router::dispatch → case CONSOLE_IO_RAW_WRITE
  → bridge->handle_raw_write(msg)
       │
       ├─ Unicode=1 → WideCharToMultiByte(CP_UTF8) → WriteFile(vt_out)
       └─ Unicode=0 → MultiByteToWideChar(CP_ACP) → WideCharToMultiByte(CP_UTF8) → WriteFile(vt_out)
```

**注意**: RAW_WRITE 不经过 screen_buffer，直接写 PTY。

---

## 三、路径 2: ReadConsole — 终端键盘输入到用户程序

### 3.1 完整调用链

```
终端用户按键
  │
  ├─ [1] WT 通过 PTY 管道发送字符到 corehost
  │      格式: UTF-8 字节流
  │      普通按键: 直接 ASCII/UTF-8 字符
  │      特殊键: VT 序列 (如 ESC[A = 上箭头)
  │
  ├─ [2] 用户程序调用 ReadConsole(hIn, ...)
  │      → ConDrv → CD_IO_DESCRIPTOR{Function=CONSOLE_IO_USER_DEFINED, ApiNumber=L1=5}
  │
  ├─ [3] api_router::dispatch_L1 → case 5 → api_read_console(msg)
  │      → bridge->handle_console_read(msg, proc_z, init_data, init_bytes)
  │      │
  │      ├─ 保存 pending 状态 (_pend_kind = ConsoleRead)
  │      ├─ accumulate_from_pipe() → 尝试立即读取
  │      │    │
  │      │    ├─ PeekNamedPipe(vt_in) → ReadFile(vt_in) → _readbuf[]
  │      │    ├─ 有 inp (input_buffer)? → process_input(bytes, len)
  │      │    │    │
  │      │    │    ├─ 逐字节处理:
  │      │    │    │   ├─ 0x1B (ESC) → 进入 VT 解析模式
  │      │    │    │   │   vt_parser::parse(byte) → vt_message
  │      │    │    │   │   vt_input_engine::convert(msg) → INPUT_RECORD
  │      │    │    │   │   input_buffer::write(&rec, 1)
  │      │    │    │   │
  │      │    │    │   ├─ \r / \n → 跳过 (行尾由 scan_for_line 处理)
  │      │    │    │   ├─ 0x7f (Backspace) / 0x08 → _text_len--
  │      │    │    │   └─ 可打印字符 → [_text_buf++] = byte; WriteFile(vt_out) echo
  │      │    │    │
  │      │    │    └─ 无 inp? → 直接 echo 全部字节到 vt_out
  │      │    │
  │      │    └─ scan_for_line() → 查找 \r / \n / 0x1A
  │      │         找到? → complete_pending()
  │      │
  │      └─ 未找到完整行? → return false (io_loop 进入 pending 自旋)
  │
  ├─ [4] io_loop 自旋 → on_idle() → accumulate_from_pipe() → scan_for_line()
  │      循环直到收到换行符
  │
  └─ [5] complete_pending()
         │
         ├─ _text_buf 有内容? → [编码转换 C] vt_input_engine::convert_text()
         │    手动 UTF-8→wchar_t 解码 (处理 2/3/4 字节序列)
         │    → input_buffer::write() → SetEvent(InputAvailableEvent)
         │
         ├─ finalize_read()
         │    ├─ 行尾规范化: \r → \r\n, 孤 \n → \r\n
         │    │
         │    └─ [编码转换 D] Unicode 模式?
         │         ├─ Yes → MultiByteToWideChar(CP_UTF8, _readbuf) → UTF-16 LE 输出
         │         └─ No  → memcpy(_readbuf) → 原始字节输出
         │
         └─ miniio::complete_io(server, comp)
              → ConDrv → 用户程序 ReadConsole 返回
```

### 3.2 编码转换点

| 编号 | 位置 | 源编码 | 目标编码 | API |
|------|------|--------|---------|-----|
| — | `vt_parser::parse()` | UTF-8 字节 | `vt_message` (结构化) | 内联状态机 |
| — | `vt_input_engine::convert()` | `vt_message` | `INPUT_RECORD` | 内联 |
| **C** | `vt_input_engine::convert_text()` | UTF-8 字节 | `wchar_t` | 手动 UTF-8 解码 |
| **D** | `finalize_read` L632-643 | UTF-8 原始字节 | UTF-16 LE | `MultiByteToWideChar(CP_UTF8)` |

### 3.3 子路径: RAW_READ (CONSOLE_IO_RAW_READ, Function=0x06)

与 ReadConsole 类似，但消息格式为 `CONSOLE_READCONSOLE_MSG`（无 `CONSOLE_MSG_HEADER` 包装），pending 类型为 `PendingKind::RawRead`。

---

## 四、路径 3: 屏幕缓冲操作

### 4.1 FillConsoleOutput

```
FillConsoleOutput(hOut, char, len, coord)
  → L2 API 0 → api_fill_output(msg, state, sb, bridge)
       │
       ├─ ElementType 校验 (ASCII/UNICODE/ATTRIBUTE)
       ├─ 写入 screen_buffer: fill_character() / fill_attribute()
       │
       └─ VT 同步:
            ├─ PowerShell shim: 全屏空格填充?
            │   └─ Yes → vt_write_clear_screen() → ESC[2J + ESC[H
            │            (ED2 全屏清除, 大幅减少 VT 序列量)
            │
            └─ No → DECSC(ESC7) + CUP + 逐格写入 + DECRC(ESC8)
                    每格: vt_write_cell(ch) → WideCharToMultiByte(CP_UTF8)
```

### 4.2 ScrollConsoleScreenBuffer

```
ScrollConsoleScreenBuffer(hOut, scrollRect, clipRect, destOrigin, fill)
  → L2 API 12 → api_scroll_sb(msg, state, sb, bridge)
       │
       ├─ sb->scroll() — 移动 screen_buffer 中的 CHAR_INFO
       │
       └─ VT 同步:
            ├─ DECSC(ESC7) 保存光标
            ├─ DECSTBM(ESC[top;bottomr) 设置滚动区域
            ├─ dy < 0? → ESC[nM (IL 插入行, 向下滚)
            ├─ dy > 0? → ESC[nL (DL 删除行, 向上滚)
            └─ ESC[r (重置滚动区域) + DECRC(ESC8) 恢复光标
```

### 4.3 WriteConsoleOutput (矩形写入)

```
WriteConsoleOutput(hOut, rect, CHAR_INFO[])
  → L2 API 17 → api_write_console_output(msg, state, sb, bridge)
       │
       ├─ sb->write_rect(rect, CHAR_INFO[]) — 批量写入 screen_buffer
       │
       └─ VT 同步: 逐行扫描 rect
            ├─ CUP 移到行首
            ├─ 按 attribute 变化分批次 vt_write_attr() + vt_write_cell()
            └─ DECRC 恢复光标
```

### 4.4 WriteConsoleOutputString

```
WriteConsoleOutputString(hOut, str, len, coord)
  → L2 API 18 → api_write_output_string(msg, state, sb, bridge)
       │
       ├─ StringType==ATTRIBUTE → sb->write_attribute()
       ├─ StringType==字符类型 → sb->write_character(coord, chars, len)
       │
       └─ VT 同步:
            DECSC + CUP + vt_write_attr + 逐字 vt_write_cell + DECRC
            [编码转换] wchar_t → UTF-8 (via vt_write_cell)
```

---

## 五、路径 4: 屏幕缓冲回读

### 5.1 ReadConsoleOutput

```
ReadConsoleOutput(hOut, rect, CHAR_INFO[])
  → L2 API 19 → api_read_console_output(msg, state, sb, bridge)
       │
       ├─ sb->read_rect(rect, out[]) — 从 screen_buffer 读取 CHAR_INFO
       │    数据格式: wchar_t (UnicodeChar) + WORD (Attributes)
       │    无编码转换 — 直接返回 wchar_t 数组
       │
       └─ 标记 state.cursor_position_dirty = true
            (下次 GetConsoleScreenBufferInfo 时将触发 DSR CPR 光标同步)
```

### 5.2 ReadConsoleOutputString

```
ReadConsoleOutputString(hOut, str, len, coord)
  → L2 API 15 → api_read_output_string(msg, state, sb, bridge)
       │
       ├─ StringType==ATTRIBUTE → sb->read_attribute()  返回 WORD[]
       ├─ StringType==字符类型 → sb->read_character()   返回 wchar_t[]
       │
       └─ 无 VT 输出，无编码转换
```

---

## 六、路径 5: ConDrv 消息路由全景

### 6.1 主循环

```
run_io_loop_no_setup(server, event, handler)
  │
  for(;;) {
    │
    ├─ read_io(prev, &msg)  // DeviceIoControl(IOCTL_CONDRV_READ_IO)
    │    返回 CD_IO_DESCRIPTOR + CD_IO_COMPLETE + body[4096]
    │
    ├─ msg.descriptor.Function == 0?  // PENDING 超时
    │   └─ handler.on_idle() → bridge->on_idle() → accumulate/scan
    │
    ├─ handler.on_message(msg) == false?  // 挂起
    │   └─ while(handler.has_pending()) handler.on_idle()  // 自旋
    │
    └─ handler.should_exit()? → break
  }
```

### 6.2 Function → Handler 映射

| ConDrv Function | 值 | 路由 | 编码说明 |
|----------------|-----|------|---------|
| CONNECT | 0x01 | `io->handle_connect()` | 无字符串 |
| DISCONNECT | 0x02 | `io->handle_disconnect()` | 无字符串 |
| CREATE_OBJECT | 0x03 | `io->handle_create_object()` | 无字符串 |
| CLOSE_OBJECT | 0x04 | `io->handle_close_object()` | 无字符串 |
| RAW_WRITE | 0x05 | `bridge->handle_raw_write()` | ANSI→UTF-16→UTF-8 (双跳) |
| RAW_READ | 0x06 | `bridge->handle_raw_read()` | UTF-8→(可选UTF-16) |
| USER_DEFINED | 0x07 | `api->handle_user_defined()` | 见 L1/L2/L3 分派 |
| RAW_FLUSH | 0x08 | `io->handle_raw_flush()` | 无字符串 |

### 6.3 L1/L2 API 枚举

```
USER_DEFINED → CONSOLE_MSG_HEADER.ApiNumber
  Layer = ApiNumber >> 24   (1/2/3)
  Index = ApiNumber & 0xFFFFFF

L1 (10 APIs):
  0=GetConsoleCP  1=GetConsoleMode  2=SetConsoleMode  3=GetNumberOfConsoleInputEvents
  4=GetConsoleInput  5=ReadConsole  6=WriteConsole  7=NotifyLastClose
  8=GetConsoleLangId  9=MapBitmap

L2 (22 APIs):
  0=FillConsoleOutput  1=GenerateConsoleCtrlEvent  2=SetConsoleActiveScreenBuffer
  3=FlushConsoleInputBuffer  4=SetConsoleCP  5=GetConsoleCursorInfo
  6=SetConsoleCursorInfo  7=GetConsoleScreenBufferInfo  8=SetConsoleScreenBufferInfo
  9=SetConsoleScreenBufferSize  10=SetConsoleCursorPosition
  11=GetLargestConsoleWindowSize  12=ScrollConsoleScreenBuffer
  13=SetConsoleTextAttribute  14=SetConsoleWindowInfo  15=ReadConsoleOutputString
  16=WriteConsoleInput  17=WriteConsoleOutput  18=WriteConsoleOutputString
  19=ReadConsoleOutput  20=GetConsoleTitle  21=SetConsoleTitle
```

---

## 七、编码转换时机总表

### 7.1 输出方向（corehost → 终端）

| 阶段 | 输入 | 输出 | 转换函数 | 触发 |
|------|------|------|---------|------|
| ANSI 解码 | CP_ACP/OEMCP 字节 | UTF-16 LE | `MultiByteToWideChar` | api_write_console (Unicode=0) |
| UTF-8 编码 | UTF-16 LE (wchar_t) | UTF-8 字节 | `WideCharToMultiByte(CP_UTF8)` | vt_write_cell / raw_write |
| DEC 映射 | ASCII 0x5F-0x7E | Unicode 框线字符 | `dec_to_unicode()` | dec_line_drawing_mode=ON |

### 7.2 输入方向（终端 → corehost → 用户程序）

| 阶段 | 输入 | 输出 | 转换函数 | 触发 |
|------|------|------|---------|------|
| VT 解析 | UTF-8 字节流 | `vt_message` | `vt_parser::parse()` | process_input 中找到 ESC |
| VT→Rec | `vt_message` | `INPUT_RECORD` | `vt_input_engine::convert()` | VT 序列完成 |
| 文本解码 | UTF-8 字节 | wchar_t | 手动 UTF-8→码点 | convert_text (行完成时) |
| UTF-16 输出 | UTF-8 字节 | UTF-16 LE | `MultiByteToWideChar(CP_UTF8)` | finalize_read (Unicode=1) |
| ANSI 输出 | UTF-8 原始字节 | 不变 | `memcpy` | finalize_read (Unicode=0) |

### 7.3 屏幕缓冲操作

| 操作 | 输入数据格式 | 输出 VT 编码 | 备注 |
|------|------------|-------------|------|
| FillConsoleOutput | wchar_t + WORD | UTF-8 (via vt_write_cell) | 含 PowerShell shim 优化 |
| ScrollConsoleScreenBuffer | SMALL_RECT + COORD | UTF-8 (ESC 序列) | DECSTBM + IL/DL |
| WriteConsoleOutput | CHAR_INFO[] | UTF-8 (逐行 CUP+SGR+cell) | 含属性变化检测 |
| WriteConsoleOutputString | wchar_t[] 或 WORD[] | UTF-8 | DECSC/DECRC 包裹 |
| ReadConsoleOutput | CHAR_INFO[] (回读) | 无 VT 输出 | 标记 cursor_dirty |
| ReadConsoleOutputString | wchar_t[] / WORD[] (回读) | 无 VT 输出 | 直接返回 |

---

## 八、关键设计要点

### 8.1 代码页链路

```
SetConsoleCP(input_cp)  → state.input_code_page
SetConsoleOutputCP(output_cp) → state.output_code_page

使用点:
  - api_write_console ANSI 路径: MultiByteToWideChar(state.output_code_page, ...)
  - RAW_WRITE ANSI 路径:   MultiByteToWideChar(CP_ACP, ...)  [使用 GetACP(), 非 state]
```

### 8.2 宽字符双格处理

CJK/全角字符宽度为 2 时：
- screen_buffer: `ci0.Char=wch, ci1.Char=0x0000`（trailing null）
- VT 输出: 终端自行处理宽字符渲染（发送正确 UTF-8 后终端负责占据 2 列）

### 8.3 Echo 机制

非 VT 输入字节（用户键入的文本）通过 `WriteFile(vt_out, &byte, 1)` 回显到终端，确保用户看到键入内容。VT 序列（ESC 开头）不触发回显。

### 8.4 批量 VT 缓冲

`pipe_bridge::_vt_batch_buf[8192]` — 在单次 `complete_pending()` 边界内累积 VT 输出，减少 `WriteFile` 调用次数。

### 8.5 换行符规范化

- 管道收到 `\r` → 确保紧跟 `\n`；若 `\n` 来自 PeekNamedPipe 补读，则手动 `WriteFile("\n")`
- 管道收到孤 `\n` → 前插 `\r`，形成 `\r\n`
- 最终输出到用户程序: 统一为 `\r\n`

---

*分析基于: src/conpty/conpty_api_handlers.hpp, conpty_pipe_bridge.hpp, conpty_vt_parser.hpp, conpty_vt_input_engine.hpp, conpty_message_router.hpp, miniio/io_loop.hpp*
