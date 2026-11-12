# 设计取舍 0001：I/O 模型的线程结构与状态归属

状态：已决定（实现待验证项见文末）

## 1. 背景

见 `plan/01-io-model.md`。核心矛盾：多个输入源（ConDrv 消息、终端输入管道、
信号管道、窗口消息）需要同时被服务，而控制台状态（屏幕、光标、输入队列、
ConDrv 挂起请求、VT 输出）必须在同一时刻只被一个线程推进。当前用一个线程加
短超时轮询把两者凑在一起，导致空转与延迟下限。

## 2. 约束（本设计的硬条件）

来自任务要求与用户澄清：

1. 不轮询：没有输入时任何线程都不唤醒。
2. 每个输入源一个线程，线程拥有自己的输入缓冲与解析状态。
3. **控制台只需要同步处理，不需要异步化。多线程的唯一目的是消除轮询。**
   因此不允许"输入线程把事件排进队列、主线程再异步消费"——那会把同步处理
   变成异步流水线，队列会在主线程繁忙时累积，且引入"事件产生时刻"与"被处理
   时刻"的语义裂缝。
4. 不捕获异常；致命错误在主线程抛出，在工作线程 `std::abort()`。
5. 锁与条件变量只用 SRWLock 封装；主线程必须 join 所有工作线程。

## 3. 候选方案

### 方案 A：每源一线程 + 事件队列，主线程统一消费（否决）

最初的设计。每个输入线程解析后把事件放进有界队列并置位"输入就绪"，主线程
排空队列后处理。

否决理由（用户指出）：控制台的处理本来就是同步的，事件一产生就应当被处理；
排队等于把同步逻辑拆成两段，队列长度是"主线程延迟"的度量，输入越快队列越长，
残留事件还会让 EOF/关闭时序变得难以推理。虽然可以用哨兵事件、水位线和
"排空到空"来掩盖，但那是给不需要异步的系统强加异步。

### 方案 B：全异步 I/O（overlapped + IOCP）（否决）

终端输入与信号管道由外部创建（匿名管道，`libconpty`/defterm 入口），无法要求
调用方以 `FILE_FLAG_OVERLAPPED` 打开；`DeviceIoControl` 的 READ_IO 也是同步
完成语义。强行改造需要复制句柄或自建转发管道，复杂度和风险远超收益。

### 方案 C：保留轮询，增大时间片或自适应（否决）

问题本质是"没有输入也定期醒来"，调整间隔只是把空转换成延迟。

### 方案 D：每源一线程 + 会话锁，数据到达即在锁内同步处理（选定）

- 每个输入线程阻塞在自己的句柄/消息上；读到数据后**获取会话锁，立刻完成
  处理**（沿用现有 `on_idle`/pending 完成逻辑），然后释放锁继续阻塞读。
- 主线程只在处理 ConDrv 消息时获取同一把会话锁。
- 因为处理是同步的，任何时刻至多有一个线程在推进控制台状态；控制台状态仍是
  事实上的单写者，只是"单写者"由锁保证而不是由"只有一个线程"保证。
- 管道本身就是缓冲：工作线程没处理完不会读下一块，背压天然成立，不需要任何
  应用层队列。

选定理由：满足"同步处理"；满足"线程拥有自己的缓冲与状态"（解析器、解码器、
行编辑缓冲仍私有）；没有队列、没有异步时序；锁的临界区只覆盖一次有界的处理，
不跨阻塞等待。

## 4. 选定方案的关键决定

### 4.1 会话锁

- 一把 `win32::shared_mutex`，全程以**独占**方式使用（`std::unique_lock`）。
  共享模式不用：控制台状态不是读多写少，且读也需要与处理串行。
- 锁覆盖：ConDrv 消息分派、输入解析与行编辑、pending 请求完成、`input_buffer`
  更新、VT 输出缓冲追加与 flush、退出判定。
- **禁止持锁等待**：锁内不允许 `WaitForSingleObject`、`ReadFile` 阻塞读、
  条件变量等待、join。所有阻塞操作都在锁外。
- 锁内允许的 I/O：`DeviceIoControl`（READ_IO/COMPLETE_IO，均为短同步调用）、
  `WriteFile` 到 vt_out（与现状相同；若对端不读可能阻塞，见第 6 节）。
- 只有一个锁，因此没有锁序问题；不允许在锁内回调用户代码或再次获取同一把锁
  （SRWLock 不可递归）。

### 4.2 唤醒协议（无队列）

- 每个输入源有一个"状态变化"事件（自动重置），工作线程在**释放锁之后**置位。
- 主线程等待集 = {ConDrv InputAvailableEvent，各输入源的"状态变化"事件}，
  无限等待，醒来后取锁、排空 READ_IO（读到"无消息"为止）、处理、解锁。
- 事件只是"提示状态可能变了"，不携带数据，因此不存在事件累积：无论置位多少
  次，主线程醒来后看到的都是当前真实状态（pending 是否完成、是否 EOF、是否有
  ConDrv 消息）。这正是"同步处理"需要的语义。
- 丢失唤醒：工作线程先改状态、后置位事件；主线程取锁后总是重新读取真实状态，
  不依赖事件内容。事件置位发生在主线程等待之前时，等待立即返回。

### 4.3 状态归属

| 状态 | 归属 | 访问方式 |
| --- | --- | --- |
| vt_in 读缓冲、UTF-8 解码器、VT 输入解析器、原文缓冲 | 终端输入线程私有 | 无锁；只在该线程内访问 |
| 键盘修饰键、Win32 Input 解码状态 | 终端输入线程私有 | 无锁 |
| cooked 行缓冲与行内编辑光标 | 终端输入线程私有 | 无锁 |
| 信号消息解析中间态 | 信号线程私有 | 无锁 |
| 窗口过程状态（阶段 B） | 窗口线程私有 | 无锁 |
| `console_state`、主/备用 `screen_buffer`、`input_buffer` | 共享 | 会话锁内访问 |
| ConDrv pending 请求与 `io_msg` 双缓冲 | 共享 | 会话锁内访问 |
| VT 输出缓冲、终端光标/行编辑边界追踪 | 共享 | 会话锁内访问 |
| 退出标志、各源 EOF 标志 | 共享 | 会话锁内访问；事件只作提示 |

任务要求"线程拥有自己的输入缓冲区和状态"：上表前五行即从现有 `pipe_bridge`
迁出的部分；它们不再被主线程访问，因此不需要锁，也不需要跨线程传递事件。

### 4.4 回显与输出顺序

回显不经过任何队列：输入线程在会话锁内直接把回显写入 VT 输出缓冲（必要时
flush），与应用输出在同一把锁下串行。因此"按键回显"与"程序输出"的相对顺序
就是两者获取锁的顺序，与单线程时代的语义一致，不会出现"回显延迟到程序输出
之后"的现象。

### 4.5 输入模式的读取

因为输入线程在锁内处理，它可以直接读 `console_state` 里的输入模式（Ctrl+Z/
Ctrl+C 语义），不需要快照。这是方案 D 相对方案 A 的一个额外简化：不需要
"模式快照 + 版本号 + 陈旧窗口"这一整套机制。

### 4.6 Ctrl+C / Ctrl+Break 的读语义（与 conhost 对齐）

行为依据是conhost的 Ctrl+C / Ctrl+Break 观测语义：

- **Ctrl+C**：仅 processed input、无 Alt、key down、`VK == 'C'`。投递
  `CTRL_C_EVENT`，并以 `STATUS_ALERTED`（0x101）结束等待中的
  ReadConsole/ReadFile/GetConsoleInput；**不 Flush** 输入缓冲；按键不写入输入缓冲。
- **Ctrl+Break**：不看 processed input。先 `Flush` 输入缓冲，再投递
  `CTRL_BREAK_EVENT`，同样以 `STATUS_ALERTED` 结束读。
- 事件投递走 CSRSS `ConsoleControl(ConsoleEndTask)`（corehost 不在控制台上，
  `GenerateConsoleCtrlEvent` 到不了客户端）。
- `ConsoleControl(EndTask)` 不在会话锁内执行：处理 Ctrl+C/Break 时只把事件与目标
  pid 入队，输入线程/message_router 在释放会话锁后调用 `deliver_ctrl_event()` 投递；
  与conhost宿主"先记录事件、解锁后逐进程投递"的模型一致，避免客户端处理器
  阻塞期间其它客户端被会话锁卡住。
- 客户端观察到的结果由 Windows 控制台客户端库映射：0 字节读取 +
  `ERROR_OPERATION_ABORTED`。

### 4.7 阻塞读的取消

匿名管道不支持 overlapped，也没有"可等待的可读事件"。选定
`CancelSynchronousIo` 方案：

1. 线程进入 `ReadFile` 前置位 `in_read`，返回后清除（原子标记，不在锁内）。
2. 主线程请求停止，然后等待 {读线程句柄, "in_read" 事件}：事件一置位就说明读线程
   刚进入 `ReadFile`，此时调用 `CancelSynchronousIo`；线程句柄一置位就说明读线程
   已退出。**纯事件驱动，没有重试间隔、没有超时、没有轮询。**
3. 被取消的 `ReadFile` 返回 `ERROR_OPERATION_ABORTED`，线程检查停止标志后
   正常退出（不当作 EOF，不产生输入事件）。
4. 循环而不是只取消一次，是为了覆盖"线程已通过停止检查、尚未进入 ReadFile"
   的窗口；只要主线程持续观察到 `in_read` 并再次取消，就不会永久阻塞。
5. **不允许任何等待上限（超时兜底）。** join 必须是无限等待：超时的含义是
   "放弃一个仍在运行的线程"，那等于把设计缺陷伪装成可恢复情况——线程还在跑，
   它的状态、句柄、栈可能在 join 返回后被销毁，崩溃/数据损坏会推迟到更难定位
   的地方发生。正确的兜底是"终止性由构造保证，破坏终止性的情况按致命错误处理"：
   - 每个线程在进入阻塞前检查停止标志；
   - 每一次阻塞读都能被取消，取消失败（`CancelSynchronousIo` 返回意外错误）
     立即抛出/终止，而不是继续等；
   - 协议损坏、不可取消的阻塞调用等"本不应发生"的情况是致命错误，记录线程名
     与状态后 abort；
   - 任何"等一会儿再放弃"的写法都禁止。

   这也意味着：产品路径上不存在任何超时等待（轮询时间片已按任务要求删除，
   剩余等待全部是无限等待 + 事件/取消唤醒）。测试框架里的截止时间是"判定
   失败并结束用例"的手段，属于测试基础设施，不是产品逻辑，不得回流到产品代码。

实现载体：`common/win32/cancellable_read.hpp` 负责 `in_read` 事件、`request_stop`、
`cancel_pending` 与事件驱动的无限 `wait_for_exit`；线程句柄由
`common/win32/thread.hpp` 的 `basic_thread` 提供（`join()` 为无限等待，不保存线程 id）。

被否决的替代：关闭读端句柄（行为未定义，I/O 可能继续挂起）；改 overlapped
（调用方给的句柄不可控）；`TerminateThread`（破坏进程状态，违背"正确处理线程
退出"）。

### 4.8 致命错误不能跨线程抛出

异常无法跨线程传播：主线程致命错误抛 `win32::error`/`std::bad_alloc`；工作
线程致命错误记录诊断后 `std::abort()`。全仓库不新增 `catch`。

### 4.9 线程命名与创建

每个工作线程在创建时传入可读名称（终端输入 / 信号管道 / 控制台窗口），由
`common/win32/thread.hpp` 的 `basic_thread` 统一处理：`CreateThread` 以
`CREATE_SUSPENDED` 创建，创建者调用 `SetThreadDescription` 后 `ResumeThread`。
不提供创建后改名接口，也不把线程 id 保存在线程对象里。

### 4.10 禁止探测式等待

"先 `WaitForSingleObject(handle, 0)` 看一眼，再决定要不要做事"与轮询没有本质区别：
它仍然是"没输入时也在主动检查"。这类代码在旧实现里用来决定"提交 completion 前
是否先 flush 输出"，在新模型里一律删除：

- 输出 flush 的时机由"这一轮已经处理完消息、即将进入无限等待"决定，而不是由
  一次 0ms 探测决定；
- 需要知道"ConDrv 是否还有消息"时，直接 `READ_IO`，返回 `no_message` 就是答案；
- 输入线程/AIO 事件由 `message_router` 的入口处理；需要知道挂起请求是否完成时，
  直接读 `has_pending()`，不等待/轮询状态。

判断标准：**等待只能出现在"我要阻塞直到某件事发生"的位置，且超时参数必须是
INFINITE；"探查状态"必须通过读取真实状态完成，不能通过等待实现。**

## 5. 影响

- `pipe_bridge` 的输入解析、行编辑、信号处理外移；它保留"主线程侧的 API 状态
  与输入事件队列（`input_buffer`）"。
- 主循环从"时间片 + 窥探"变成"等待集 + 取锁处理 + 排空 READ_IO"。
- defterm 的初始 CONNECT 循环与 handoff 等待循环同构改造，避免残留第二套轮询。
- 需要新的并发测试（取消、锁、EOF 时序）与会话回归测试；`common/` 本身按用户
  要求不写测试，其正确性由使用方（会话测试）覆盖。

## 6. 待验证 / 开放问题

1. `WriteFile` 到 vt_out 在锁内可能阻塞（对端不读）。现状是主线程同样会阻塞，
   因此不是新风险；但锁会把阻塞放大到所有输入线程。需要在 A6 评估：输出缓冲
   是否要改为"锁内追加、锁外写"（仍同步，但不在锁内做可能阻塞的写）。
2. 取消协议的可靠性需要在"检查后、读前"注入延迟的压力测试验证。
3. 会话锁的公平性：工作线程连续输入时主线程可能等待锁；SRWLock 独占排队是
   FIFO 自旋，实际影响需测量（目标：输入洪流下 READ_IO 不被饿死）。
4. 退出时"停输入线程"与"主循环结束"的先后：停线程需要获取会话锁，而主线程
   可能正持锁处理；需保证停止请求不会与处理互锁（停止请求只在主线程退出循环
   后发起，此时主线程不持锁）。
5. `ReadFile` 返回 0 字节（对端关闭）与 `ERROR_OPERATION_ABORTED`（取消）必须
   严格区分：前者是 EOF，后者是"停止"，不能混为一谈。

### 4.11 实测发现：ConDrv READ_IO 是阻塞的

B6 的伪窗口用例暴露出一个此前被注释误导的事实：`DeviceIoControl(IOCTL_READ_IO)`
在没有消息时会**阻塞**，而不是立即返回 `ERROR_IO_PENDING`。证据：窗口线程在
12:24:04 收到 `WM_CLOSE` 并置位事件，主循环直到 12:24:09 测试拆除会话、ConDrv
断开时才打印 "read_io false, exiting"——5 秒里主线程一直停在 READ_IO 内部。

推论与调整：

1. **主循环的 `wait_any` 只是安全网**：正常情况下主线程阻塞在 READ_IO 上，
   ConDrv 有新消息或断开时自然返回，本身就不轮询。
2. 输入线程完成 pending 请求时不需要唤醒主线程（它已经用 COMPLETE_IO 直接
   完成），唤醒只用于"会话状态需要主线程决策"的场景。
3. **`WM_CLOSE` 必须能打断 READ_IO**：窗口线程收到 `WM_CLOSE` 后，除了置位
   `close_requested`，还要取消主线程正在进行的设备 I/O。实测：
   - `CancelSynchronousIo(主线程)` **不能**取消 `DeviceIoControl(IOCTL_READ_IO)`；
   - `CancelIoEx(server, nullptr)` 可以，READ_IO 返回 `ERROR_OPERATION_ABORTED`。
   `read_io_try` 把 `ERROR_OPERATION_ABORTED` 当作"本轮无消息"，主循环随后在
   事件等待里看到 `close_requested` 并退出。
4. 因此 B6 的实现顺序是：暴露主线程句柄给窗口线程 → 关闭时取消 READ_IO →
   主循环检查 `close_requested` → 走统一关闭路径（给客户端发关闭事件、退出）。

这条发现同时说明：`ConPTY.IdleCpu` 的 0ms 不只是"没轮询"，还因为主线程根本
不在用户态空转，而是在内核里等消息。
## 4.12 COM RPC 边界捕获异常是唯一例外

`corehost/comserver/com_server.cpp` 的 `EstablishHandoff` 用 `try/catch(...)` 包裹实现体。
这不是"用 catch 掩盖错误"：COM 方法跨 RPC 边界，异常逃逸是未定义行为；捕获后返回
`E_FAIL` 并记录日志，是协议层的错误返回路径。产品其余部分仍然禁止 catch（见 A9 审计）。

## 4.13 defterm handoff 失败后的取舍：无窗口 headless

`corehost/defterm/defterm.cpp` 在 COM handoff 失败（或 elevated）时，通知并立刻
`CTRL_BREAK` 客户端，然后以 keepalive 空输入管道 + 空 `vt_out` 启动 conpty 会话：
客户端不会获得键盘输入，输出被丢弃。conhost 在这种情况会回退到本地窗口；本实现
选择"委托失败即尽快结束客户端"，避免维护第二套窗口/渲染路径。影响与解除条件记录
在 `tasks/04-open-gaps.md` G26。

## 4.14 WINDOW_BUFFER_SIZE_EVENT 的触发条件

与系统宿主行为一致：只有 `ENABLE_VIRTUAL_TERMINAL_INPUT` 且缓冲区
尺寸变化时产生；事件写入 input_buffer 前按"最近一次上报尺寸"去重（
重复尺寸不重复上报）。触发点：PtySignal resize、SetConsoleScreenBufferSize、
SetConsoleScreenBufferInfo(Ex)、SetConsoleWindowInfo、VT `\x1b[8;h;wt`。

## 4.15 SGR 鼠标的转换边界

`\x1b[<b;x;yM/m` 在**非** VT 输入模式下转换成 `MOUSE_EVENT`（按下/松开/移动/滚轮、
修饰键，按 conhost 的状态保留语义）；VT 输入模式或 RAW_READ 下原样透传，由应用
自行解析。conhost 的双击/悬停启发式没有复刻（记录在 G20 备注）。
## 4.16 与系统 conhost 的差分测试

`tests/test_conpty_differential.cpp` 默认只跑 corehost 侧冒烟，不与系统宿主比较；
设置 `COREHOST_TEST_DIFFERENTIAL=1` 时才把 `%SystemRoot%\System32\conhost.exe`
复制成独立目录里的 `corehost.exe`，让同一组 Console API 分别在两个宿主下运行。
raw/record Ctrl+C 的记录需要再设置 `COREHOST_TEST_DIFFERENTIAL_CTRL=1`。

比较稳定字段：`in_mode`、`out_mode`、`SetConsoleTextAttribute` 后的 `wAttributes`、
光标大小/可见性、标题与标题长度。`out_mode` 在 ConPTY 下两边都强制启用
`ENABLE_VIRTUAL_TERMINAL_PROCESSING`，因此纳入比较。

差分发现并修复的真实差异：
- `GetConsoleTitleW` 的 `TitleLength` 是字符数，不是字节数；
- `GetConsoleMode(input)` 回读时补 `EXTENDED_FLAGS` 和
  `INSERT/QUICK_EDIT/AUTO_POSITION`；`SetConsoleMode` 只有带 `EXTENDED_FLAGS`
  才允许整体替换私有位。

## 4.17 Completion 的 Information 语义与输入负载边界

ConDrv 客户端把 `CD_IO_COMPLETE.IoStatus.Information` 当作**数据区字节数**；corehost 的单 region 完成同时携带 API struct + 数据。
因此约定：

- `Write.Size = sizeof(API struct) + 数据区字节数`（客户端一次拷入 API 消息体）；
- `Information = 数据区字节数`（不含 struct）。

早期实现把 struct 也算进 `Information`，`ReadConsoleOutputCharacter` 在 80 字符
精确缓冲下会让客户端多写 `sizeof(struct)` 字节，触发 ASAN 堆越界（G28）。
`api_handlers.hpp` 的 `ucomplete_data` 封装了该约定；`message_output_data_capacity`
同时按客户端 `OutputSize` 与本地 `io_msg::body` 余量收缩数据区，未写区域清零，
避免把上一条消息的 body 回传给客户端。

输入方向同理：`InputSize` 由 ConDrv 消息提供，可能大于本地 body。所有变长输入
（ReadConsole initial data、WriteConsoleInput/Output/OutputString、SetTitle）
统一经 `pipe_bridge::read_input_payload` 取数据：body 放得下就直接切片，否则用
READ_INPUT 读入临时缓冲；实际长度由 ConDrv 的读取结果校验，不设任意上限。
## 4.18 WM_CLOSE 与 CTRL_CLOSE_EVENT：不实现（记录平台限制）

伪窗口不处理 WM_CLOSE，关闭请求直接结束会话；ConPTY 下conhost宿主会在 pty
管道关闭时向客户端投递 `CTRL_CLOSE_EVENT`，差分测试中系统宿主客户端确实收到
`EVENT=2`。

corehost 用同一 EndTask 路径投递 close 时（io 主线程、vt 输入线程、hwnd 为
nullptr 或伪窗口、flags 变体都试过），CSRSS 阻塞约 5 秒后返回成功，但客户端
`SetConsoleCtrlHandler` 的 close 处理器从未被调用；同一 API 的 Ctrl+C/Break 可以
触发。判断是外部 ConDrv server 不被 CSRSS 当作可分发 close 的 console host，
用户态无法绕过。模拟该行为只会让每次关闭多等 5 秒而没有可观察效果，因此按决定
不实现：WM_CLOSE 直接结束会话，不伪造 close 事件。应用在 corehost ConPTY 下不应
依赖 close 处理器（该差异记录在 gaps G18）。
## 4.19 默认 out_mode 与 conhost 一致

conhost宿主在 **ConPTY 模式**下强制
`SetDefaultVirtTermLevel(1)`，忽略用户注册表 `HKCU\Console\VirtualTerminalLevel`；
只有普通控制台窗口才使用注册表值；普通窗口在创建屏幕缓冲区时把
`ENABLE_VIRTUAL_TERMINAL_PROCESSING` 加入 `OutputMode`。

corehost 只服务 ConPTY，因此在会话初始化时：
1. 读取并记录 `VirtualTerminalLevel`（诊断用）；
2. 按 `effective_virtual_terminal_level(registry_level, conpty_mode=true)` 强制
   取 1（`console_state.hpp` 的纯函数，可单测）；
3. `state.output_mode = output_mode_for_virtual_terminal_level(1)`。

差分测试已把 `out_mode` 纳入比较（此前 corehost=3、conhost=7 的差异即由此修复）。

### 4.20 字符转码边界

所有字符转码集中在 `libcorehost/char_convert.hpp`，其它模块不再直接调用
`WideCharToMultiByte`/`MultiByteToWideChar`。分配时使用保守上界而不是精确
长度探测：

- UTF-8 长度 N → UTF-32/UTF-16 最多 N 个单元（每个 codepoint 至少 1 字节）；
- ANSI 长度 N → UTF-32/UTF-16 最多 N 个单元；
- 转换后以实际写入长度作为 API 返回、`NumRecords` 或 `Information`。

`*_exact_len` 仅保留给需要精确长度契约的 API 长度查询，不参与分配决策。

### 4.21 会话状态锁与 IO 模块

主循环、输入线程入口和窗口事件都收敛到现有 `message_router`：

- `message_router` 私有持有 `win32::shared_mutex`，所有 `std::unique_lock` 只出现在
  `message_router.hpp`；其他模块不包含锁、不提供 lock/guard。
- 输入线程每次读到一批数据只调用一次入口（`on_input_bytes`/`on_pty_signal`/
  `on_input_eof`），入口内加一次锁并处理整批；窗口可见性一次入口一次锁；
  ConDrv 每条消息在主循环内只加一次锁完成分派、pump、completion 准备。
- `pipe_bridge` 不再自己加锁；Ctrl 事件队列由 `message_router` 的同一把锁保护，
  解锁后逐项调用 `ConsoleControl(EndTask)`。
- `pseudo_window` 不持有锁：标题经 `WM_SETTEXT` 交给窗口线程，可见性/关闭只置位
  事件；主线程在 `message_router` 中处理。
- 依赖通过构造函数注入：state/buffers → bridge → api → window → message_router →
  输入/信号线程；没有后绑定。