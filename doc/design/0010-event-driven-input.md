# 事件驱动的终端输入线程

## 背景

WT 通过 winconpty 传入的 `vt_in` 是异步 duplex pipe。conhost 的
`VtInputThread` 在 `FILE_SYNCHRONOUS_IO_*` 未设置时使用 `OVERLAPPED`
`ReadFile`；corehost 采用同样的句柄能力判断，不使用轮询。

同步 `CreatePipe` 句柄仍用于测试和部分宿主，继续走阻塞 `ReadFile` +
`CancelSynchronousIo`。

## 实现

- 用 `NtQueryInformationFile(FileModeInformation)` 判断句柄是否异步；
- 异步句柄：
  - `ReadFile` 带 `OVERLAPPED` 和 completion event；
  - 同时等待 completion event 与停止事件；
  - 停止时 `CancelIoEx` 打断挂起读；
- 同步句柄：
  - 阻塞 `ReadFile` + `cancellable_read` 取消协议；
- 不设置 16 ms/0 ms 时间片；无数据时不轮询。

## 验证

- `ConPTY.Programs`、`ConPTY.PseudoWindow`、`ConPTY.IdleCpu`、`Defterm.Connect`；
- 真实 WT 启动、输入、回车、快速连续命令；
- 全量 ctest 26/26。
