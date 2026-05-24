# 架构概览

## 概述

corehost 是 Windows 默认终端协议中替代系统 conhost.exe 的新控制台宿主。支持 x64 和 ARM64（均为 64 位），C++23 编译。通信层依赖 ConDrv 内核驱动（`\Device\ConDrv`）、COM 进程间交接及匿名管道 I/O。

启动时 `wWinMain` 调用 `console::initialize_console_control()` 从 `user32.dll` 获取未公开的 `ConsoleControl` 函数指针（`GetModuleHandleExW` + `GetProcAddress`），然后解析命令行分派到四种模式。

---

## 四种启动模式 — wWinMain 决策

命令行经 `console_arguments` 解析后按以下优先级分派：

### 模式 1: COM 服务器（`-Embedding`）

**检查**: `args.com_server()` — 命令行中是否出现 `-Embedding` token

**执行**:
```c
auto hr = comserver::com_server_entry();
// → 返回 handoff_result{server, vt_in, vt_out, event}
conpty::conpty_entry(std::move(hr.server), std::move(hr.vt_in),
                     std::move(hr.vt_out), std::move(hr.event),
                     0, 0, false, text_measurement_mode::console);
```

**出口**: 双缓冲 ConDrv I/O 循环，阻塞直到驱动断开

### 模式 2: 默认终端（`0x<HANDLE>`）

**检查**: `args.condrv_handle() != 0` — 第一个非程序名参数以 `0x` 或 `0X` 开头

**执行**:
```c
defterm::defterm_entry(ch);
// → CreateEventW(TRUE, FALSE) → set_server_info → run_io_loop
```

**出口**: 若 COM 移交成功 → `WaitForSingleObject(client)` 返回后退出。否则 I/O 循环阻塞至客户端断开

### 模式 3: ConPTY headless（`--server <hex>`）

**检查**: `args.is_headless()`

**执行**:
```c
auto server = win32::handle{reinterpret_cast<HANDLE>(args.server_handle())};
auto ev = win32::handle{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
miniio::set_server_info(server.view(), ev.view());
conpty::conpty_entry(std::move(server),
                     win32::handle{GetStdHandle(STD_INPUT_HANDLE)},
                     win32::handle{GetStdHandle(STD_OUTPUT_HANDLE)},
                     std::move(ev), args.width(), args.height(),
                     args.inherit_cursor(), text_measurement_mode::console);
```

**出口**: 双缓冲 ConDrv I/O 循环。stdin/stdout 直接作为 PTY 管道使用

### 模式 4: 客户端启动（其他）

**检查**: 无条件 — 前三项均不匹配时到达

**执行**:
```c
auto cmdline = args.client_command_line();
client::client_entry(cmdline.empty() ? shell::get_shell_path() : std::wstring(cmdline.data(), cmdline.size()));
// → CreateProcessW(cmd, ..., CREATE_NEW_CONSOLE, ...)
//   新进程的 ConDrv 控制台触发新的 corehost 0x<HANDLE> 进程
```

**出口**: `CreateProcess` 返回后立即退出

---

## 模块分层与依赖

每层仅依赖下方的层：

```
cli/        — wWinMain, 命令行解析, 四种模式分派, comserver→conpty 桥接
comserver/  — com_server_entry(), COM 类工厂, EstablishHandoff, do_handoff
defterm/    — defterm_entry(), handle_connect, attempt_handoff, try_handoff_all
client/     — client_entry(), CreateProcess(NEW_CONSOLE)
conpty/     — conpty_entry(), pipe_bridge (PTY管道桥+VT解析集成), vt_parser (VT序列解析器),
              vt_input_engine (vt_message→INPUT_RECORD), input_buffer, screen_buffer,
              pty_forward_handler (8种消息 + Console API), console_state, io_state
miniio/     — io_msg, read_io, complete_io, accept_connection, run_io_loop,
              prepare_completion, set_server_info, signal_thread_proc
win32/      — handle(RAII), event(RAII), com_apartment(RAII),
              duplicate_self(), duplicate_handle()
```

**关键约束**: comserver 和 conpty 之间零 `#include` 依赖。桥接仅发生在 `main.cpp` 中 `handoff_result` → `conpty_entry()` 的句柄传递。

---

## 核心数据结构

### io_msg

```c
struct io_msg {
    CD_IO_COMPLETE   complete;       // 完成信息（Identifier + IoStatus + Write 指针）
    CD_IO_DESCRIPTOR descriptor;     // 消息头（Identifier + Process + Object + Function + sizes）
    BYTE             body[4096];     // 消息体
};
```

### io_handles

```c
struct io_handles {
    win32::handle input;   // \Input 客户端句柄
    win32::handle output;  // \Output 客户端句柄
};
```

### handoff_result（comserver → main → conpty 桥接）

```c
struct handoff_result {
    win32::handle server;   // ConDrv \Server 句柄副本
    win32::handle vt_in;    // 从 WT 读取 (WT 输出写端 → corehost 读端)
    win32::handle vt_out;   // 写入 WT (corehost 写端 → WT 读端)
    win32::handle event;    // ManualReset 输入就绪事件副本
    miniio::io_handles handles;  // accept_connection 返回的 \Input/\Output 句柄
    com::com_ptr<ITerminalHandoff3> terminal;  // 延迟到主线程析构
};
```

### signal_thread_params

```c
struct signal_thread_params {
    win32::handle pipe;      // 信号管道读端
    win32::handle vt_in;     // PTY 读端, 断开时 close 打断 PeekNamedPipe
};
```

管道断开时信号线程关闭 `vt_in`，触发主 I/O 循环退出链路：
`vt_in.close() → PeekNamedPipe 失败 → _vt_eof=true → should_exit()=true → 循环退出`

---

## RAII 保证

| 类型 | 构造 | 析构 |
|------|------|------|
| `win32::handle` | 接收原始 HANDLE | `CloseHandle` |
| `win32::event` | `CreateEventW` | `CloseHandle` |
| `win32::com_apartment` | `CoInitializeEx(MTA)` | `CoUninitialize` |
| `done_guard` | 保存 `event&` | `event.set()` |
| `winrt::com_ptr<T>` | `AddRef` | `Release` |
| `cls_obj` | 保存 `&cookie` | `CoRevokeClassObject(cookie)` |

`done_guard` 是 comserver 特有的 RAII：在 `EstablishHandoff` 入口创建，析构时调用 `done_ev.set()`。保证 COM 故障时主线程不会永久阻塞。

---

## 关键 API 封装

| 函数 | 等价于 |
|------|--------|
| `win32::duplicate_self()` | `DuplicateHandle(GetCurrentProcess(), GetCurrentProcess(), GetCurrentProcess(), &h, SYNCHRONIZE, FALSE, 0)` |
| `win32::duplicate_handle(HANDLE src)` | `DuplicateHandle(GetCurrentProcess(), src, GetCurrentProcess(), &h, DUPLICATE_SAME_ACCESS, FALSE, 0)` |
| `winrt::create_instance<T>(clsid, ctx)` | `CoCreateInstance(clsid, nullptr, ctx, IID_PPV_ARGS(&raw))` |
| `hnd.as<U>()` | `QueryInterface(IID_IU, &raw)` |
| `winrt::make<T>(args...)` | `new T(args...)` + `IUnknown` 自动生成 |
| `condrv::create_client_handle(server, name)` | `NtOpenFile(RootDirectory=server, ObjectName=name, Access=GENERIC_READ\|WRITE\|SYNCHRONIZE, Share=R\|W\|D, FILE_SYNCHRONOUS_IO_NONALERT)` |

---

## ConDrv IOCTL 速查

| IOCTL | 值 | Method | 说明 |
|-------|-----|--------|------|
| READ_IO | `0x500004` | METHOD_OUT_DIRECT | 读取消息 + 提交上轮完成 |
| COMPLETE_IO | `0x500008` | METHOD_NEITHER | 完成 CONNECT |
| SET_SERVER | `0x50001C` | METHOD_NEITHER | 注册事件 |

---

## Function 消息类型速查

| 值 | 名称 | defterm | conpty |
|----|------|---------|--------|
| 0x01 | CONNECT | handle_connect | on_connect → accept_connection |
| 0x02 | DISCONNECT | 清空 handles | 清空 handles |
| 0x03 | CREATE_OBJECT | NtOpenFile | NtOpenFile |
| 0x04 | CLOSE_OBJECT | 确认 | 确认 |
| 0x05 | RAW_WRITE | 丢弃（返回 InputSize） | WriteFile→终端 |
| 0x06 | RAW_READ | EOF（0字节） | 非阻塞 PeekNamedPipe → on_idle 跨调用累积直到换行 |
| 0x07 | USER_DEFINED | STATUS_UNSUCCESSFUL | Console API 处理 |
| 0x08 | RAW_FLUSH | 确认 | 确认 |

---

## COM CLSID 速查

| 名称 | CLSID |
|------|------|
| corehost | `{47A3A1A0-2D3C-4F5E-8B1A-9C3D4E5F6A7B}` |
| WT 稳定版 | `{2EACA947-7F5F-4CFA-BA87-8F7FBEEFBE69}` |
| WT Preview | `{06EC847C-C0A5-46B8-92CB-7C92F6E35CD5}` |
| WT Canary | `{A854D02A-F2FE-44A5-BB24-D03F4CF830D4}` |
| WT Dev | `{1F9F2BF5-5BC3-4F17-B0E6-912413F1F451}` |

---

## COM IID 速查

| 接口 | IID |
|------|-----|
| IConsoleHandoff | `{E686C757-9A35-4A1C-B3CE-0BCC8B5C69F4}` |
| ITerminalHandoff3 | `{6F23DA90-15C5-4203-9DB0-64E73F1B1B00}` |
| IDefaultTerminalMarker | `{746E6BC0-AB05-4E38-AB14-71E86763141F}` |

---

## 注册表

```
路径: HKEY_CURRENT_USER\Console\%%Startup
  DelegationConsole   REG_SZ  — 第一跳目标 CLSID
  DelegationTerminal  REG_SZ  — 第二跳目标 CLSID
```

## 线程模型

| 线程 | 生命周期 | 职责 |
|------|---------|------|
| 主线程 | 进程入口到退出 | 初始化 → 分派 → 桥接 |
| COM RPC（系统池） | -Embedding 模式下 | CreateInstance + EstablishHandoff |
| 信号线程 | 移交成功后创建 | 读取信号管道 → ConsoleControl |
| I/O 线程 | defterm/conpty 入口 | ConDrv 消息循环（调用线程自身） |

I/O 循环在 defterm 或 conpty 的调用线程上同步执行（无独立 I/O 线程）。信号线程在移交成功后异步启动。
