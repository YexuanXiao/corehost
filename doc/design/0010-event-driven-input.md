# 事件驱动的终端输入线程

## 句柄判断

WT 通过 winconpty 传入的 `vt_in` 是异步 duplex pipe；conhost 的
`VtInputThread` 在 `FILE_SYNCHRONOUS_IO_*` 未设置时使用 `OVERLAPPED`
`ReadFile`。

corehost 直接声明并调用 `NtQueryInformationFile(FileModeInformation)`，
不使用 `GetProcAddress` 动态加载。

## 两条完全分离的实现

- `overlapped_vt_input_thread`
  - 仅用于异步句柄；
  - `ReadFile` 带 `OVERLAPPED` 和 completion event；
  - 等待 completion event / 停止事件；
  - 停止时 `CancelIoEx` 打断挂起读。
- `blocking_vt_input_thread`
  - 仅用于同步 `CreatePipe` 句柄；
  - 阻塞 `ReadFile` + `CancellableRead` 取消协议；
  - 不与 overlapped 状态、事件或缓冲区混用。

`make_vt_input_thread` 根据句柄模式选择实现，调用方只看到统一的
`vt_input_thread` 接口。

## 验证

- `ConPTY.Programs`、`ConPTY.PseudoWindow`、`ConPTY.IdleCpu`、`Defterm.Connect`；
- 真实 WT 启动、输入、回车、Shift、高频连续命令；
- 全量 ctest 26/26。
