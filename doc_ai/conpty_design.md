# ConPTY 总体设计文档

## 基于 terminal/src 原始代码分析

---

## 一、ConPTY 功能全列表

以下功能列表来源于 `terminal/src/` 的原始实现：

### 1. 进程管理
| # | 功能 | 原始实现位置 |
|---|------|------------|
| 1.1 | CreatePseudoConsole — 创建 PTY 管道对 + ConDrv Server + 启动 conhost 进程 | `winconpty/winconpty.cpp` |
| 1.2 | ClosePseudoConsole — 关闭 PTY，终止 conhost 进程 | `winconpty/winconpty.cpp` |
| 1.3 | ResizePseudoConsole — 通知 conhost 调整终端尺寸 | `winconpty/winconpty.cpp` |
| 1.4 | ClearPseudoConsole — 清屏命令 | `winconpty/winconpty.cpp` |
| 1.5 | ShowHidePseudoConsole — 控制窗口可见性 | `winconpty/winconpty.cpp` |
| 1.6 | ReparentPseudoConsole — 重设父窗口 | `winconpty/winconpty.cpp` |
| 1.7 | PackPseudoConsole — 打包 PTY 句柄用于跨进程传递 | `winconpty/winconpty.cpp` |

### 2. 命令行解析
| # | 功能 | 原始实现位置 |
|---|------|------------|
| 2.1 | `--server 0x<HANDLE>` — ConDrv Server 句柄 | `host/ConsoleArguments.cpp` |
| 2.2 | `--signal 0x<HANDLE>` — 信号管道句柄 | 同上 |
| 2.3 | `--headless` — 无窗口模式标志 | 同上 |
| 2.4 | `--width <N> --height <N>` — 初始终端尺寸 | 同上 |
| 2.5 | `--inheritcursor` — 继承光标位置 | 同上 |
| 2.6 | `--textMeasurement <mode>` — 文本宽度测量模式 (graphemes/wcswidth/console) | 同上 |
| 2.7 | `--ambiguousIsWide` — 模糊宽度字符视为宽字符 | 同上 |
| 2.8 | `-Embedding` — COM 服务器模式 | 同上 |

### 3. I/O 线程与消息循环
| # | 功能 | 原始实现位置 |
|---|------|------------|
| 3.1 | ConsoleIoThread — 主 I/O 线程，循环调用 `ReadIo` + `ServiceIoOperation` | `host/srvinit.cpp` |
| 3.2 | SetServerInformation — 向 ConDrv 注册 InputAvailableEvent | `host/srvinit.cpp` |
| 3.3 | IoSorter::ServiceIoOperation — 8 种消息类型分派 | `server/IoSorter.cpp` |
| 3.4 | CONNECT — 客户端连接处理，分配 console 资源 | `server/IoDispatchers.cpp` |
| 3.5 | DISCONNECT — 客户端断开，释放资源 | 同上 |
| 3.6 | CREATE_OBJECT — 创建 `\Input`/`\Output` 客户端句柄 | 同上 |
| 3.7 | CLOSE_OBJECT — 关闭客户端句柄 | 同上 |
| 3.8 | RAW_WRITE — 客户端写入 → 转换为 VT 序列输出到 PTY | `server/ApiDispatchers.cpp` |
| 3.9 | RAW_READ — 客户端读取 ← 从 PTY 输入缓冲区读取 | 同上 |
| 3.10 | USER_DEFINED — Console API 调用（40+ 个 API） | `server/IoDispatchers.cpp` → `ApiSorter.cpp` |
| 3.11 | RAW_FLUSH — 刷新输入缓冲区 | `server/IoSorter.cpp` |

### 4. VT 输出渲染（Console → PTY）
| # | 功能 | 原始实现位置 |
|---|------|------------|
| 4.1 | Writer::WriteUTF8 — 原始 UTF-8 写入 PTY 输出管道 | `host/VtIo.cpp` |
| 4.2 | Writer::WriteUTF16 — UTF-16 → UTF-8 转换后写入 (含 CRLF 翻译) | 同上 |
| 4.3 | Writer::WriteCUP / WriteDECTCEM / WriteSGR1006 / WriteDECAWM — 光标/属性/宽字符 VT 序列 | 同上 |
| 4.4 | Writer::WriteAttributes — 字符属性 → SGR 序列 (16色/256色/真彩色) | 同上 |
| 4.5 | Writer::WriteInfos — CHAR_INFO 数组 → VT 文本 + 属性序列 (含宽字符处理) | 同上 |
| 4.6 | Writer::WriteScreenInfo — 完整屏幕快照 → VT 序列 (两缓冲区、光标、属性、换行状态) | 同上 |
| 4.7 | Writer::Submit — 批量刷新到管道 (支持 Overlapped I/O) | 同上 |
| 4.8 | Writer::_flushNow — 实际 WriteFile，处理 ERROR_BROKEN_PIPE / ERROR_IO_PENDING | 同上 |
| 4.9 | 初始 VT 握手序列：DA1 + FocusEvent + Win32Input | 同上 |
| 4.10 | 终止 VT 序列：关闭 Focus / Win32Input 模式 | `host/VtIo.cpp` Shutdown() |

### 5. VT 输入解析（PTY → Console）
| # | 功能 | 原始实现位置 |
|---|------|------------|
| 5.1 | VtInputThread — 独立线程，从 PTY 输入管道 ReadFile 读取 VT 序列 | `host/VtInputThread.cpp` |
| 5.2 | UTF-8 → UTF-16 转换 | 同上 |
| 5.3 | InputStateMachineEngine — VT 输入序列状态机，解码为 console 输入事件 | `terminal/parser/InputStateMachineEngine.cpp` |
| 5.4 | WaitUntilDA1 — 等待终端 DA1 响应，获取设备属性 | `host/VtInputThread.cpp` |
| 5.5 | CaptureNextCursorPositionReport — 捕获光标位置报告 | 同上 |
| 5.6 | 按键事件 → INPUT_RECORD 合成 (Win32 键盘模式、常规 VT 键、修饰键) | `terminal/parser/InputStateMachineEngine.cpp` |
| 5.7 | 鼠标事件 → MOUSE_EVENT_RECORD 合成 | 同上 |
| 5.8 | Focus 事件处理 | 同上 |
| 5.9 | InteractDispatch — 将解码后的事件写入控制台输入缓冲区 | `terminal/adapter/InteractDispatch.cpp` |

### 6. 控制信号管道
| # | 功能 | 原始实现位置 |
|---|------|------------|
| 6.1 | PtySignalInputThread — 独立线程，从信号管道读取控制命令 | `host/PtySignalInputThread.cpp` |
| 6.2 | ShowHideWindow — 显示/隐藏伪窗口 | 同上 |
| 6.3 | ResizeWindow — 调整控制台缓冲区大小 | 同上 |
| 6.4 | ClearBuffer — 清空缓冲区 | 同上 |
| 6.5 | SetParent — 重设父窗口 | 同上 |
| 6.6 | CreatePseudoWindow — 创建伪窗口 (拥有者 terminal UI) | 同上 |
| 6.7 | 信号管道断开 → SendCloseEvent → 退出 | 同上 |

### 7. 控制台状态管理
| # | 功能 | 原始实现位置 |
|---|------|------------|
| 7.1 | VtIo 状态机：Uninitialized → Initialized → Starting → Running | `host/VtIo.hpp` |
| 7.2 | ConsoleAllocateConsole — 分配 screen buffer、渲染器、输入缓冲区 | `host/srvinit.cpp` |
| 7.3 | 文本宽度测量模式切换 (graphemes/wcswidth/console) | `host/VtIo.cpp` |
| 7.4 | 模糊宽度字符处理 | 同上 |
| 7.5 | Console lock 管理 — 多线程同步 | 遍布 `host/` |
| 7.6 | 伪窗口创建与 DPI 缩放处理 | `host/VtIo.cpp` |

### 8. 默认终端移交（Delegation / Handoff）
| # | 功能 | 原始实现位置 |
|---|------|------------|
| 8.1 | ConsoleHandleConnectionRequest — CONNECT 时检查是否应移交 | `server/IoDispatchers.cpp` |
| 8.2 | 注册表 DelegationConsole / DelegationTerminal 查询 | `host/srvinit.cpp` |
| 8.3 | CoCreateInstance → IConsoleHandoff::EstablishHandoff → 句柄移交 | `host/srvinit.cpp` |
| 8.4 | IDefaultTerminalMarker 验证 | `host/srvinit.cpp` |
| 8.5 | IConsoleHandoff COM 接口实现（接收端） | `host/` 相关文件 |
| 8.6 | ITerminalHandoff3 COM 接口调用（发送端） | `host/srvinit.cpp` |

---

## 二、原始架构概览

```
                      ┌──────────────────────┐
                      │  CreatePseudoConsole │  winconpty.cpp
                      │  (管道对 + 启动conhost)│
                      └──────────┬───────────┘
                                 │ 管道句柄通过继承传递给子进程
                                 ▼
                ┌────────────────────────────────┐
                │        conhost.exe 主进程        │
                │                                │
                │  ┌──────────────────────────┐  │
                │  │ ConsoleCreateIoThread    │  │  srvinit.cpp
                │  │  → ConsoleServerInit     │  │
                │  │  → SetServerInformation  │  │
                │  │  → VtIo::Initialize      │  │
                │  └──────────┬───────────────┘  │
                │             │                  │
                │  ┌──────────▼───────────────┐  │
                │  │ ConsoleAllocateConsole   │  │  srvinit.cpp
                │  │  → 分配 ScreenBuffer     │  │
                │  │  → 创建渲染器            │  │
                │  │  → StartIfNeeded         │  │
                │  └──────────┬───────────────┘  │
                │             │                  │
       ┌────────┼─────────────┼──────────────┐   │
       │        │             │              │   │
       ▼        ▼             ▼              ▼   │
  ┌─────────┐ ┌──────────┐ ┌──────────┐ ┌──────────┐
  │VtInput  │ │VtIo::    │ │PtySignal │ │ConsoleIo │
  │Thread   │ │Writer    │ │Thread    │ │Thread    │
  │         │ │          │ │          │ │          │
  │ReadFile │ │WriteFile │ │ReadFile  │ │ReadIo    │
  │← PTY_in │ │→ PTY_out │ │← Signal  │ │← ConDrv  │
  │         │ │          │ │          │ │          │
  │VT→Input │ │Console→VT│ │Resize/   │ │8种消息   │
  │Events   │ │Sequences │ │Show/etc  │ │分派      │
  └─────────┘ └──────────┘ └──────────┘ └──────────┘
```

---

## 三、IoSorter 消息分派全景

原始 `IoSorter::ServiceIoOperation` 处理 8 种 ConDrv 消息：

| Function | 名称 | 原始处理 | 说明 |
|----------|------|---------|------|
| 0x01 | CONNECT | `ConsoleHandleConnectionRequest` | 客户端首次连接，分配 console 环境 |
| 0x02 | DISCONNECT | `ConsoleClientDisconnectRoutine` | 客户端断开，释放句柄 |
| 0x03 | CREATE_OBJECT | `ConsoleCreateObject` | 创建 `\Input`/`\Output` 句柄 |
| 0x04 | CLOSE_OBJECT | `ConsoleCloseObject` | 关闭对象句柄 |
| 0x05 | RAW_WRITE | `ServerWriteConsole` | 写入 → VT 输出 |
| 0x06 | RAW_READ | `ServerReadConsole` | 读取 ← 输入缓冲区 |
| 0x07 | USER_DEFINED | `ConsoleDispatchRequest` → `ApiSorter` | Win32 Console API 调用 |
| 0x08 | RAW_FLUSH | `ServerFlushConsoleInputBuffer` | 刷新输入 |

**USER_DEFINED 内部 (ApiSorter)** 分派超过 40 个 L1/L2 API，包括：
- L1: Get/SetMode, Get/SetCP, ReadConsole, WriteConsole, GetNumberOfInputEvents, GetLangId, NotifyLastClose, MapBitmap
- L2: FillConsoleOutput, GenerateCtrlEvent, SetActiveScreenBuffer, FlushInputBuffer, SetCP, Get/SetCursorInfo, Get/SetScreenBufferInfo, SetScreenBufferSize, SetCursorPosition, ScrollScreenBuffer, SetTextAttribute, SetWindowInfo, ReadConsoleOutput*, WriteConsoleInput*, GetTitle, SetTitle, GetLargestWindowSize

---

## 四、VtIo::StartIfNeeded — 初始化顺序

原始实现在终端连接后的初始化步骤：

```
1. 创建 VtInputThread (如果 _hInput 有效)
2. 启动 VtInputThread
3. [可选] 若 inheritCursor:
     Writer.WriteDSRCPR()  — 请求光标位置
4. Writer.WriteUTF8(
     "\x1b[c"             — DA1: 请求设备属性
     "\x1b[?1004h"        — Focus Event Mode: 启用焦点事件
     "\x1b[?9001h"        — Win32 Input Mode: 完整 INPUT_RECORD 转发
   )
5. Writer.Submit()         — 批量刷新到管道
6. VtInputThread::WaitUntilDA1(3000) — 等待终端 DA1 响应
7. PtySignalInputThread::ConnectConsole() — 通知信号线程控制台已连接
8. State → Running
```

---

## 五、VtIo Shutdown — 清理顺序

```
1. 检查状态是否为 Running (否则直接返回)
2. [可选] OutputCP 转换器返回原始代码页
3. [可选] 恢复光标信息
4. Writer.WriteUTF8(
     "\x1b[?1004l"        — 关闭 Focus Event Mode
     "\x1b[?9001l"        — 关闭 Win32 Input Mode
   )
5. Writer.Submit()
```

---

## 六、corehost 的 ConPTY 实现相对于原始的功能对照

| 原始功能 | corehost 当前状态 | 备注 |
|---------|-----------------|------|
| **VtIo::Initialize** | `conpty_entry` | 简化入口，无状态机 |
| **VtIo::StartIfNeeded** | 跳过 (无 VT 初始化序列) | 无 VtInputThread 消费 DA1 响应 |
| **VtIo::Writer** | `raw_write` → UTF-8 转码 → `WriteFile(vt_out)` | 纯字节转发，无 VT 序列生成 |
| **VtInputThread** | `on_idle()` 非阻塞等效 | 单线程跨调用累积，读至换行后 complete_io |
| **InputStateMachineEngine** | 无 | 无 VT 输入序列解析 |
| **DA1 / DeviceAttributes** | 无 | 无设备属性协商 |
| **PtySignalInputThread** | `signal_thread_proc` (miniio/signal.cpp) | ConsoleControl 转发 + 管道断开时关闭 vt_in |
| **Resize/Clear/ShowHide** | 未实现 | 信号管道的 PTY_SIGNAL_* 未处理 |
| **ConsoleIoThread** | `run_io_loop` / `run_io_loop_no_setup` | 双缓冲 + 自旋 + should_exit 退出 |
| **IoSorter** | `pty_forward_handler::on_message` | 8 种消息全处理 |
| **ApiSorter (40+ APIs)** | L1/L2 部分实现 | 约 15 个 API (含 ReadConsole pending 路径) |
| **ScreenBuffer / 渲染器** | 无 | mini console 模式无渲染 |
| **ConsoleAllocateConsole** | 无 | 无完整控制台分配 |
| **Overlapped I/O** | 无 | 全部同步 I/O |
| **CreatePseudoConsole ABI** | 无 | 调用方自行创建管道 |
| **文本宽度测量** | 仅枚举定义 | `text_measurement_mode` 未在 pty_forward_handler 中使用 |

### 关键差异说明

1. **ReadConsole 处理**: 原始用 `CONSOLE_STATUS_WAIT` 挂起 + VtInputThread 独立线程完成。corehost 用 `api_read_console` 总是返回 `false` + `on_idle()` 跨调用累积直到换行 → `complete_io`。此设计避免了 VtInputThread 独立线程，但要求 I/O 循环在 pending 期间自旋 `on_idle()` 而非调用 `read_io`（因 ConDrv 有 pending I/O 时 `IOCTL_READ_IO` 阻塞）。

2. **手动 Echo**: 原始依赖 `ENABLE_ECHO_INPUT` 模式下的驱动自动回显。corehost 在 `on_idle` 中通过 `WriteFile(vt_out)` 手动回显字符和 `\r\n`，确保用户键入即时可见。

3. **退出链路**: 原始通过信号管道的 `SendCloseEvent`。corehost 通过信号线程在管道断开时关闭 `vt_in` → `PeekNamedPipe` 失败 → `_vt_eof=true` → `should_exit()` → I/O 循环退出。

4. **单线程**: 原始有 ConsoleIoThread + VtInputThread + PtySignalInputThread 三个线程。corehost 合并为 run_io_loop 主线程 + signal_thread_proc 信号线程两个线程。
