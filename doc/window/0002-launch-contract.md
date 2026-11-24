# Window Mode 启动契约

## 现有启动方式

`corehost/cli/main.cpp` 当前有三种主要路径：

1. **conhost 模式**
   - ConDrv 启动进程；
   - 命令行携带 `0x...` 形式的 ConDrv `\Server` 句柄；
   - `defterm_entry(handle)` 接收该句柄并处理 `CONNECT`。

2. **COM handoff / ConPTY 模式**
   - `-Embedding` 或 `--server/--signal`；
   - 需要 `libcorepty`、`libconpty` 或 defterm 的 handoff 代码。

3. **client 模式**
   - `corehost <cmd>`；
   - `client_entry` 只调用 `CreateProcess(CREATE_NEW_CONSOLE)`；
   - 它本身没有 ConDrv `\Server` 句柄；
   - 真正的控制台宿主由 Windows 启动的 conhost 实例负责。

## 冲突

代码计划同时要求：

- `window_entry` 的参数与 `client_entry` 相同；
- `window_entry` 在 corehost 进程内维持控制台并创建窗口；
- 不使用 ConPTY；
- 不使用 defterm 或 libcorepty。

`client_entry` 的参数是：

```text
title, client_command_line
```

这两个参数不足以获得控制台服务器。仅凭它们，corehostw 只能启动一个带 `CREATE_NEW_CONSOLE` 的子进程；该子进程的控制台服务器仍然属于系统启动的 conhost，而不是 corehostw。

要在一个进程内实现完整控制台，必须获得：

- ConDrv `\Server` 句柄；
- 处理 `CONNECT`；
- 建立 console API dispatcher 和输入/输出句柄。

这与 `defterm_entry` 的输入契约一致，而与 `client_entry` 不同。

## 可选方案

### 方案 A：window_entry 接收 ConDrv server 句柄（推荐）

```text
ConDrv
  ↓
corehostw.exe 0x...
  ↓
window_entry(condrv_handle)
  ↓
窗口 + 控制台会话
```

优点：

- 单进程；
- 无 ConPTY；
- 完整控制台语义；
- 与现有 conhost 启动模型一致；
- `corehostw` 可以像 conhost 一样被注册/启动。

缺点：

- `window_entry` 参数不是 `client_entry(title, command_line)`；
- 需要明确 corehostw 的 conhost 注册/启动方式。

### 方案 B：window_entry 接收 title/command_line 并启动 client

```text
corehostw title command
  ↓
CreateProcess(CREATE_NEW_CONSOLE)
  ↓
系统启动 conhost
```

优点：

- 参数与 `client_entry` 相同。

缺点：

- 创建出来的控制台不在 corehostw 进程内；
- 无法满足“corehost 维持控制台”；
- 若使用 ConPTY 才能拿回控制台，又违反禁止 ConPTY 的要求。

### 方案 C：corehostw 作为注册的 console host 启动

- 将 corehostw 注册为 conhost/conhost 替代；
- ConDrv 启动 corehostw 时命令行带 server 句柄；
- `window_entry` 使用 server 句柄创建窗口会话；
- 用户直接启动 corehostw 时，另一条 launcher 路径只负责启动客户端，不承担窗口会话。

优点：

- 参数契约、控制台所有权、单进程模型都自洽；
- 可以兼容 conhost 注册。

缺点：

- 需要额外的注册/启动策略；
- `window_entry` 的 server 句柄参数仍然不是 title/command_line。

## 当前决定

在实现窗口入口之前，必须确定使用 A/C 一类的 **server 句柄契约**。否则方案 B 无法实现真正的进程内控制台，继续实现只会得到第二个 conhost 前端或被迫使用 ConPTY。

因此 W1 的入口实现暂缓，先记录此冲突并请求确认。

## 需要确认的问题

1. `corehostw` 是否作为 conhost 的替代/注册 console host 启动？
2. `window_entry` 是否接受 ConDrv server 句柄（方案 A/C），而不是 title/command_line（方案 B）？
3. 用户直接运行 `corehostw` 时，是否允许它只作为启动器（启动系统 conhost + 默认终端），还是必须直接进入窗口模式？
