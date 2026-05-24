# ConPTY I/O Model (2026-05-22)

## 架构概览

```
                         ┌─── WT ───┐
                         │ Terminal │
                         └────┬─────┘
                    vt_in(R) │ │ vt_out(W)     signal_pipe(W)
              ┌──────────────┘ │ ┌──────────────────┘
              ▼                ▼ ▼
┌──────────────────────────────────────────────────────┐
│                  corehost.exe                         │
│                                                       │
│  ┌─ Signal Thread (独立) ──────────────────────────┐  │
│  │  ReadFile(signal_pipe) 阻塞                      │  │
│  │    → PtySignal::ResizeWindow → state/sbuf        │  │
│  │    → PtySignal::ClearBuffer  → sbuf              │  │
│  │    → PtySignal::ShowHideWindow → no-op           │  │
│  │    → PtySignal::SetParent → no-op                │  │
│  │  管道断 → vt_in.clear() + pipe_broken=1          │  │
│  └──────────────────────────────────────────────────┘  │
│         │ pipe_broken 原子标志                         │
│         ▼                                              │
│  ┌─ Main I/O Thread ──────────────────────────────┐  │
│  │  loop:                                           │  │
│  │    WaitForSingleObject(ConDrvEvent, 16)          │  │
│  │    read_io(server) → ConDrv 消息                  │  │
│  │      Function=0 → on_idle()                       │  │
│  │        PeekNamedPipe(vt_in)→process_input()      │  │
│  │        if avail==0 && pipe_broken → _vt_eof      │  │
│  │      Function=7 → dispatch → api_handlers         │  │
│  │      Function=5 → handle_raw_write()              │  │
│  │      Function=6 → handle_raw_read()               │  │
│  │      Function=1 → handle_connect()                │  │
│  └──────────────────────────────────────────────────┘  │
│                                                       │
│  ┌─ Shared State ──────────────────────────────────┐  │
│  │  console_state / screen_buffer / input_buffer    │  │
│  │  ⚠️ 两个线程共享访问 (无锁，依赖低竞争 + 原子)    │  │
│  └──────────────────────────────────────────────────┘  │
└──────────────────────────────────────────────────────┘
```

## 为什么信号线程不可移除

尝试将 PtySignal 处理融入 `on_idle()` 主循环（非阻塞 `PeekNamedPipe` + `ReadFile`）失败的根因：

**信号线程的 `ReadFile(signal_pipe)` 阻塞是 ConDrv 协议时序的一部分**。

ConDrv 的消息分派与信号管道之间存在隐式同步依赖——信号管道读端必须在独立线程中保持就绪读取状态，否则会阻塞 WT 与 ConPTY 之间的初始化握手。

具体表现：移除信号线程后，cmd.exe 启动后的 `CONSOLE_IO_CONNECT` 和 `RAW_WRITE` 消息永不抵达 corehost（28 秒超时后 WT 杀死进程）。

**结论**：信号线程必须保留为独立线程。`on_idle()` 的非阻塞 PeekNamedPipe 无法替代阻塞 `ReadFile`。

## 退出路径

```
WT 关闭 → signal_pipe 对端关闭 → 信号线程: ReadFile 失败
  → pp->vt_in.clear() (副本) + pipe_broken->store(true)
  → on_idle: PeekNamedPipe(vt_in) avail==0 + pipe_broken=true
    → _vt_eof = true → should_exit() = true → I/O 循环退出
```

**已知剩余问题**：信号线程只能关闭 vt_in 副本，真正的 bridge.vt_in 在 WT 退出后才断裂。`pipe_broken` 原子标志作为补充通知机制，有最多 16ms 延迟（on_idle 的 WaitForSingleObject 超时间隔）。

## 两个 Read 模式

| 模式 | `_pend_kind` | 触发者 | process_input 行为 |
|------|:---:|------|-------------------|
| ConsoleRead | `ConsoleRead` | cmd.exe `ReadConsole` | `_edit_*` 编辑 + echo |
| PowerShell | `None` | PowerShell `GetConsoleInput(PEEK)` | `KEY_EVENT` → `input_buffer` |
