# Embedding 模式执行流程分析

> 基于 `terminal/src/` 原始代码，对照 corehost 当前实现。
> 日期: 2026-05-17

---

## 一、原始双线程模型

```
                    ┌──────────────────────┐
                    │   ConsoleIoThread    │  ← 主 I/O 线程, 忙轮询 ConDrv
                    │                      │
                    │  for (;;) {          │
                    │    ReadIo(prev, &msg) │  ← DeviceIoControl(IOCTL_READ_IO)
                    │    if (PENDING)       │       + WaitForSingleObject(0)
                    │      continue;        │
                    │    ServiceIoOperation │  ← 分派 8 种消息类型
                    │  }                    │
                    └──────────┬───────────┘
                               │ ReadConsole 无数据时
                               │ 返回 CONSOLE_STATUS_WAIT (0xC0030001)
                               │ → 不调用 ReadIo (不发送 completion)
                               │ → 驱动端 I/O 保持 pending
                               ▼
                    ┌──────────────────────┐
                    │   VtInputThread      │  ← 独立线程, 阻塞读 PTY
                    │                      │
                    │  for (;;) {          │
                    │    ReadFile(pty, ..)  │  ← 阻塞等待 WT 输出
                    │    parse VT sequences │  ← 解析控制序列
                    │    fill InputBuffer   │  ← 填入输入缓冲区
                    │    CompleteIo(...)    │  ← 完成挂起的 ReadConsole
                    │  }                    │
                    └──────────────────────┘
```

### 关键流程

1. **ReadIo** (`ConDrvDeviceComm.cpp:39-55`):
   ```cpp
   hr = DeviceIoControl(IOCTL_READ_IO, prev, ..., &msg, ...);
   if (hr == ERROR_IO_PENDING) {
       WaitForSingleObjectEx(_Server.get(), 0, FALSE);  // 0ms 忙轮询
       hr = S_OK;
   }
   return hr;
   ```
   - **单次** `DeviceIoControl`，不是 for 循环
   - `lpInBuffer` = 上一轮的 `CD_IO_COMPLETE`（首次 nullptr）
   - `lpOutBuffer` = `CD_IO_DESCRIPTOR` + body
   - PENDING 时 0ms 忙轮询后返回 S_OK，上层继续循环

2. **ServiceIoOperation** (`IoSorter.cpp`):
   - `CONSOLE_IO_CONNECT` → `ConsoleHandleConnectionRequest` → 创建 `\Input/\Output`
   - `CONSOLE_IO_USER_DEFINED` → L1/L2 API 分派
   - ReadConsole 无数据 → 返回 `CONSOLE_STATUS_WAIT`
   - 返回 `CONSOLE_STATUS_WAIT` 后，`ConsoleIoThread` **不调用** `ReadIo`
   - 驱动端 I/O 保持 pending

3. **VtInputThread** (`VtInputThread.cpp`):
   - 独立线程，阻塞 `ReadFile` 从 PTY 管道读取
   - 解析 VT 序列 → 填充 `InputBuffer`
   - 调用 `CompleteIo` 完成挂起的 `ReadConsole`
   - 管道断开时退出

---

## 二、corehost 单线程等效模型

```
                    ┌──────────────────────────────────────────┐
                    │   run_io_loop_no_setup (主线程)            │
                    │                                          │
                    │  for (;;) {                              │
                    │    read_io(prev, &msg)                    │  ← DeviceIoControl
                    │    if (Function == 0) {                   │      + Wait(event,0)
                    │      handler.on_idle();                   │  ← 非阻塞 PeekNamedPipe
                    │      if (should_exit()) break;            │     累积/echo/完成
                    │      continue;                            │
                    │    }                                      │
                    │    if (!handler.on_message(msg)) {        │  ← false=ReadConsole挂起
                    │      while(has_pending()) on_idle();      │  ← 自旋直到换行或_eof
                    │      if (should_exit()) break;            │
                    │    }                                      │
                    │  }                                        │
                    └──────────────────────────────────────────┘
```

### 关键设计决策

| 原始 | corehost 等效 | 说明 |
|------|-------------|------|
| VtInputThread 独立线程 | `on_idle()` 在 I/O 循环空闲时调用 | 单线程，在两次 ConDrv 消息之间非阻塞读 PTY |
| `CONSOLE_STATUS_WAIT` 返回后不调用 ReadIo | `on_message` 返回 `false` → `prev_msg` 不更新 | 下轮 `read_io` 以 `prev=nullptr` 调用，驱动不感知 |
| `CompleteIo` 由 VtInputThread 调用 | `on_idle` 内 `complete_io(server, ...)` | 独立 IOCTL 完成挂起消息 |
| 双缓冲 `msgA/msgB` | 双缓冲 `msgA/msgB` | 防止 completion 和 descriptor 重叠 |
| — | `has_pending()` / `should_exit()` | 自旋等待 + 干净退出检测 |

---

## 三、已修复的 Bug

### Bug 1: `signal.cpp` 中的 `std::abort()` (致命)

**现象**: WT 窗口一闪而过，cmd.exe 立即退出。

**根因**: WT 启动后向信号管道发送控制码 8 (`ConsoleSetWindowOwner`)，信号线程收到未知码后执行 `default: std::abort()`，**整个 corehost 进程被杀死**。

**修复**: 将 `std::abort()` 替换为 `break`（忽略未知信号码，继续循环）。

### Bug 2: 管道方向交换

**现象**: WT 收不到 cmd.exe 输出。

**根因**: `EstablishPtyHandoff([out]in, [out]out)` 语义:
- `[out]in` = WT 读端 → corehost 写 (`vt_out`)
- `[out]out` = WT 写端 → corehost 读 (`vt_in`)

初版赋值颠倒。

**修复**: `result.vt_in = wt_out; result.vt_out = wt_in;`

### Bug 3: COM RPC 重入

**现象**: `CoRevokeClassObject` 时死锁/崩溃。

**根因**: `ITerminalHandoff3` 的 `com_ptr` 在 COM RPC 线程析构 → `Release()` 触发 RPC 重入。

**修复**: 将 `com_ptr` 移到 `handoff_result`，延迟到主线程析构。

### Bug 4: `read_io` 使用 `INFINITE` 等待

**现象**: 循环阻塞，后续 ConDrv 消息无法处理。

**根因**: `WaitForSingleObject(event, INFINITE)` 而非原始的 `timeout=0`。

**修复**: 改为 `WaitForSingleObject(event, 0)` — 对标原始 0ms 忙轮询。

### Bug 5: prev_msg 重复发送 completion

**现象**: 驱动收到重复的 completion。

**修复**: `read_io` 调用前立即 `prev_msg = nullptr`，防止同一 completion 被发送两次。

### Bug 6: `set_server_info` 对 COM-marshaled 句柄失败 (错误 22)

**现象**: `DeviceIoControl(IOCTL_SET_SERVER)` 返回 `ERROR_BAD_COMMAND`。

**根因**: COM marshaling 后的句柄不是真正的 ConDrv 句柄。

**修复**: `run_io_loop_no_setup` 跳过 `set_server_info`，使用外部传入的已注册 event。

### Bug 7: IOCTL_READ_IO 在 ReadConsole pending 时死锁

**现象**: 输入字符后 WT 无响应，cmd.exe 卡死。

**根因**: ConDrv 有 pending ReadConsole 时 `IOCTL_READ_IO` 阻塞 (内核等待 completion)，但 completion 需要 `on_idle` 中的 `complete_io`，而 `on_idle` 只在 `read_io` 返回 PENDING 时调用——形成死锁。

**修复**: `on_message` 返回 `false` 后进入自旋循环 `while(has_pending()) on_idle()`，不调用 `read_io`。

### Bug 8: LINE_INPUT 模式下 "More?" 输出

**现象**: 每输入一个字符，cmd.exe 输出 "More? "。

**根因**: LINE_INPUT 模式下 cmd.exe 的 `ReadConsole` 期望完整行。`api_read_console` 即时返回不完整数据，cmd 将其解释为需要更多输入。

**修复**: `api_read_console` 总是返回 `false` (pending)，`on_idle` 跨调用累积直到换行才 `complete_io`。

### Bug 9: `_pending_read` 悬垂指针

**现象**: 完成数据包含垃圾字节，cmd.exe 输出乱码。

**根因**: 保存 `&msg` 指针到 `_pending_read`，但 `msgA`/`msgB` 交替会覆盖该内存。

**修复**: 使用独立存储 `_pending_id`/`_pending_uni`/`_pending_active`/`_pending_outbuf[]`，不依赖 msg 生命周期。

### Bug 10: `IoStatus.Information` 值错误

**现象**: ConDrv 返回错误字节数给 cmd.exe，导致垃圾回显。

**根因**: `IoStatus.Information` 必须等于 `Write.Size` (即 `sizeof(CONSOLE_READCONSOLE_MSG) + data_bytes`)，之前使用了错误的值。

**修复**: `flush_accumulated` 中设置 `comp.IoStatus.Information = sz` (头部+数据总大小)。

---

## 四、文件清单

| 文件 | 角色 |
|------|------|
| `src/cli/main.cpp` | 入口: COM 公寓初始化, 分派 com_server/defterm/headless/client |
| `src/comserver/com_server.cpp` | COM 服务器: IConsoleHandoff → ITerminalHandoff3, signal_thread 创建 (含 vt_in) |
| `src/conpty/conpty_entry.hpp` | ConPTY 消息处理: on_message (8种) + on_idle (ReadConsole pending/echo) |
| `src/miniio/io_loop.hpp` | I/O 事件循环模板: run_io_loop / run_io_loop_no_setup (含自旋+should_exit) |
| `src/miniio/io_thread.hpp` | ConDrv IOCTL 原语: read_io / complete_io / accept_connection |
| `src/miniio/signal.hpp` | 信号管道监听线程参数 (含 vt_in 字段) |
| `src/miniio/signal.cpp` | 信号管道监听线程: ConsoleControl 转发 + 管道断开关闭 vt_in |

---

## 五、IOCTL 速查

| IOCTL | 值 | 方向 | 用途 |
|-------|-----|------|------|
| `IOCTL_READ_IO` | `CTL_CODE(FILE_DEVICE_CONSOLE, 1, METHOD_OUT_DIRECT, ...)` | lpInBuffer=完成, lpOutBuffer=新消息 | 读取下一条 ConDrv 消息 |
| `IOCTL_COMPLETE_IO` | `CTL_CODE(FILE_DEVICE_CONSOLE, 2, METHOD_NEITHER, ...)` | lpInBuffer=CD_IO_COMPLETE | 提交完成结果 |
| `IOCTL_SET_SERVER` | `CTL_CODE(FILE_DEVICE_CONSOLE, 7, METHOD_NEITHER, ...)` | lpInBuffer=CD_IO_SERVER_INFORMATION | 注册 InputAvailableEvent |

| 消息类型 | Function 值 | 说明 |
|---------|------------|------|
| `CONSOLE_IO_CONNECT` | 1 | 客户端连接 |
| `CONSOLE_IO_DISCONNECT` | 2 | 客户端断开 |
| `CONSOLE_IO_CREATE_OBJECT` | 3 | 创建对象句柄 |
| `CONSOLE_IO_CLOSE_OBJECT` | 4 | 关闭对象句柄 |
| `CONSOLE_IO_RAW_WRITE` | 5 | WriteConsole 等效 |
| `CONSOLE_IO_RAW_READ` | 6 | ReadConsole 等效 |
| `CONSOLE_IO_USER_DEFINED` | 7 | L1/L2 Console API |
| `CONSOLE_IO_RAW_FLUSH` | 8 | Flush |

---

## 六、信号管道协议

```
┌──────────┐   1 byte     ┌──────────┐
│   WT     │──────────────│ corehost │
│ (终端)   │  + payload   │ (宿主)   │
└──────────┘              └────┬─────┘
                               │ ConsoleControl()
                               ▼
                          ┌──────────┐
                          │  CSRSS   │
                          └──────────┘
```

| Code | 名称 | Payload | 操作 |
|------|------|---------|------|
| 1 | `ConsoleNotifyConsoleApplication` | `CONSOLENOTIFYAPPDATA` (≥8 bytes) | `ConsoleControl(1, &cpi, 8)` |
| 5 | `ConsoleSetForeground` | 无 | no-op |
| 7 | `ConsoleEndTask` | `CONSOLEENDTASKDATA` (≥16 bytes) | `ConsoleControl(7, &c, 24)` |
| 8 | `ConsoleSetWindowOwner` | (WT 扩展) | 忽略 (default: break) |
| 其他 | 未知 | 变长 | 忽略 (default: break) |
