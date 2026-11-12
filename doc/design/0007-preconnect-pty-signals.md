# 0007 — 连接前 PTY 信号的暂存语义

状态：已实现

## 背景

PTY 信号管道在客户端第一次 CONNECT 之前就可能收到：

- `ShowHideWindow`
- `ResizeWindow`
- `SetParent`
- `ClearBuffer`

客户端尚未连接时，控制台输入/屏幕缓冲区还没有可用的连接对象。系统宿主对这些
信号的处理不同：`SetParent` 可以立即更新窗口 owner；`ShowHideWindow` 和
`ResizeWindow` 必须暂存到连接建立后再应用；`ClearBuffer` 在连接前没有可清空的
会话状态，直接忽略。

## 行为

第一次 CONNECT 之前：

- `ShowHideWindow` 只记录最后一个可见性值，不调用 `ShowWindow`；
- `ResizeWindow` 只记录最后一个尺寸，不修改 screen buffer，也不产生
  `WINDOW_BUFFER_SIZE_EVENT`；
- `ClearBuffer` 丢弃；`WM_SIZE`/`WM_SHOWWINDOW` 产生的可见性事件也不向终端上报；
- `SetParent` 立即更新 `GWLP_HWNDPARENT`。

第一次 CONNECT 成功后：

- 先应用暂存的 resize；
- 再应用暂存的 show/hide；
- 之后到达的信号立即应用。

## 证据

- 日志中 `SetParent`、`ShowHideWindow` 出现在首次 CONNECT 之前；此前的实现会
  在连接前直接 `ShowWindow`。
- 独立测试 `ConPTY.PseudoWindow`：
  - 未连接时发送 show/hide，窗口状态不变化；
  - 连接后发送 show/hide，`IsIconic` 变化。

## 取舍

暂存只保留最后一个 show/hide 和最后一个 resize。与连接前的窗口状态相比，中间
值没有可观测意义；保留最后一个值即可。

`ClearBuffer` 在连接前丢弃，因为此时没有活动的屏幕缓冲区可清空。

## 外部可观测差异

无。该实现以系统宿主的连接前/后信号时序为准。
