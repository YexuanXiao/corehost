# WT 输入与并发 GetConsoleInput

## 背景

Windows Terminal 通过 `winconpty` 传入同一 duplex pipe 作为 stdin/stdout。
WT 启动后不会立即发送输入；当用户按键时，PSReadLine 可能同时发出多个可等待
`GetConsoleInput`。

## 修复

- `vt_input_thread` 先用 `PeekNamedPipe` 判断可读，再 `ReadFile`。
  直接对 WT 的 `vt_in` 做阻塞 `ReadFile` 会导致会话在 WT 尚未结束输入时进入 EOF。
- `pipe_bridge` 为多个可等待 `GetConsoleInput` 维护 FIFO；前一个完成后按顺序
  启动下一个，避免覆盖 `pending_io_state`。
- `message_router` 不再把“旧 pending 已完成后的新 pending”判为状态损坏。

## 外部可观测行为

- WT 不会在启动后立即退出；
- 输入 `e`、回车不会关闭会话；
- 键盘输入仍转换为 `INPUT_RECORD`，PowerShell/PSReadLine 能继续工作。

## 已知差异

输入线程使用 `PeekNamedPipe` + 16ms 空闲等待，而不是把 WT 的异步管道当作
可阻塞句柄；这是 WT 能正常工作的必要条件。等待只用于空闲，不用于放弃线程退出。
