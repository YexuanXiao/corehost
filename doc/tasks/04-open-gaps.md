# 未完成项与阻塞原因

状态：随每个子任务更新。任何"没做完"的东西都必须在这里，而不是散落在提交信息里。

## 1. 已关闭

| 编号 | 项 | 结论 | 归属 |
| --- | --- | --- | --- |
| G1 | defterm 直连客户端的 `GetConsoleMode` 失败 | RESOLVED：传本进程 STD 句柄会被判未知对象（测试环境没有客户端句柄表），改传 `nullptr`（object=0）后成功；真实客户端有句柄表，不受影响 | C5 |
| G2 | ssh 远程组合 | DONE：`tests/lib/ssh_server.hpp` 生成密钥与最小 sshd_config，以当前用户身份在高端口拉起临时 sshd；`ssh` 与 `ssh_pty` 两条用例通过，不再依赖系统服务 | D3 |
| G3 | 伪窗口 show/hide 信号 | DONE：`WM_SIZE`/`WM_SHOWWINDOW` 在会话锁内向 vt_out 写 `ESC[1t/2t`，`visibility` 用例验证 | B7 |
| G4 | 真实 WindowsTerminal 层 | 删除：机会式用例 `Defterm.RealTerminal` 依赖用户默认终端、会真实弹窗、无法稳定 CI；按决定删除。defterm 覆盖保留 L0 直连 + L1 替身 | C6 |
| G5 | handoff 替身层 | DONE：协议拆到 `corehost/defterm/handoff_session.hpp`，`Defterm.Handoff` 用进程内 `IConsoleHandoff` 替身驱动 | C6 |
| G6 | 输出侧 flush 的 `noexcept` 路径 | DONE：对端关闭按预期失败丢弃；其它写失败记录后 `abort`（致命） | A9 |
| G7 | `defterm.cpp` 的空 `on_idle()` | DONE：调用点与方法已删除 | A6b |
| G8 | CR/LF 跨批次归属 | DONE：`swallow_lf_after_cr` 批间状态；单测 `CRLF split across batches` | D2 |
| G9 | 中断/信号压力 | DONE：`ctrl_c`、`ctrl_break`、`signal_repeat`（5 轮交替） | A10 |
| G10 | 20 次以上会话泄漏观察 | DONE：`repeat` 扩到 50 次，句柄 104→107 | A10 |
| G11 | Ctrl+C 投递 | DONE：根因是 `CTRL_C_EVENT == 0` 被当作"无事件"哨兵；改用独立标志后 `ConsoleControl(ConsoleEndTask)` 正常投递。读语义对齐 conhost：Ctrl+C 不清输入缓冲、Ctrl+Break 先 Flush，两者以 `STATUS_ALERTED` 结束挂起读；`ctrl_c_read` + 两条单测覆盖 | A |
| G12 | 交互式客户端连续会话不稳定 | DONE：serve-while-pending / WriteConsoleInput 唤醒 / VT 输入 / Process 令牌修复后，`latency` p95 9–13ms 稳定通过 | D3 |
| G13 | 部分控制台程序 0xc0000142 | 已定位为测试拆除竞态：正常会话中 `cmd /c mode con` 成功；弹窗发生在子进程初始化期间关闭伪控制台。缓解：测试树 `SetErrorMode` + Job 对象先杀客户端进程树再关伪控制台 | A/D |
| G14 | 挂起读期间不服务其它 ConDrv 请求 | DONE：独立 scratch 缓冲继续 READ_IO，其余请求处理完立即显式 COMPLETE_IO；`serve` 用例验证 | D3 |
| G15 | WriteConsoleInput 唤醒挂起读 | DONE：`on_console_input_written`；此前失败是探针缺 `--wake-probe` 分派，不是宿主问题 | D3 |

## 2. 进行中 / 未完成

| 编号 | 项 | 现状（证据） | 解除条件 | 归属 |
| --- | --- | --- | --- | --- |
| G16 | defterm 的 Ctrl+Break 投递不可达 | `corehost/defterm/defterm.cpp:129,153` 仍用 `GenerateConsoleCtrlEvent`，corehost 不在客户端控制台上（与 G11 同类），返回值也未检查 | 改为 `console_end_task` 投递并加注入式单测 | A（bugfix） |
| G17 | ~~`GenerateConsoleCtrlEvent` 忽略进程组~~ | DONE：`io_state` 按 CONNECT 记录 per-process group；`process_list_snapshot::copy_for_group` 过滤；`send_ctrl_event(event, group)`（0=全部）经 ConsoleControl 投递；`api_ctrl_event` 校验事件码并传 `ProcessGroupId`；单测 `Ctrl event filters by process group`（0/匹配组/不匹配组） | — | A |
| G18 | WM_CLOSE 关闭事件 | **不实现（决策）**。证据：① conhost 的伪窗口 WndProc 不处理 WM_CLOSE，DefWindowProc 只销毁窗口；系统宿主在 pty 关闭时会向客户端投递 `CTRL_CLOSE_EVENT`，差分测试确认其客户端收到 `EVENT=2`。② corehost 用同一 EndTask 路径（io 主线程与 vt 输入线程、hwnd=nullptr/伪窗口、flags 变体都试过）时 CSRSS 阻塞约 5s 后返回成功，但客户端 close 处理器从未被调用；Ctrl+C/Break 走同一 API 可以触发。根本原因：corehost 是外部 ConDrv server，不是 CSRSS 认可的 console host，close 处理器分发受此限制（用户态无法绕过）。模拟只能得到 5s 卡顿而没有任何可观察行为，故按用户决定不实现。WM_CLOSE 仍立即结束会话（挂起读随之返回）；应用不应依赖 ConPTY 下的 close 处理器 | 若未来改为进程内 host 或 CSRSS 提供新接口再评估 | B |
| G19 | ~~VT 输入模式缺 `WINDOW_BUFFER_SIZE_EVENT`~~ | DONE：`bridge.queue_window_buffer_size_event()` 在 resize（PtySignal）、`SetConsoleScreenBufferSize`、`SetConsoleScreenBufferInfo(Ex)`、`SetConsoleWindowInfo`、VT `\x1b[8;h;wt` 后按 VT 输入模式产生事件，并对重复尺寸去重；`resize_event` 端到端用例 + 单测 `Window size event gated and deduped` | — | D |
| G20 | ~~鼠标输入未实现~~ | DONE：parser 识别 `\x1b[<b;x;yM/m`（0x3C..0x3F 私有参数字节）；非 VT 输入模式合成 `MOUSE_EVENT`（按下/松开/移动/滚轮、修饰键，按 conhost `_UpdateSGRMouseButtonState` 的状态保留语义），VT 输入模式/RAW_READ 透传原文；单测 `SGR mouse sequence` + 端到端 `mouse` 用例（按下→滚轮→松开） | 未实现 conhost 的双击/悬停启发式 | D |
| G21 | ~~Ctrl+Esc / Alt+Esc 未按 conhost 抑制~~ | DONE：`process_input_win32_key` 在 key_down 且带 Ctrl/Alt 的 VK_ESCAPE 上直接返回（不写输入缓冲）；单测 `Ctrl+Esc and Alt+Esc consumed` | — | A |
| G22 | ~~输入状态所有权文档与实现不符~~ | DONE：`vt_input_state.hpp` 头部改为"输入线程独占 vt_in 读取；bridge 状态由会话锁串行访问"，并说明主线程会消费批间残留字节 | — | A |
| G23 | ~~测试库缺 VT 屏幕模型~~ | DONE：`tests/lib/vt_screen.hpp`（文本/换行/退格/TAB、CUP/CUU/CUD/CUF/CUB、ED/EL、SGR、主/备用缓冲区），自测 `VT.ScreenSelfTest` 7 组；`ConPTY.Programs` 的 `cmd` 与新增 `screen_erase` 用例改为断言渲染屏幕 | — | D1 |
| G24 | ~~缺与系统 conhost 的差分测试~~ | DONE（默认只跑 corehost 冒烟；比较需 COREHOST_TEST_DIFFERENTIAL=1）：`tests/test_conpty_differential.cpp` 把测试目录里的 corehost.exe 换成复制的 `%SystemRoot%\System32\conhost.exe`（libconpty 按名查找宿主），同一 API 探针在两个宿主下运行并比较。用差分发现并修复了两个真实差异：`GetConsoleTitleW` 的 `TitleLength` 应为字符数（原实现返回字节数）、`GetConsoleMode(input)` 应补 `EXTENDED_FLAGS/INSERT/QUICK_EDIT/AUTO_POSITION`。`out_mode` 已与系统宿主一致：ConPTY 下强制启用 ENABLE_VIRTUAL_TERMINAL_PROCESSING（注册表值仍读取并记录，供诊断），比较模式已把 out_mode 纳入 | 继续扩充场景（resize/换行等已知 conhost bug 领域只记录） | D |
| G25 | ~~ssh 负路径缺失~~ | DONE：未授权密钥（Permission denied + 非 0 退出）、远端 `exit 7` 传播、交互会话 ^C 中断远端命令三条用例通过；raw read 改为"有字节即返回"（与系统宿主观测行为一致） | — | D3 |
| G26 | ~~defterm handoff 失败后为无窗口 headless~~ | 已文档化：`design/0001` §4.13 记录该取舍与影响（handoff 失败时无输入/输出，conhost 会回退本地窗口；本实现按"委托失败即通知并打断客户端"设计） | 若需要本地窗口回退，另立计划 | A |
| G28 | ~~`ReadConsoleOutputCharacter` 在 ConPTY 下让客户端 ASAN abort~~ | DONE：根因是 completion 的 `Information`/`Write.Size` 把 API struct 计入数据长度（172 = 12 + 160），客户端按 `Information` 拷贝时多写 12 字节到 160 字节缓冲。修复：新增 `ucomplete_data`（Information=数据区字节数，Write.Size=struct+数据区）与 `message_output_data_capacity`（OutputSize 与 body 双约束）；`ReadConsoleOutputCharacter`/`ReadConsoleOutput` 容量裁剪、未写区域清零；同类变长返回（ReadConsole/GetConsoleInput/GetTitle/GetAlias*/GetCommandHistory/ProcessList）统一为数据区长度。输入侧改用 `read_input_payload`（>body 大负载走 READ_INPUT，不设任意上限），不再用 `InputSize` 直接越界读 body。回归：超大 OutputSize 单测 + `screen_read` 端到端（逐行 80 字符 + 8x3 矩形）+ 脚本 `expect` | 继续审计其它客户端可控长度字段（如自定义 API） | D |
| G27 | ~~零断言/未注册测试~~ | DONE：删除未注册的 `test_width_verify.cpp` 与 0 断言的 `test_conpty_sbattr_probe`（差分测试覆盖其目的）；测试目标 26 个 | — | C/D |

## 3. 已决定不做（有理由）

| 项 | 理由 | 记录 |
| --- | --- | --- |
| 真实 defterm handoff（WindowVisible=TRUE → COM → WT）用例 | 用户决定 defterm 测试保持现状；现有 L0 直连与 L1 替身覆盖协议层。环境依赖、会弹窗的真实终端用例已删除 | 本文件 §4、`05-final-status` |
| `-Embedding` 第二跳（corehost 作为委托终端 → WT）重构与替身测试 | 同上，保持现状；该路径靠代码审查 | 本文件 §4 |
| `com_server.cpp` 的 `try/catch(...)` 移除 | COM RPC 边界不允许异常逃逸，属于明确例外，保留 | `design/0001` §4.12 |
| 伪窗口渲染、滚动条、菜单、DPI 真实处理、UIA provider | 伪窗口只做消息端点；依赖方不需要渲染 | `design/0002` 第 5 节 |
| 用注册表切换默认终端来测 defterm | 测试必须与系统状态无关；改为 ConDrv 直连 | 看板"约束补充" |
| 为 `common/` 写单元测试 | 用户明确要求；正确性由使用方测试覆盖 | `agent.md` |

## 4. 环境限制（不是代码问题）

| 现象 | 影响 | 记录 |
| --- | --- | --- |
| 受限文件沙箱下 CMake `try_compile` 挂起 | 配置/构建需要完整权限 | 看板基线表 |
| 本机默认终端是 WindowsTerminal（`HKCU\Console\%%Startup\DelegationConsole={2EACA947-…}`） | 新控制台被委托为 ConPTY，`AttachConsole` 到这类控制台被拒绝，无法用"附着"方式做 conhost 对照；差分改用 `conhost.exe --headless` 宿主（G24） | G24 | ~~缺与系统 conhost 的差分测试~~ | DONE（默认只跑 corehost 冒烟；比较需 COREHOST_TEST_DIFFERENTIAL=1）：`tests/test_conpty_differential.cpp` 把测试目录里的 corehost.exe 换成复制的 `%SystemRoot%\System32\conhost.exe`（libconpty 按名查找宿主），同一 API 探针在两个宿主下运行并比较。用差分发现并修复了两个真实差异：`GetConsoleTitleW` 的 `TitleLength` 应为字符数（原实现返回字节数）、`GetConsoleMode(input)` 应补 `EXTENDED_FLAGS/INSERT/QUICK_EDIT/AUTO_POSITION`。`out_mode` 已与系统宿主一致：ConPTY 下强制启用 ENABLE_VIRTUAL_TERMINAL_PROCESSING（注册表值仍读取并记录，供诊断），比较模式已把 out_mode 纳入 | 继续扩充场景（resize/换行等已知 conhost bug 领域只记录） | D |
| 系统 conhost 自身存在已知 bug | 差分只比较稳定、有意义的行为，不做 1:1 对齐；差异记录在案 | G24 | ~~缺与系统 conhost 的差分测试~~ | DONE（默认只跑 corehost 冒烟；比较需 COREHOST_TEST_DIFFERENTIAL=1）：`tests/test_conpty_differential.cpp` 把测试目录里的 corehost.exe 换成复制的 `%SystemRoot%\System32\conhost.exe`（libconpty 按名查找宿主），同一 API 探针在两个宿主下运行并比较。用差分发现并修复了两个真实差异：`GetConsoleTitleW` 的 `TitleLength` 应为字符数（原实现返回字节数）、`GetConsoleMode(input)` 应补 `EXTENDED_FLAGS/INSERT/QUICK_EDIT/AUTO_POSITION`。`out_mode` 已与系统宿主一致：ConPTY 下强制启用 ENABLE_VIRTUAL_TERMINAL_PROCESSING（注册表值仍读取并记录，供诊断），比较模式已把 out_mode 纳入 | 继续扩充场景（resize/换行等已知 conhost bug 领域只记录） | D |
