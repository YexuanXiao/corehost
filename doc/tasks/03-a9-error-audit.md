# A9：错误分类审计（新建代码路径）

状态：DOING（A 阶段新代码已审计；输出侧 `noexcept` 路径与旧辅助函数未覆盖，见第 3 节）

## 1. 审计范围

A 阶段新增/改写的代码路径：

- `common/win32/cancellable_read.hpp`、`thread.hpp`、`srwlock.hpp`
- `libcorehost/io_threads.hpp`（输入线程、信号线程）
- `libcorehost/io_loop.hpp`（事件驱动主循环）
- `libcorehost/pipe_bridge.hpp`（`on_input_bytes`/`on_input_eof`/`on_signal_closed`）
- `corehost/defterm/signal.cpp`（`read_and_forward`、信号转发线程）、`terminal_handoff.hpp`、`defterm.cpp` 初始循环

## 2. 逐项结果

| 调用 | 失败处理 | 分类 |
| --- | --- | --- |
| `ReadFile`（cancellable_read::read） | 0 字节 → closed；`ERROR_OPERATION_ABORTED` → 停止请求则 cancelled，否则致命；`broken_pipe`/`pipe_not_connected`/`no_data` → closed；其余 throw | 正确 |
| `CancelSynchronousIo` | `ERROR_NOT_FOUND` → 返回 false（等事件重试语义），其余 throw | 正确 |
| `SetEvent`（signal_state / 管道关闭） | 失败 throw | 正确 |
| `WaitForSingleObject` / `WaitForMultipleObjects` | 经 `wait_one`/`wait_any`；abandoned 按致命处理；timeout 不可能（INFINITE） | 正确 |
| `SetThreadDescription` | 失败 throw → 线程内 → terminate（致命，符合策略） | 正确 |
| `CreateThread` / `CreateEvent` | 失败 throw | 正确 |
| 协议损坏（半条消息、未知 id/code、畸形 dwSize） | 抛 `invalid_state` → 会话终止 | 正确（会话级失败） |
| `read_exact` 半条消息后 EOF | 调用方区分 cancelled 与 closed，closed 且已读部分 → `invalid_state` | 正确 |

## 3. 尚未覆盖 / 遗留

1. **输出侧 `vt_output_buffer::flush`** 是 `noexcept`，写 `vt_out` 失败时的行为需要确认（是否记录并终止）。它属于既有输出路径，不在 A 阶段改动范围内，但按"任何可能失败的调用都必须检查"的要求必须处理，记入 A9 收尾。
2. `pipe_bridge` 中已无调用方的 `on_idle`/`drain_available_vt_input`/`wait_for_pending_vt_input`/`prepare_console_input_events` 及 `pipe_bridge_io` 的 peek 辅助函数仍是旧实现（内部有 `peek_named_pipe`），属于死代码；删除它属于 A6b 收尾，避免后续误用。
3. `defterm.cpp` 的 `connect_handler::on_idle()` 是空实现，仍被初始循环调用；可以在 A6b 一起删除调用点。

## 4. 结论

A 阶段新代码没有"忽略返回值"的调用点；致命/非致命边界与 `design/0001` 一致。第 3 节三项在 A6b/A9 收尾中处理。
