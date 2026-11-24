# Window Mode 状态

## 已完成

- W0.1：计划、模块边界、状态文档已建立；
- W0.2：启动契约分析已完成，记录于 `0002-launch-contract.md`；
- 已确认当前 `client_entry` 不持有 ConDrv server 句柄，无法直接作为进程内控制台入口。

## 未完成

- W0.2 的最终决策：`window_entry` 是接收 ConDrv server 句柄，还是采用其他启动契约；
- W1：corehostw target 与 main 宏切换；
- W2 及之后所有任务。

## 当前阻塞

`terminal-window-code-plan.md` 同时要求：

- `window_entry` 参数与 `client_entry` 相同；
- corehost 在自身进程内维持控制台；
- 不使用 ConPTY/defterm/libcorepty。

这三项在现有启动模型中不能同时成立。需要确认使用 ConDrv server 句柄的入口契约后，才能正确实现入口和后续窗口会话。

## 下一步

1. 确认启动契约；
2. 实现 corehostw target 和 main 宏切换；
3. 建立最小 top-level 窗口闭环；
4. 添加自动化构建/smoke test。
