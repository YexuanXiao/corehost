# 子任务看板

状态图例：`TODO` 未开始 / `DOING` 进行中（同一时刻只允许一个）/ `BLOCKED` 阻塞 /
`DONE` 完成。完成的子任务必须有对应提交，提交列填写提交主题，哈希用 `git log` 查。

## 阶段 0：计划与文档

| 编号 | 子任务 | 状态 | 提交 | 备注 |
| --- | --- | --- | --- | --- |
| 0 | 建立 `doc/` 计划、任务看板、设计取舍骨架 | DONE | 见首个文档提交 | 与 `agent.md` 一起提交 |

## 阶段 A：非轮询 I/O 模型（任务 1）

| 编号 | 子任务 | 状态 | 提交 | 备注 |
| --- | --- | --- | --- | --- |
| A1 | 基线调查：枚举全部轮询/唤醒点、状态归属；锁定回归基线 | DONE | A1 基线调查 | 9 处轮询点；15/15 测试通过；方法见 `doc/tasks/01-a1-baseline.md` |
| A2 | `common/win32`：SRWLock 共享/独占锁 + 条件变量封装（common 不写测试） | DONE | A2 SRWLock 封装 | `common/win32/srwlock.hpp`；编译检查通过 |
| A3 | `common/win32`：命名线程、启动/join、可取消阻塞读 + 单测 | DONE | A3 线程与可取消读工具 | `thread.hpp`、`cancellable_read.hpp`；common 不写测试，由使用方覆盖 |
| A4 | 终端输入线程：读、解码、解析、键盘事件、cooked 编辑，锁内同步处理 | DONE | A4/A5 输入与信号线程 | 状态在 `vt_input_state`；线程阻塞读、锁内同步处理；ctest 15/15 |
| A5 | 信号管道线程：协议解析、锁内应用 resize/clear/parent | DONE | A4/A5 输入与信号线程 | `pty_signal_thread`；断管按 EOF 处理 |
| A6 | 主循环：INFINITE 等待 {InputAvailableEvent, state_changed}；删除 16ms/0ms 时间片 | DONE | A4/A5 输入与信号线程 | 死代码清理见 A6b |
| A6b | 清理死代码：`on_idle`/`wait_for_pending_vt_input`/`pipe_bridge_io` peek 路径 | DOING | A6b 清理主循环侧轮询入口 | 已删 message_router 轮询入口、signal::poll、pipe_bridge::on_idle/wait_for_pending_vt_input；read_available 在 threaded 模式不再碰管道；剩余：drain/prepare 辅助、defterm 空 on_idle；`_on_line_terminator` 的 CR/LF lookahead 需在 threaded 模式下确认为输入线程独占（待 A10 用例覆盖） |
| A7 | defterm 初始 CONNECT 循环与 handoff 等待循环收敛到同一模型 | DONE | A7 defterm 循环去轮询 | 初始循环 INFINITE 等 input_event；handoff 用信号线程 + 等 {终端进程, 管道关闭}；待阶段 C 真机验证 |
| A8 | 退出协议：停止标志、事件驱动取消、无限 join | DONE | A4/A5 输入与信号线程 | 终止性由构造保证；压力验证归 A10 |
| A9 | 错误分类审计：所有系统调用检查、致命路径统一 | DOING | A9 审计新建路径 | 见 `doc/tasks/03-a9-error-audit.md`；遗留：输出 flush、死代码、defterm 空 on_idle |
| A10 | 性能与唤醒验证：空转计数、CPU、时延、压力、泄漏 | DOING | A10 空闲 CPU 用例 | `ConPTY.IdleCpu`：3 秒空闲实测 **0 ms** CPU（阈值 100ms）；时延/压力/泄漏待测试库落地后补 |
| A11 | 文档更新与提交 | TODO | | |

## 阶段 B：GetConsoleWindow 伪窗口（任务 2）

| 编号 | 子任务 | 状态 | 提交 | 备注 |
| --- | --- | --- | --- | --- |
| B1 | 原版 window/windowproc 分析：消息面、类、生命周期 | DONE | B1 伪窗口需求分析 | 分析对象是 `InteractivityFactory`（ConPTY 伪窗口），结论写入 `design/0002` |
| B2 | 伪窗口设计：窗口类型、消息矩阵、状态机 | DONE | B2/B3 伪窗口与窗口线程 | `design/0002` 3/4 节 |
| B3 | 窗口线程 + 消息循环 + 命名 + 停止协议 | DONE | B2/B3 伪窗口与窗口线程 | `pseudo_window.hpp`；GetMessage 阻塞，WM_QUIT + 无限 join |
| B4 | 消息处理骨架：查询类消息、默认处理 | DONE | B2/B3 伪窗口与窗口线程 | WM_CREATE/CLOSE/DESTROY/ACTIVATE/GETOBJECT + DefWindowProc |
| B5 | `GetConsoleWindow` 返回句柄 + 生命周期绑定 | DONE | B2/B3 伪窗口与窗口线程 | API 返回 `instance().hwnd()`；会话内创建/退出销毁（日志实测 hwnd） |
| B6 | 关闭语义：`WM_CLOSE` → 会话关闭事件 | DONE | B6 WM_CLOSE 结束会话 | CancelIoEx(server) 打断 READ_IO；用例实测 0.11s 内进程退出 |
| B7 | 状态联动：标题、尺寸、show/hide 最小支持 | TODO | | |
| B8 | 泄漏/并发/压力验证 + 文档 | TODO | | |

## 阶段 C：defterm 测试（任务 3）

| 编号 | 子任务 | 状态 | 提交 | 备注 |
| --- | --- | --- | --- | --- |
| C1 | 测试库骨架：runner/断言/隔离/超时/环境探测 + 自测 | DOING | C1 测试库 runner | runner/断言/过滤/子进程隔离/超时均已完成并有自测；环境探测（C2）待做 |
| C2 | 进程/会话驱动；诊断产物；ConDrv 客户端 | DOING | C2 宿主层：环境探测与子进程运行器 | `tests/lib/platform.hpp`：PATH/System32/WT/sshd/交互式探测 + `run_process`（Job 对象管进程树、600ms 超时实测终止）；会话驱动完成并通过；**defterm 测试不碰注册表**：测试进程自建 ConDrv server/reference，启动 `corehost.exe <handle>`，再打开 `\\Connect` 完成客户端 CONNECT 并绑定 PEB ConsoleHandle（见 `tests/lib/condrv.hpp`）。已修：旧循环在首次 READ_IO 前等事件，CONNECT 先排队时永久阻塞；现能收到 CONNECT 并启动 headless 会话。遗留：测试进程 GetConsoleMode 失败，待查连接对象匹配
| C3 | defterm 启动路径：delegation 切换、启动客户端、等待就绪 | TODO | | |
| C4 | 控制台测试客户端（脚本化 API/VT/窗口消息） | TODO | | |
| C5 | 用例矩阵：控制台/窗口/输入/子进程/关闭 | TODO | | |
| C6 | 真实 WT 层、稳定性、文档 | TODO | | |

## 阶段 D：conpty 测试（任务 4）

| 编号 | 子任务 | 状态 | 提交 | 备注 |
| --- | --- | --- | --- | --- |
| D1 | 测试库：会话驱动、输出泵、VT 屏幕模型、诊断 | TODO | | 复用 C1 |
| D2 | 控制台测试程序（脚本化 API/VT/子进程） | TODO | | |
| D3 | 基础用例：输出、光标、输入、resize、异常 | TODO | | |
| D4 | 真实程序用例：cmd、powershell、vim、edit | TODO | | |
| D5 | ssh 远程组合：专用 sshd、连接、远程交互、清理 | TODO | | |
| D6 | 子进程继承用例、稳定性、文档 | TODO | | |

## 当前迭代

- 阶段：A
- 正在做：A6b 收尾（pipe_bridge 内部 drain/peek 辅助）
- 下一步：A6b → A9 错误审计 → A10 性能/唤醒验证 → 阶段 B

## 基线记录

| 项 | 值 | 采集时间 | 方法 |
| --- | --- | --- | --- |
| 构建 | 通过（`ninja: no work to do`） | 2026-11-10 | `cmake --build build` |
| 配置 | Debug / Ninja / MSVC 14.52 / `COREHOST_LOG_LEVEL=2` | 2026-11-10 | `build/CMakeCache.txt` |
| 测试 | 16/16 通过（新增 ConPTY.IdleCpu） | 2026-11-11 | `ctest --test-dir build --output-on-failure` |
| 空闲 CPU | 改造后 3 秒空闲 0 ms；改造前未采样（旧实现按 16ms 唤醒，理论值远高于此） | 2026-11-11 | `ConPTY.IdleCpu` |
| 输入时延 | 待采集 | | A10 统一方法 |
| 环境 | WT 已安装；`ssh.exe`/`sshd.exe` 存在；交互式桌面 | 2026-11-10 | 命令探测 |
| 环境注意 | 受限文件沙箱下 CMake 的 `try_compile` 子进程会挂起（编译器探测卡死）；配置/构建需要完整权限 | 2026-11-10 | 最小工程复现 |

## 约束补充（用户澄清，已写入 agent.md / design）

| 日期 | 约束 | 落实位置 |
| --- | --- | --- |
| 2026-11-10 | `common/` 的内容不写测试 | 删除 `test_srwlock`；`common` 正确性由使用方测试覆盖 |
| 2026-11-10 | 不用"每输入一个队列"，控制台是同步处理，多线程只为消除轮询 | `design/0001` 方案 D：会话锁 + 数据到达即处理 |
| 2026-11-10 | **禁止任何等待上限**：不许用超时放弃等待 | `design/0001` 4.6；`plan/01` 第 2/6 节；`cancellable_read::wait_for_exit` 改为无限等待 |
| 2026-11-11 | defterm 测试禁止改注册表/系统状态：构造 ConDrv 句柄 + 启动控制台程序 + 交给 defterm | `tests/lib/condrv.hpp`；plan/03 与 design/0003 已修订 |
| 2026-11-11 | **禁止单例与全局可变状态**，依赖必须参数传递 | 删除 `pseudo_window::instance()`；窗口实例由会话持有，hwnd 经 `console_state::window_handle` 传给 API handler |

## 阻碍

| 编号 | 子任务 | 条件 | 已尝试 |
| --- | --- | --- | --- |
| — | — | 无 | — |

## BUG 记录

| 编号 | 现象 | 分支 | 状态 | 关联子任务 |
| --- | --- | --- | --- | --- |
| B1 | ConPTY 客户端不连接（无 CONNECT，输出落父控制台） | bugfix/conpty-client-attach | FIXED（已合并） | C2 |

## 提交记录

提交历史见 `git log --oneline`；每个子任务的提交主题记录在上表的"提交"列。
