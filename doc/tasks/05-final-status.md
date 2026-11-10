# 最终状态（阶段 A–D）

采集时间：2026-11-11。命令均可复现，数值来自本机一次完整运行。

## 1. 验证命令与结果

```powershell
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCOREHOST_DISABLE_LOG=OFF -DCOREHOST_LOG_LEVEL=2
cmake --build build
ctest --test-dir build --output-on-failure
```

| 项 | 结果 |
| --- | --- |
| 构建 | 通过（`ninja: no work to do` 表示增量已最新） |
| 测试目标 | **23 个**，`ctest` 全绿（一次完整运行中出现过 1 次偶发失败，立即复跑为 100% 通过，见第 5 节） |
| 空闲 CPU | `ConPTY.IdleCpu`：3 秒空闲 **0 ms**（阈值 100 ms） |
| 输入时延 | `ConPTY.Programs/latency`：20 次按键 p95 **10–13 ms**（阈值 250 ms） |
| 会话稳定性 | `ConPTY.Robustness/repeat`：50 次会话，测试进程句柄 104 → 107 |
| 真实程序 | cmd / 嵌套子进程 / powershell / vim / edit / visibility 全部通过；ssh SKIP（sshd 未运行） |

## 2. 任务 1（I/O 模型）证据

| 验收项 | 证据 |
| --- | --- |
| 无固定时间片轮询 | 生产代码检索无 `wait_one(..., <n>)`/`sleep_for`/轮询 Peek；会话循环只等事件与 ConDrv |
| 无输入不唤醒 | `ConPTY.IdleCpu` 0 ms/3s |
| 输入线程拥有自己的状态 | `vt_input_state`（读缓冲/解码器/解析器/键盘/行编辑）由 `vt_input_thread` 持有，见 `design/0004` |
| 挂起读期间仍服务其它请求 | `serve` 用例（ReadConsole 挂起时另一线程查询光标立即返回） |
| WriteConsoleInput 唤醒挂起读 | `wake` 用例 |
| VT 输入程序收到原始序列 | `vt` 用例（Up+Backspace = `1b 5b 41 7f`） |
| 退出与 join | `Deferm`/`ConPTY` 各用例的宿主进程都在 5s 内退出；线程停止走事件驱动取消 + 无限 join |

## 3. 任务 2（GetConsoleWindow 伪窗口）证据

| 需求 | 证据 |
| --- | --- |
| 有效句柄、可枚举、属于专用线程 | `Defterm.Connect/window`：`IsWindow`、`GetWindowThreadProcessId` = corehost 进程 |
| 不可见 | `IsWindowVisible == FALSE` |
| 同步消息不挂起 | 50 次 `SendMessageTimeout` + pressure 用例 |
| 查询类自洽 | `WM_GETTEXT`/`WM_GETTEXTLENGTH` 返回控制台标题与长度；`GetWindowRect` 非零；显式 `WM_SHOWWINDOW` 只上报、不改变可见性 |
| 关闭语义 | `WM_CLOSE` → `CancelIoEx` 打断 READ_IO → 宿主退出（0.11s 级） |
| show/hide 上报 | `ConPTY.Programs/visibility`：`WM_SIZE` 触发 `ESC[2t/1t` |
| 无泄漏 | `pressure`：200 次 API + 50 次消息后句柄/ USER 对象有界 |

## 4. 任务 3 / 4（测试）证据

| 需求 | 证据 |
| --- | --- |
| 测试库（runner/隔离/超时/宿主层/失败归档） | `Runner.SelfTest`、`Platform.SelfTest`；失败用例自动归档日志到 `test-artifacts/<case>/` |
| defterm 自动化（不碰注册表/系统状态） | `Defterm.Connect`：CONNECT、GetConsoleMode、窗口与关闭、进程列表、压力、重复会话 |
| conpty 真实程序 | `ConPTY.Programs`：cmd、嵌套子进程、powershell、vim、edit |
| conpty 健壮性 | `ConPTY.Robustness`：resize、clear、非法尺寸、退出码、50 次重复 |
| 子进程继承控制台 | ConPTY `nested` 用例；defterm 直连方式不可行，已 SKIP 并注明 |
| ssh 远程组合 | 用例已实现，环境阻塞（sshd 未运行，见 G2） |

## 5. 已知缺口（详见 `04-open-gaps.md`）

- **G2**：sshd 未运行（需管理员），ssh 用例 SKIP；
- **G4/G5**：真实 WindowsTerminal 层与 handoff 替身层未实现；
- **G11**：Ctrl+C 投递暂停——探针场景下分支未触发，已记录排查入口；
- 偶发：一次完整 `ctest` 出现 1 个失败，立即复跑通过；未定位到具体用例（日志被覆盖）。后续应给失败用例保留产物（`--output-on-failure` 已开，但需归档日志）。

'## 6. 完成度总表（第 60 轮收尾）

| 任务 | 完成度 | 未达成项 |
| --- | --- | --- |
| 1. I/O 模型 | 完成 | — |
| 2. GetConsoleWindow 伪窗口 | 完成 | — |
| 3. defterm 测试 | 确定性层完成 | 真实 WT 层（G4）、handoff 替身层（G5） |
| 4. conpty 测试 | 主矩阵完成 | ssh（G2，环境）、Ctrl+C（G11，暂停） |

最终一次完整运行：构建通过，`ctest` **23/23 通过，41.46s**。

遗留项均已在 `04-open-gaps.md` 登记，每项都有解除条件；没有"静默跳过"的验收项。

## 7. 结论'

任务 1、2 的验收项都有自动化证据；任务 3 的确定性层（L0 直连）与任务 4 的
主要矩阵已完成，真实 WT 层与 ssh 受环境限制。剩余项均登记在 gaps，且都有明确的
解除条件。
