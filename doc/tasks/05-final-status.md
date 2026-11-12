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
| 真实程序 | cmd / screen_erase / 脚本（API、窗口消息、屏幕文本）/ 嵌套子进程 / powershell / vim / edit / ssh（正负路径）/ ssh_pty / ssh_interrupt / visibility 全部通过 |

## 2. 任务 1（I/O 模型）证据

| 验收项 | 证据 |
| --- | --- |
| 无固定时间片轮询 | 生产代码检索无 `wait_one(..., <n>)`/`sleep_for`/轮询 Peek；会话循环只等事件与 ConDrv |
| 无输入不唤醒 | `ConPTY.IdleCpu` 0 ms/3s |
| 输入线程拥有自己的状态 | `vt_input_state` 由会话持有、主线程在 `message_router` 锁内更新；输入线程只负责读管道 |
| 挂起读期间仍服务其它请求 | `serve` 用例（ReadConsole 挂起时另一线程查询光标立即返回） |
| WriteConsoleInput 唤醒挂起读 | `wake` 用例 |
| VT 输入程序收到原始序列 | `vt` 用例（Up+Backspace = `1b 5b 41 7f`） |
| Ctrl+C 语义（对齐 conhost） | `ctrl_c_read`：挂起读以 STATUS_ALERTED 结束（0 字节 + err=995）；单测覆盖"C 不清缓冲 / Break 清缓冲"；依据 `terminal/` 源码 |
| 退出与 join | `Deferm`/`ConPTY` 各用例的宿主进程都在 5s 内退出；线程停止走事件驱动取消 + 无限 join |

## 3. 任务 2（GetConsoleWindow 伪窗口）证据

| 需求 | 证据 |
| --- | --- |
| 有效句柄、可枚举、属于专用线程 | `Defterm.Connect/window`：`IsWindow`、`GetWindowThreadProcessId` = corehost 进程 |
| 不可见 | `IsWindowVisible == FALSE` |
| 同步消息不挂起 | 50 次 `SendMessageTimeout` + pressure 用例 |
| 查询类自洽 | 默认窗口文本返回控制台标题与长度；伪窗口始终 0×0，`ResizeWindow` 只改 screen buffer；显式 `WM_SHOWWINDOW` 仍上报 |
| owner/可见性 | `SetParent` 后 `GWLP_HWNDPARENT`/`GA_ROOTOWNER` 为 owner；`ShowHidePseudoConsole` 改变 `IsIconic` 且不产生 VT 回显；创建期 WM_SIZE 不 CancelIoEx，外部可见性变化仍取消 READ_IO |
| 关闭语义 | `WM_CLOSE` → `CancelIoEx` 打断 READ_IO → 宿主退出（0.11s 级） |
| show/hide 上报 | `ConPTY.Programs/visibility`：`WM_SIZE` 触发 `ESC[2t/1t` |
| 无泄漏 | `pressure`：200 次 API + 50 次消息后句柄/ USER 对象有界 |

## 4. 任务 3 / 4（测试）证据

| 需求 | 证据 |
| --- | --- |
| 测试库（runner/隔离/超时/宿主层/失败归档） | `Runner.SelfTest`、`Platform.SelfTest`；失败用例自动归档日志到 `test-artifacts/<case>/` |
| defterm handoff 协议（替身层） | `Defterm.Handoff`：portable attach 字段、句柄有效性、失败路径 |
| defterm 自动化（不碰注册表/系统状态） | `Defterm.Connect`：CONNECT、GetConsoleMode、窗口与关闭、进程列表、压力、重复会话 |
| conpty 真实程序 | `ConPTY.Programs`：cmd、嵌套子进程、powershell、vim、edit |
| conpty 健壮性 | `ConPTY.Robustness`：resize、clear、非法尺寸、退出码、50 次重复 |
| 子进程继承控制台 | ConPTY `nested` 用例；defterm 直连方式不可行，已 SKIP 并注明 |
| ssh 远程组合 | 已实现：临时 sshd（当前用户、高端口、密钥认证），非交互与交互 PTY 两条用例通过 |

## 5. 已知缺口（详见 `04-open-gaps.md`）

第 64 轮状态：**G1–G15 全部关闭**（G1 RESOLVED，其余 DONE）。

- ~~G2~~：已用临时 sshd 解决；
- ~~G4~~：真实 WT 层的机会式用例已删除（环境依赖、会弹窗）；defterm 由 L0/L1 覆盖；

- ~~偶发失败~~：已定位为**测试驱动 `close()` 的顺序问题**——先 `join` 输出泵、后结束客户端；
  `CancelSynchronousIo` 只能命中正在进行的同步读，读已返回时抓不到，`join` 就死等（表
  现为某用例挂到 120s 超时）。改为先结束客户端并关闭伪控制台（corehost 退出 → vt_out
  关闭 → 泵读到 EOF），再 `join`。修复后 `ConPTY.Robustness` 连续 30 次、全量 `ctest`
  连续 3 次全部通过（约 31s）。

'## 6. 完成度总表（第 60 轮收尾）

| 任务 | 完成度 | 未达成项 |
| --- | --- | --- |
| 1. I/O 模型 | 完成 | — |
| 2. GetConsoleWindow 伪窗口 | 完成 | — |
| 3. defterm 测试 | L0 直连 + L1 handoff 替身（真实终端机会式用例已删除） | 真实 handoff、-Embedding、UI 自动化（gaps §3 决策） |
新增证据：差分测试（corehost vs 系统 conhost）发现并修复 TitleLength 字符数、GetConsoleMode 私有标志两处差异；raw read 改为有字节即返回；VT 屏幕模型与脚本化客户端落地。
| 4. conpty 测试 | 主矩阵 + 语义补齐完成 | 关闭事件（G18 决策不实现）；`ReadConsoleOutputCharacter` 客户端崩溃（G28） |

最终一次完整运行：构建通过，`ctest` **23/23 通过，41.46s**。

遗留项均已在 `04-open-gaps.md` 登记，每项都有解除条件；没有"静默跳过"的验收项。

'## 6.1 验证命令补充

本仓库的 `build/` 是 Visual Studio 多配置生成器，`ctest` 必须带配置：

```powershell
ctest --test-dir build -C Debug --output-on-failure
```

第 66 轮实测：**26/26 通过，约 33s**。相较上一轮：删除环境依赖的 `Defterm.RealTerminal`；`Defterm.Connect` 删除必然 SKIP 的 slow case（7.3s → 0.9s）；`ConPTY.Differential` 默认只跑 corehost 冒烟（18s → 0.2s），与 conhost 比较改为 opt-in（`COREHOST_TEST_DIFFERENTIAL=1`）。测试选项见 `06-test-options.md`。本轮同时统一线程创建（CREATE_SUSPENDED→命名→Resume）、删除线程 id 与延迟命名、共享 ConsoleControl 加载器、扁平化控制事件队列，并把转码集中到 `char_convert.hpp`（UTF-8 按 4N、ANSI 按 DBCS 2M 的粗略上界）。

## 7. 结论

任务 1、2 的验收项都有自动化证据；任务 3 的确定性层（L0 直连）与任务 4 的
主要矩阵已完成，真实 WT 层与 ssh 受环境限制。剩余项均登记在 gaps，且都有明确的
解除条件。