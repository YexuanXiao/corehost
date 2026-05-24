# ConPTY I/O 子系统实现

> 基于 `terminal/src/` 原始代码分析，对照 corehost 当前实现。
> 最后更新: 2026-05-19

## 一、原始双线程模型 vs corehost 单线程模型

### 原始 (terminal/src)
```
┌─────────────────────┐     ┌──────────────────────┐
│   ConsoleIoThread   │     │    VtInputThread      │
│   (忙轮询 ConDrv)    │     │    (阻塞读 PTY)       │
│                     │     │                      │
│ for(;;) {           │     │ for(;;) {            │
│   ReadIo(prev,&msg) │     │   ReadFile(pty,...)  │
│   if(PENDING)       │     │   parse VT seqs      │
│     continue;       │     │   fill InputBuffer   │
│   ServiceIoOperation│     │   CompleteIo(...)     │
│ }                   │     │ }                    │
└─────────────────────┘     └──────────────────────┘
```
- ReadConsole 无数据 → `CONSOLE_STATUS_WAIT` → **不调用 ReadIo** (不发送 completion)
- VtInputThread 独立线程阻塞读 PTY，有数据后 `CompleteIo` 完成挂起的 ReadConsole

### corehost (当前实现)
```
┌──────────────────────────────────────────────┐
│              run_io_loop (单线程)              │
│                                              │
│ for(;;) {                                    │
│   read_io(prev, &msg)  // DeviceIoControl    │
│   if (Function == 0) { // PENDING 超时       │
│     handler.on_idle(); // ★ 检查/完成挂起消息  │
│     if (should_exit()) break;                │
│     continue;                                │
│   }                                          │
│   if (on_message(msg) == false) { // 挂起     │
│     while(has_pending()) on_idle(); // 自旋   │
│     if (should_exit()) break;                │
│   }                                          │
│ }                                            │
└──────────────────────────────────────────────┘
```
- ReadConsole 总是返回 `false` → 进入 pending 状态
- `on_idle()` → `process_input()` 非阻塞 PeekNamedPipe → VT 解析 + 行累积
- VT 序列经 `vt_parser::parse()` → `vt_input_engine::convert()` → `input_buffer::write()`
- 文本累积到 `_text_buf`，行完成时 `convert_text()` 批量转换为 INPUT_RECORD
- 收到换行后 `complete_io` 完成挂起消息

---

## 二、实现文件

| 文件 | 职责 |
|------|------|
| `src/conpty/conpty_entry.hpp` | `pty_forward_handler` — 8种 ConDrv 消息处理 + Console API + ReadConsole pending/echo |
| `src/conpty/conpty_pipe_bridge.hpp` | `pipe_bridge` — PTY 管道桥：VT 解析集成 + 输入引擎 + 行累积 + echo + 启动序列 |
| `src/conpty/conpty_vt_parser.hpp` | `vt_parser` — VT/CSI/OSC 序列状态机解析器（48 种消息 + 19 种 bad_state） |
| `src/conpty/conpty_vt_input_engine.hpp` | `vt_input_engine` — vt_message → INPUT_RECORD 转换引擎 |
| `src/conpty/conpty_input_buffer.hpp` | `input_buffer` — INPUT_RECORD 队列（write/read/peek/flush） |
| `src/miniio/io_loop.hpp` | `run_io_loop` / `run_io_loop_no_setup` — 双缓冲 I/O 事件循环模板 |
| `src/miniio/io_thread.hpp` | ConDrv IOCTL 原语: `read_io` / `complete_io` / `accept_connection` |
| `src/miniio/signal.hpp` | `signal_thread_params` + `signal_thread_proc` 声明 |
| `src/miniio/signal.cpp` | 信号管道线程: ConsoleControl 转发 + 管道断开时关闭 vt_in |

**对标关系**:
- `pty_forward_handler` ≡ `IoSorter::ServiceIoOperation` + `ApiSorter` + `VtInputThread::_InputThread`
- `pipe_bridge::process_input()` ≡ `StateMachine::ProcessString` + `InputStateMachineEngine::Action*` + `InteractDispatch`
- `on_idle()` ≡ `VtInputThread::_Run` 的非阻塞等效
- `signal_thread_proc` ≡ `PtySignalInputThread`

---

## 三、pty_forward_handler 核心设计

### 3.1 消息分派 (on_message)

处理 8 种 ConDrv Function 类型:

| Function | 值 | 处理 |
|----------|-----|------|
| CONNECT | 0x01 | `accept_connection` → 创建 `\Input`/`\Output` 句柄 |
| DISCONNECT | 0x02 | 清除 handles |
| CREATE_OBJECT | 0x03 | `NtOpenFile(\Input)` / `NtOpenFile(\Output)` |
| CLOSE_OBJECT | 0x04 | 确认 |
| RAW_WRITE | 0x05 | `raw_write` → `WideCharToMultiByte(CP_UTF8)` → `WriteFile(vt_out)` |
| RAW_READ | 0x06 | `raw_read` → 非阻塞 PeekNamedPipe → 读至换行 → finalize_read |
| USER_DEFINED | 0x07 | `handle_user_defined` → L1/L2 API 分派 |
| RAW_FLUSH | 0x08 | 确认 |

### 3.2 ReadConsole 挂起路径 (api_read_console + on_idle)

```
api_read_console(msg)
  ├─ _vt_eof? → do_complete(msg, 0), return true (立即返回 0 字节)
  └─ 否则:
      保存 Identifier、Unicode 标志、CONSOLE_READCONSOLE_MSG 到独立存储
      设置 _pending_active=true, _raw_read_total=0
      return false (消息挂起)
```

```
on_idle()
  ├─ _vt_eof? → 若 _pending_active → flush_accumulated(), return
  ├─ PeekNamedPipe(vt_in) 失败 → _vt_eof=true, 若 pending → flush_accumulated()
  ├─ !_pending_active → 仅检测断管, return
  ├─ avail == 0 → return (下轮重试)
  └─ ReadFile → 累积到 _readbuf
       ├─ 扫描 \r/\n → 手动 echo 新字节+\r\n 到 vt_out, flush_accumulated(), _pending_active=false
       └─ 未找到换行 → echo 新字节到 vt_out, 保持 _pending_active=true (下轮继续)
```

### 3.3 flush_accumulated

```c
void flush_accumulated() {
    // 从 _pending_outbuf 重建 CONSOLE_READCONSOLE_MSG (含 original ExeNameLength/InitialNumBytes 等字段)
    // finalize_read: 规范化行尾 (\r→\r\n, \n→\r\n)
    // CD_IO_COMPLETE: Identifier=_pending_id, IoStatus.Information=sizeof(MSG)+data_bytes
    // miniio::complete_io(server, comp)
}
```

### 3.4 手动 Echo

在 `on_idle` 中每次读取新字节后，通过 `WriteFile(vt_out)` 将原始字节回写到 VT 输出管道。这替代了原始 `ENABLE_ECHO_INPUT` 模式下的驱动自动回显。

- 未找到换行: echo 所有新读字节
- 找到 `\r`: echo `\r` 之前未回显的字节 + `\r\n`
- 找到 `\n`: echo 到 `\n` (含)

### 3.5 干净退出链路

```
WT 关闭 → signal_thread: pipe 断开 → pp->vt_in.clear()
→ PeekNamedPipe(vt_in) 失败 → on_idle 设置 _vt_eof=true
→ 若 _pending_active: flush_accumulated() 发送残留数据
→ api_read_console: _vt_eof 为真 → do_complete(msg, 0) 返回 0 字节
→ cmd.exe 收到 EOF → 退出
→ ConDrv 检测客户端断开 → read_io 返回 false
→ io_loop: should_exit() → break
```

---

## 四、I/O 循环设计 (io_loop.hpp)

### 4.1 双缓冲

```c
io_msg msgA{}, msgB{};  // 两个独立缓冲区
io_msg *cur = &msgA;     // 当前接收缓冲区
io_msg *prev_msg = nullptr; // 上一轮完成的消息
```

`prev->Write.Data` 指向上一轮的 `body`，`cur->descriptor` 和 `cur->body` 是当前轮的新消息。由于 `msgA != msgB`，两者绝不重叠。

### 4.2 Handler 接口要求

```cpp
// Handler 必须提供:
bool on_connect(io_msg &msg);     // CONNECT 处理, 应调用 accept_connection
bool on_message(io_msg &msg);     // 非 CONNECT 消息, true=已完成, false=挂起
void on_idle();                   // 空闲时调用, 服务挂起消息
bool has_pending() const;         // 检查是否有挂起 I/O
bool should_exit() const;         // VT pipe 断开后 I/O 循环退出
```

### 4.3 自旋循环

当 `on_message` 返回 `false` (消息挂起) 时:
```c
while (handler.has_pending())
    handler.on_idle();
if (handler.should_exit())
    break;
```

这确保在 ReadConsole pending 期间，I/O 循环不会调用 `read_io`（因为 `read_io` 在 ConDrv 有 pending I/O 时会阻塞）。

---

## 五、关键设计决策

| 决策 | 原因 |
|------|------|
| `api_read_console` 总是返回 false | LINE_INPUT 模式下 cmd.exe 需要完整行，部分返回产生 "More?" |
| `on_idle` 跨调用累积输入 | ConDrv 有 pending I/O 时 `read_io` 阻塞，无法回到消息循环 |
| 独立存储 `_pending_id`/`_pending_outbuf` | msgA/msgB 交替会覆盖指针，不能保存 `&msg` 引用 |
| `IoStatus.Information = sizeof(MSG) + data_bytes` | ConDrv 据此返回正确字节数给 cmd.exe，错误值导致垃圾回显 |
| `signal_thread_params.vt_in` | 信号线程在管道断开时关闭 vt_in，打断主线程 PeekNamedPipe |
| `should_exit() = _vt_eof && !_pending_active` | 确保残留数据先发送再退出 |
| 手动 echo 替代 ECHO_INPUT 模式 | 驱动级回显在 ConPTY 路径不可用，需在 on_idle 中手动 WriteFile |

---

## 六、测试

`tests/test_conpty_pipe.cpp` — 管道读写单元测试 (6 项, 全部通过):

```
=== ConPTY Pipe I/O Tests ===
  UTF-8 roundtrip... PASSED
  UTF-16 CR->CRLF translation... PASSED
  Broken pipe detection... PASSED
  Empty pipe read... PASSED
  Batch submit accumulation... PASSED
  UTF-16 empty string... PASSED
Total: 6 | Passed: 6 | Failed: 0
```

---

*对标原始代码: terminal/src/host/VtIo.cpp, VtInputThread.cpp, PtySignalInputThread.cpp, terminal/src/parser/InputStateMachineEngine.cpp*
*当前实现: src/conpty/conpty_entry.hpp, src/conpty/conpty_pipe_bridge.hpp, src/conpty/conpty_vt_parser.hpp, src/conpty/conpty_vt_input_engine.hpp, src/miniio/io_loop.hpp, src/miniio/io_thread.hpp, src/miniio/signal.cpp*
