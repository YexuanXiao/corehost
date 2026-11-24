# Window Mode 状态

## 已完成

- W0.1：计划、模块边界、状态文档已建立；
- W0.2：启动契约已确定。`window_entry(title, command_line)` 先创建 ConDrv `\Server`，建立进程内控制台会话和窗口，再启动 client；等价于 `conhost.exe cmd.exe`，不使用 ConPTY/defterm/libcorepty。

## 未完成

- W1：corehostw target 与 main 宏切换；
- W2 及之后所有任务。

## 下一步

1. 添加 `\Device\ConDrv\Server` 和客户端句柄创建的公共封装；
2. 实现 corehostw target 和 main 宏切换；
3. 建立最小 top-level 窗口闭环；
4. 接入进程内控制台服务器；
5. 添加自动化构建/smoke test。
