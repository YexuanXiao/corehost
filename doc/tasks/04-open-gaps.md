# 未完成项与阻塞原因

状态：随每个子任务更新。任何"没做完"的东西都必须在这里，而不是散落在提交信息里。

## 1. 明确未完成

| 编号 | 项 | 现状 | 解除条件 | 归属 |
| --- | --- | --- | --- | --- |
| G1 | ~~defterm 直连客户端的 `GetConsoleMode` 失败~~ | RESOLVED：传本进程 STD 句柄会被判未知对象（测试环境没有客户端句柄表）；改传 `nullptr`（object=0）后成功。真实客户端有句柄表，不受影响 | — | C5 |
| G2 | ssh 远程组合用例 | 用例已实现（`ssh -tt localhost` + 标记校验 + 环境 SKIP），但本机 sshd 服务 **Stopped**，启动需管理员 | 以管理员启动 sshd 并为当前用户配置公钥登录；代码无需改动 | D5 |
| G3 | 伪窗口 show/hide 信号 | 未实现：`WM_SHOWWINDOW`/`WM_SIZE` 不上报给终端，`PtySignal::ShowHideWindow` 只消费不发送 | 需要确认 ConPTY 下可见性上报的协议方向（原版走 VT writer，corehost 走信号管道） | B7 |
| G4 | 真实 WindowsTerminal 层（L2） | 未开始：defterm 测试目前覆盖 L0（ConDrv 直连），没有真正拉起 WT 并做端到端交互 | 需要交互式桌面 + 已安装 WT；用例要能探测并 SKIP | C6 |
| G5 | Handoff 替身层（L1） | 未开始：没有测试实现的 COM handoff 接收方 | 需要一个最小 `IConsoleHandoff` 本地服务器；可先在 defterm 分支上做 | C6 |
| G6 | ~~输出侧 `vt_output_buffer::flush` 的 `noexcept` 路径~~ | DONE：对端关闭仍按预期失败丢弃；其它写失败改为记录后 `abort`（致命） | — | A9 |
| G7 | ~~`defterm.cpp` 的 `connect_handler::on_idle()` 空实现~~ | DONE：调用点与方法已删除 | — | A9/A6b |
| G8 | CR/LF 跨批次 lookahead 在线程模型下的归属 | `_on_line_terminator` 会在批次末尾 `PeekNamedPipe` 找 LF；threaded 模式下由输入线程执行，但缺专门用例 | 增加"CR 与 LF 分两次到达"的用例，断言不产生多余行 | A10 |
| G9 | 中断/信号压力测试 | 未做：Ctrl+C/Ctrl+Break 与信号线程交互只在少数用例间接覆盖 | 在 defterm/conpty 两侧补"信号到达时无丢失、无重复"的压力用例 | A10 |
| G10 | ~~20 次以上会话的泄漏观察~~ | DONE：repeat 扩到 50 次（句柄 104→107） | — | A10 |
| G12 | 输入时延用例在隔离子进程下不稳定 | 手动运行稳定（p95 0–1ms），CI 隔离进程里交互式 cmd 首轮可能 EOF；用例暂时 SKIP 并注明 | 找出隔离进程与手动运行的差异（控制台/stdio），或改用非交互式回显程序 | D3 |
| G11 | **Ctrl+C 未投递到前台进程组** | 收到 Win32 Ctrl+C 键事件后，宿主没有生成 `CTRL_C_EVENT`（代码中只有 `GenerateConsoleCtrlEvent` API 透传）；`ctrl_c` 用例因此 SKIP 并注明 | 需要记录 CONNECT 的 ProcessGroupId，并在 processed input 下于会话锁内调用 `GenerateConsoleCtrlEvent(CTRL_C_EVENT, group)` | A 阶段遗留 |

## 2. 已明确不做（有理由）

| 项 | 理由 | 记录 |
| --- | --- | --- |
| 伪窗口渲染、滚动条、菜单、DPI 真实处理、UIA provider | 伪窗口只做消息端点；依赖方不需要渲染 | `design/0002` 第 5 节 |
| 用注册表切换默认终端来测 defterm | 测试必须与系统状态无关；改为 ConDrv 直连 | 看板"约束补充" |
| 为 `common/` 写单元测试 | 用户明确要求；正确性由使用方测试覆盖 | `agent.md` |

## 3. 环境限制（不是代码问题）

| 现象 | 影响 | 记录 |
| --- | --- | --- |
| 受限文件沙箱下 CMake `try_compile` 挂起 | 配置/构建需要完整权限 | 看板基线表 |
| 测试进程写 HKCU 返回 ACCESS_DENIED | 与 defterm 方案无关了（不再写注册表）；未继续排查 | 已删除相关代码 |
| sshd 服务未运行 | D5 用例 SKIP | G2 |
| 本机未验证 WindowsTerminal 交互 | C6 未开始 | G4 |
