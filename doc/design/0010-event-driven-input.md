# 事件驱动的终端输入线程

## 句柄判断

WT 通过 winconpty 传入的 `vt_in` 是异步 duplex pipe；conhost 的
`VtInputThread` 在 `FILE_SYNCHRONOUS_IO_*` 未设置时使用 `OVERLAPPED`
`ReadFile`。

corehost 直接声明并调用 `NtQueryInformationFile(FileModeInformation)`，
不使用 `GetProcAddress` 动态加载。

## 两套读路径

`vt_input_thread` 在构造时设置 `_is_overlapped`，提供两套不共享状态的函数：

- `run_overlapped()`
  - 异步句柄专用；
  - `ReadFile` 带 `OVERLAPPED` 和 completion event；
  - 等待 completion event / 停止事件；
  - 停止时 `CancelIoEx` 打断挂起读。
- `run_synchronous()`
  - 同步 `CreatePipe` 专用；
  - 阻塞 `ReadFile` + `CancellableRead` 取消协议。

`start()` 只创建线程；线程入口根据 `_is_overlapped` 选择上述函数。
`stop_and_join()` 同样根据 `_is_overlapped` 选择
`stop_and_join_overlapped()` 或 `stop_and_join_synchronous()`；
两条停止协议也完全独立。

## 验证

- `ConPTY.Programs`、`ConPTY.PseudoWindow`、`ConPTY.IdleCpu`、`Defterm.Connect`；
- 真实 WT 启动、输入、回车、Shift、高频连续命令；
- 全量 ctest 26/26。
