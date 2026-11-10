# 未完成项与阻塞原因

状态：随每个子任务更新。任何"没做完"的东西都必须在这里，而不是散落在提交信息里。

## 1. 明确未完成

| 编号 | 项 | 现状 | 解除条件 | 归属 |
| --- | --- | --- | --- | --- |
| G1 | ~~defterm 直连客户端的 `GetConsoleMode` 失败~~ | RESOLVED：传本进程 STD 句柄会被判未知对象（测试环境没有客户端句柄表）；改传 `nullptr`（object=0）后成功。真实客户端有句柄表，不受影响 | — | C5 |
| G2 | ~~ssh 远程组合~~ | DONE：测试库 `tests/lib/ssh_server.hpp` 在测试目录旁生成密钥与最小 sshd_config，以当前用户身份在高端口拉起**临时 sshd**（非管理员），`ssh` 非交互命令与 `ssh_pty` 交互会话两条用例均通过；不再依赖系统 sshd 服务 | — | D3 |
| G3 | ~~伪窗口 show/hide 信号~~ | DONE：`WM_SIZE(SIZE_MINIMIZED/RESTORED)`、`WM_SHOWWINDOW` 在会话锁内向 vt_out 写 `ESC[2t/1t`（原版 `VtIo::Writer::WriteWindowVisibility` 同款）；`ConPTY.Programs` 的 visibility 用例验证 | — | B7 |
| G4 | 真实 WindowsTerminal 层（L2） | 未开始：defterm 测试目前覆盖 L0（ConDrv 直连），没有真正拉起 WT 并做端到端交互 | 需要交互式桌面 + 已安装 WT；用例要能探测并 SKIP | C6 |
| G5 | Handoff 替身层（L1） | 未开始：没有测试实现的 COM handoff 接收方 | 需要一个最小 `IConsoleHandoff` 本地服务器；可先在 defterm 分支上做 | C6 |
| G6 | ~~输出侧 `vt_output_buffer::flush` 的 `noexcept` 路径~~ | DONE：对端关闭仍按预期失败丢弃；其它写失败改为记录后 `abort`（致命） | — | A9 |
| G7 | ~~`defterm.cpp` 的 `connect_handler::on_idle()` 空实现~~ | DONE：调用点与方法已删除 | — | A9/A6b |
| G8 | CR/LF 跨批次 lookahead 在线程模型下的归属 | `_on_line_terminator` 会在批次末尾 `PeekNamedPipe` 找 LF；threaded 模式下由输入线程执行，但缺专门用例 | 增加"CR 与 LF 分两次到达"的用例，断言不产生多余行 | A10 |
| G9 | 中断/信号压力测试 | 未做：Ctrl+C/Ctrl+Break 与信号线程交互只在少数用例间接覆盖 | 在 defterm/conpty 两侧补"信号到达时无丢失、无重复"的压力用例 | A10 |
| G10 | ~~20 次以上会话的泄漏观察~~ | DONE：repeat 扩到 50 次（句柄 104→107） | — | A10 |
| G15 | ~~WriteConsoleInput 唤醒挂起读~~ | DONE：移植的 `on_console_input_written` 端到端成立——探针 `--wake-probe` 在行读挂起时写一个 Enter 记录，`ReadConsole` 立即返回（`READ-RETURNED`）。此前失败原因是探针缺少 `--wake-probe` 分派（输出 "unknown mode"），不是宿主问题 | — | D3 |
| G14 | ~~挂起读期间不服务其它 ConDrv 请求~~ | DONE：移植 fork `5104668`——挂起读时用独立 scratch 缓冲继续 READ_IO，其余请求处理完立即显式 COMPLETE_IO；`serve` 用例验证（ReadConsole 挂起时另一线程的 GetConsoleScreenBufferInfo 立即返回 QUERY-OK） | — | D3 |
| G12 | ~~交互式客户端连续会话不稳定~~ | DONE：移植 serve-while-pending / WriteConsoleInput 唤醒 / VT 输入 / Process 令牌等修复后，`latency` 用例（20 次按键）3 次连跑全部通过，p95 12–13ms | — | 已确认：`cmd`/`cmd /k` 能出提示符并存活；输入到达宿主并完成一次 ReadConsole（`complete_pending cooked_len=7`）；但测试侧在写入后读到 0 字节（echo 与命令回显都没有）。回显链路（`on_input_bytes → process_new_vt_input → _edit_insert → edit_insert_char → echo_byte → vt_append_char/apply_echo_byte`，`on_input_bytes` 末尾有 `vt_flush`）在代码上完整 | 下一步：在 `vt_output_buffer::flush` 加临时计数日志（写入字节数、目标句柄、失败原因），确认 `vt_flush` 是否真的把回显写入 vt_out；再对比 `handle_console_read` 前后 `_terminal` 状态 | D3 |
| G13 | ~~部分控制台程序 0xc0000142~~ | 已定位为**测试拆除竞态**：正常会话里 `cmd /c mode con` 成功（exit 0、输出正常）；弹窗发生在"子进程初始化期间关闭伪控制台"。缓解：测试进程树 `SetErrorMode` 抑制弹窗 + 会话驱动用 Job 对象先杀整棵客户端进程树再关伪控制台 | 若日后在正常会话中复现再升级为产品 BUG | A/D 交叉 |
| G11 | ~~Ctrl+C 投递~~ | DONE：根因是 `CTRL_C_EVENT == 0` 被当作"没有事件"的哨兵，`if (control_event != 0)` 永远不成立。改用独立的 `has_control_event` 标志后，`ConsoleControl(ConsoleEndTask)` 正常投递（status=0），`ctrl_c` 用例（专用探针）通过并改为硬断言 | — | A 阶段遗留 |

## 1.1 收尾状态

第 60 轮时：G1/G3/G6/G7/G10/G12/G13/G14/G15 已关闭；G2（sshd）、G4/G5（真实 WT 与
handoff 替身层）、G11（Ctrl+C）仍未完成，解除条件见对应行。构建与 `ctest` 23/23 通过。

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
| sshd 服务未运行 | D5 用例 SKIP | G2 | ~~ssh 远程组合~~ | DONE：测试库 `tests/lib/ssh_server.hpp` 在测试目录旁生成密钥与最小 sshd_config，以当前用户身份在高端口拉起**临时 sshd**（非管理员），`ssh` 非交互命令与 `ssh_pty` 交互会话两条用例均通过；不再依赖系统 sshd 服务 | — | D3 |
| 本机未验证 WindowsTerminal 交互 | C6 未开始 | G4 |
