# 05 — 从 5e9a3bf 到当前的回归审计与重建计划

状态：进行中（Round 1）
基线：`5e9a3bf`（最后确认可正常启动 WT 的提交）
当前：`dev-261111` 的 HEAD
证据：`corehost_1794449426_0000012856.log`

本计划只描述要验证的行为、顺序和验收方式，不描述具体代码改动。任何代码改动必须
先有可复现证据，再在独立分支修复，提交后按 diff 自我 review。

## 1. 问题陈述

当前 corehost 作为 OpenConsole/默认终端宿主启动时，WT 窗口一闪即退出。已知：

- `5e9a3bf` 不出现该现象；
- 现象与 ACP 无关；
- 日志显示 VT 输入管道被对端关闭后，corehost 按会话退出路径正常退出；
- 不能把“WT + corehost 作为 OpenConsole”当作开发中的测试手段。

因此必须从两个方向同时取证：

1. 比较 `5e9a3bf` 与当前 HEAD 的可执行行为差异，找出哪一类变化会让宿主或客户端
   提前关闭 VT 输入；
2. 从 `terminal/` 的原版实现和协议语义推导：哪些行为是宿主/终端合理期待的，哪些
   是当前 corehost 多做、少做或做错的。

“原版 conhost 合理的行为必须一致；原版本身是 BUG 或 corehost 不需要的功能不实现。”
源码中不得提到原版 conhost 的代码、函数名或文件路径。

## 2. 交付物

1. `build/audit-5e9a3bf-head.diff`：过滤掉 `tests/`、`doc/`、`third_parties/` 的完整 diff。
2. `build/audit-5e9a3bf-head-files.txt`：变更文件清单和逐文件行数。
3. 本计划 + 审计矩阵（按子系统记录：行为、证据、结论、动作）。
4. 每个确认的 BUG：
   - 独立分支；
   - 最小复现测试；
   - 修复；
   - 修复前后证据；
   - 合并回主分支并删除临时调试代码。
5. 源码中记录“可外部观测的 conhost 行为差异”，不得记录源码内部实现差异。
6. 全量 `ctest` 通过；新增测试必须可自动化。
7. `doc/tasks/00-board.md`、`doc/tasks/05-final-status.md` 与本计划同步更新。

## 3. 不可协商约束

| 约束 | 验收 |
| --- | --- |
| 不新增可变全局、lambda、`std::mutex` | 审查；锁用 `common/win32` |
| 不联网 | 全程不调用搜索/下载 |
| 不捕获异常 | 审查 `catch` |
| 不设置等待超时兜底 | 审查等待调用 |
| 致命错误抛出/abort，非致命错误返回 | 错误矩阵 |
| 测试全自动 | `ctest` |
| 每个子任务一次提交 + 状态记录 | git log / board |
| 不把 WT+corehost 作为测试环境 | 测试只使用 libconpty/独立 harness |
| 代码与 conhost 源码解耦 | 不出现源码内部名称、路径、注释引用 |
| 只实现 conhost 存在的行为（CMake 选项控制的除外） | 功能清单 |
| 外部可观测差异才记录 | 差异表 |

## 4. 审计顺序（代码无关）

### P0 证据与构建基线

- 固定 `5e9a3bf` 与当前 HEAD 的 commit id、编译选项、测试命令。
- 生成过滤后的 diff；确认测试/文档改动不参与行为判断。
- 读出日志中所有生命周期事件：CONNECT、signal、窗口创建/可见性、EOF、close、
  session exit、线程退出。
- 建立“正常启动”和“一闪退出”的最小外部观测指标：进程存活时间、VT 输入是否
  被对端关闭、窗口是否创建、CONNECT 是否完成、伪窗口 owner/可见性。

### P1 WT 提前关闭的直接原因

按以下假设逐项排除，每项都必须有独立测试或日志证据：

1. **VT 输入端由谁关闭**：客户端、WT、还是 corehost 自己关闭了某个句柄。
2. **CONNECT / handoff 生命周期**：corehost 是否过早完成或关闭 handoff；
   返回给 WT 的进程/句柄是否在其生命周期内有效。
3. **伪窗口生命周期**：初始尺寸、owner、可见性、`WM_ACTIVATE`、`WM_CLOSE`、
   `WM_DESTROY` 是否导致 WT 认为终端窗口已销毁或不可用。
4. **信号处理**：ShowHide、SetParent、Resize 的顺序与次数；是否在 CONNECT 前
   执行了不合法窗口操作；是否把内部可见性回显给 WT 导致反馈环。
5. **I/O 退出条件**：`vt_in` EOF、信号管关闭、`should_exit` 的判定是否过早；
   是否把“一个输入源 EOF”误判为“整个会话应退出”。
6. **完成/挂起**：completion 是否及时、是否重复提交、是否把连接状态破坏。
7. **进程/句柄所有权**：是否有句柄被提前关闭、重复关闭或跨进程语义错误。

### P2 逐子系统审查 diff 每一行

按以下顺序，每个文件至少回答：改了什么、为什么改、是否 required、是否有测试、
是否引入生命周期/线程/协议风险。

1. `common/`：线程、事件、SRWLock、handle、错误、取消 I/O、COM 基础。
2. `corehost/defterm/`：入口、handoff、signal 转发、客户端通知、终端握手。
3. `corehost/comserver/`：COM 生命周期、handoff 返回值、进程句柄。
4. `libcorehost/conpty.cpp` 与 `message_router`：装配顺序、线程启动/停止、锁、
   事件等待、退出条件、信号到窗口的路径。
5. `libcorehost/io_threads` 与 `signal`：输入源线程、精确读取、缓冲区/状态归属、
   EOF/关闭路径。
6. `libcorehost/pseudo_window`：类/样式、创建尺寸、owner、可见性、activate、
   close/destroy、消息抑制、0×0 取舍。
7. `libcorehost/pipe_bridge` / `pending_io_state` / `api_*`：ConDrv 请求/完成、
   输入缓冲、Console API 语义、VT 输出、resize、控制事件。
8. `libcorehost/io_state` / `condrv_io` / `connect_completion`：连接、对象、
   进程列表、completion 所有权。
9. `libcorehost/char_convert` / `vt_parser` / `input_engine`：字符集、换行、
   宽度、SGR、鼠标、键盘。
10. 构建/清单：CMake 选项、manifest、版本资源、ASAN 开关。

### P3 功能分类

对每项行为打标签：

- **必须实现**：conhost 有且依赖方/宿主可观测；corehost 必须一致。
- **可以不实现**：conhost 有但 corehost 明确无窗口/无 UIA 等；必须记录差异。
- **禁止实现**：conhost 没有，或属于调试/特例/垃圾兼容。
- **待确认**：证据不足，停止并向用户询问。

分类结果进入 `doc/tasks/00-board.md`，每个“必须实现”必须有测试。

### P4 修复与重建

对 P1/P2 确认的问题按依赖顺序修复：

1. 生命周期/句柄/退出条件（先保证会话不提前退出）。
2. 协议/completion/IO 正确性。
3. 窗口对外契约。
4. API/VT/字符集语义。
5. 测试与文档。

每个修复：

- 从当前 HEAD 开独立分支；
- 先写最小自动化测试并确认失败；
- 只改真正原因，不加特例/补丁；
- 跑相关测试 + 全量 ctest；
- 删除调试输出、无效代码；
- 提交后按 `git show` 自我 review；
- 合并后更新 board/本计划。

## 5. 行为差异记录格式

每个差异一条，必须外部可观测：

| 编号 | 场景 | conhost 行为 | corehost 行为 | 是否实现 | 证据 |
| --- | --- | --- | --- | --- | --- |

禁止记录“某函数不同/某文件没有”的源码级差异。

## 6. 验收标准

- WT 提前退出问题有明确根因和修复证据；不使用 WT+corehost 作为测试。
- 过滤后的 diff 每一行都有审查结论（保留/修改/删除）。
- 所有“必须实现”的行为有自动化测试。
- `ctest -C Debug` 全绿。
- 没有新增全局可变状态、lambda、`std::mutex`、`catch`、超时兜底。
- 源码不出现原版 conhost 的源码内部名称。
- 文档记录所有外部可观测差异。

## 7. 需要停下来询问的情况

- 无法从日志/独立测试区分“WT 关闭输入”和“corehost 提前退出”。
- 需要改变协议/API 语义但无法从 conhost 行为或现有文档确定。
- 发现 `5e9a3bf` 与当前测试期望冲突，无法判断哪边正确。
- 修复需要引入被禁止的机制（全局、lambda、std::mutex、超时）。

## 8. 当前进度

| 子任务 | 状态 | 备注 |
| --- | --- | --- |
| 读取 agent.md、基线/HEAD/日志 | DONE | 5e9a3bf；日志 35345 字节 |
| 生成过滤 diff | DONE | `build/audit-5e9a3bf-head.diff`（427255 字节，58 文件） |
| 日志生命周期排序 | DONE | SetParent/ShowHide 早于首次 CONNECT |
| P1 根因排除 | FIXED (independent evidence) | 连接前 ShowHide/Resize 改为暂存；pre/post-connect 独立用例通过；待用户用 WT 外部复验 |
| P2 文件级审查 | IN PROGRESS | 已完成 conpty/message_router/io_threads/signal/pseudo_window；窗口操作已收敛到窗口线程 |
| 独立复现测试 | DONE (lifecycle) | pre/post-CONNECT 窗口信号用例 |
| 修复/提交 | TODO | 每项独立 |
| 全量验证/文档 | TODO | ctest + board |
## 9. 未决问题：READ_IO 前的关闭检查

`message_router::run()` 在进入 `READ_IO` 前目前使用
`WaitForSingleObject(close_requested, 0)`，用于避免关闭事件先于 `READ_IO`
到达而丢失。这是探测式等待，严格来说不符合“不轮询/不探测”的约束。

尝试过用 `InputAvailableEvent + close` 的无限等待替代，但单独验证时：

- `ConPTY.Differential` 失败/超时；
- `Defterm.Connect` 超时并留下 corehost 进程。

因此暂不替换。需要决定：接受这一处生命周期检查，还是引入“read entered”
事件/独立关闭监视线程来消除探测。未决期间保持当前已通过的实现。
## 10. 当前 WT 复验线索

新日志显示 `vt_in` 在 PowerShell banner 之后关闭，而客户端进程退出码尚未记录。已重新加入
最小诊断：

- `on_connect` 记录客户端镜像；
- `on_input_eof` 记录 `GetExitCodeProcess`。

独立测试中，交互式 `pwsh.exe` 在连接前/后收到 SetParent、ShowHide、Resize 后仍保持运行，
说明不是这些窗口信号直接导致客户端退出。下一步需要 WT 复验日志中的客户端退出码，以区分：

1. 客户端主动退出；
2. WT 在输出/客户端条件不满足时关闭输入；
3. corehost 提前关闭句柄。