# 设计取舍 0002：GetConsoleWindow 伪窗口

状态：已实现（行为分析基于系统控制台宿主的只读观察）

## 1. 需求边界

`GetConsoleWindow` 的契约是"返回与当前控制台关联的窗口"。依赖方很少真正需要
一个"能看见的窗口"，它们需要的是：

- 一个有效 `HWND`，可以 `IsWindow`、`GetWindowThreadProcessId`；
- 能同步/异步接收消息，`SendMessageTimeout` 不超时；
- 查询类调用（`GetWindowRect`、`GetClientRect`、`GetWindowText`、
  `GetWindowLongPtr`）返回自洽值；
- `WM_CLOSE`/`SC_CLOSE` 能关闭控制台。

因此伪窗口是"消息端点"，不是"渲染目标"。这是本设计的第一原则：凡是与渲染、
可见性、真实用户交互相关的能力，一律不实现，只保证不崩溃、返回值合理。

## 2. 系统控制台宿主的行为结论（不是照抄对象）

系统宿主在 ConPTY 模式下由 host 进程创建伪窗口，关键行为如下：

| 项 | 原版做法 | 真正需求 |
| --- | --- | --- |
| 类名 | `PseudoConsoleWindow` | 依赖方按类名 `FindWindow` 的兼容性；沿用同名最稳 |
| 窗口样式 | `WS_POPUP` + `WS_EX_TOOLWINDOW \| WS_EX_NOACTIVATE \| WS_EX_NOREDIRECTIONBITMAP` | 不可见、不抢焦点、无重定向表面；`WS_POPUP` 是硬需求 |
| 父子关系 | `CreateWindowEx` 的 owner 参数 + `SetWindowLongPtr(GWLP_HWNDPARENT)` | 必须是 owned 而不是 child：`GetAncestor(GA_ROOTOWNER)` 要能返回终端窗口，且不改变坐标语义 |
| 位置/尺寸 | 创建时 0,0,0,0 | 伪窗口不渲染，尺寸由后续查询按字符网格换算 |
| 窗口过程 | 静态过程 + `GWLP_USERDATA` 存实例，转发到成员函数 | 每个会话一个窗口、状态随会话 |
| `WM_SIZE` | `SIZE_RESTORED`→"显示"，`SIZE_MINIMIZED`→"隐藏"，写回终端 | 任务栏 `SwitchToWindow` 的边角行为；伪窗口需要把可见性变化告诉终端 |
| `WM_SHOWWINDOW` | `lParam==0`（显式 `ShowWindow`）时上报可见性 | 依赖方 `ShowWindow` 不能石沉大海 |
| `WM_ACTIVATE` | 把焦点交还给 owner | 不可见窗口不能被激活 |
| `WM_GETOBJECT` | 返回 UIA provider | 仅 UI 自动化需要；corehost 不渲染，可只返回 0 |
| IME | 进程内 `ImmDisableIME(DWORD_MAX)` | 无输入法窗口；corehost 无窗口控件，可省略或同样禁用 |
| 可见性上报 | 通过 VT writer 写 `ShowHideWindow` 序列 | **corehost 已有 `PtySignal::ShowHideWindow`**，直接复用信号管道 |
| 明确否决 | `HWND_MESSAGE` | 某些软件（Azure PowerShell 扩展）会直接崩溃；必须可被枚举/查找 |
| 明确否决 | `SetParent` | 会让 `WS_VISIBLE` 窗口被系统隐藏再 `SW_SHOW`，抢走前台 |

结论：原版伪窗口的复杂度主要来自"可见性/焦点的边角行为"，而不是窗口本身。
corehost 需要的是同一套**对外契约**，不需要照搬它的渲染、UIA、菜单实现。

## 3. corehost 的取舍

### 3.1 窗口类型

沿用 `WS_POPUP` + `WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_NOREDIRECTIONBITMAP`，
类名 `PseudoConsoleWindow`，owner 设为宿主终端窗口（defterm 下 WT 通过
`PtySignal::SetParent` 传来 hwnd；conpty 下没有 owner，用空）。

- 不用 `HWND_MESSAGE`：会失去 `EnumWindows`/`FindWindow` 可见性，等于没实现。
- 不用 child window：坐标语义会变成相对父窗口，依赖方 `GetWindowRect` 会错。
- `WS_EX_NOREDIRECTIONBITMAP`：省掉无用的 GDI 重定向表面。

### 3.2 线程与生命周期

窗口在**独立线程**创建并跑消息循环；线程名 `corehost-window`。与会话同生命周期：
会话建立后创建、会话结束前销毁。窗口线程不直接读写控制台状态：

- 需要对控制台产生副作用的动作（关闭会话、可见性变化）投递给主线程，或在
  锁内执行有界状态更新；
- 需要窗口信息的主线程动作（标题、尺寸）通过快照或请求完成事件获取；
- 禁止窗口线程同步等待主线程（避免与主线程等待窗口线程形成环）。

### 3.3 消息策略

只实现对外契约需要的消息：

| 消息 | 行为 |
| --- | --- |
| `WM_CREATE` | 保存 this 指针（`GWLP_USERDATA`） |
| `WM_SIZE` | `SIZE_MINIMIZED` → 向终端发隐藏信号；`SIZE_RESTORED` → 发显示信号 |
| `WM_SHOWWINDOW` | `lParam==0` 时按 `wParam` 发显示/隐藏信号 |
| `WM_ACTIVATE` | 有 owner 时 `SetFocus(owner)`，返回 0 |
| `WM_GETOBJECT` | 返回 0（不提供 UIA；阶段 C 的测试用消息驱动） |
| `WM_CLOSE` | 投递"请求关闭会话"命令，不直接 `DestroyWindow` |
| `WM_DESTROY` | 置关闭事件、取消 READ_IO、`PostQuitMessage` |
| 其他 | `DefWindowProcW` |

### 3.4 可见性信号

`PtySignal::ShowHideWindow`（`libcorehost/signal.hpp`）由 message_router 转成
伪窗口的 `ShowWindow` 请求；窗口线程执行期间抑制 `WM_SIZE`/`WM_SHOWWINDOW`
回显。外部 `ShowWindow`/直接 `WM_SIZE` 仍通过信号管道上报终端，因此不需要新增协议。

### 3.5 关闭语义

`WM_CLOSE` 不直接销毁窗口，而是：

1. 窗口线程把"请求关闭"标记为待处理并唤醒主线程；
2. 主线程走与真实关闭一致的路径：向客户端投递关闭事件（`CTRL_CLOSE_EVENT`）、
   超时后终止进程组（由系统关闭协议决定，不是我们的等待上限）；
3. 会话退出时通知窗口线程 `PostThreadMessage(WM_QUIT)`，join。

理由：关闭语义只能有一条实现，否则测试会看到"发消息关闭"和"终端关闭"两条
不一致的行为。

### 3.6 `GetConsoleWindow` 返回值

`api_handlers` 里当前的 `::GetConsoleWindow()` 调用替换为：返回伪窗口 HWND；
窗口尚未创建（启动竞态）时返回 0——与原版"没有窗口就是 0"一致，但正常路径
必须在首个 CONNECT 前创建完成，避免调用方拿到 0。

## 4. 需求规格（实现验收）

| 编号 | 需求 | 验收 |
| --- | --- | --- |
| B-R1 | `GetConsoleWindow` 返回非 NULL、进程内唯一、稳定 | 连续调用同一值；`IsWindow` 真 |
| B-R2 | `GetWindowThreadProcessId` 返回专用窗口线程（有名字） | 线程 id 稳定，名称 `corehost-window` |
| B-R3 | 不可见：不显示、不抢前台、不进任务栏 | 运行期间无可见窗口，前台窗口不变 |
| B-R4 | 同步消息不挂起 | 100 次 `SendMessageTimeout(…, 1s)` 无超时 |
| B-R5 | 查询类返回自洽值 | 初始 0×0（与原版一致）；收到 `ResizeWindow` 后矩形非零且随字符网格变化；标题为控制台标题 |
| B-R6 | `WM_CLOSE` 结束会话、客户端收到关闭事件 | 2 秒内会话退出；重复发送不重复关闭 |
| B-R7 | 销毁干净 | 100 次会话后 USER 对象数回到基线，无残留线程 |
| B-R8 | 状态修改不跨线程直接写 | 代码审查 + 并发压力无崩溃 |
| B-R9 | 未知消息不崩溃 | 消息清单逐条发送 |

## 5. 不做

- 渲染、滚动条、菜单、图标、DPI 真实处理、UIA provider、IME 窗口。
- 多窗口/多控制台（一会话一窗口）。

## 6. 实现前确认（已解决）

1. defterm 下 owner 何时可知：`PtySignal::SetParent` 在 handoff 后由 WT 发送，
   伪窗口可能在 owner 未知时创建（先无 owner，收到 SetParent 后
   `SetWindowLongPtr(GWLP_HWNDPARENT)`）。需要确认该时序。
2. `GetWindowRect` 的换算：创建时恢复原版的 0×0；只有收到 `ResizeWindow` 后才按字符网格 × 8×16 调整，因此依赖初始非零矩形的旧测试需要调整。

3. `WM_CLOSE` 与客户端关闭事件的时序需要与 `conpty` 现有的 EOF/断开路径统一。

以上三项均已按第 7 节落地。

## 7. 实现状态（2026-11）

以下契约已落地并纳入 `ConPTY.PseudoWindow`：

- owner：`PtySignal::SetParent` → `SetWindowLongPtrW(GWLP_HWNDPARENT)`；`GetAncestor(GA_ROOTOWNER)` 返回 owner。
- 可见性：`PtySignal::ShowHideWindow` → 窗口线程 `ShowWindow(SW_SHOWNOACTIVATE/SW_MINIMIZE)`。窗口线程执行系统调用期间用私有 `_suppress_visibility` 抑制 `WM_SIZE`/`WM_SHOWWINDOW` 回调，避免把 WT 发起的 show/hide 再回显成 VT 序列。创建期 `WM_SIZE` 不 `CancelIoEx`；真实外部可见性变化仍取消 READ_IO 以唤醒主循环。
- resize：初始窗口按原版创建为 0×0；`PtySignal::ResizeWindow` 更新 `screen_buffer_size` 后，窗口线程 `SetWindowPos` 到 `cells * 8x16`（`<=0` 回退 640×400）。
- `WM_ACTIVATE`：按原版，只要有有效 owner 就 `SetFocus(owner)`，不区分 active/inactive。
- `WM_DESTROY`：置关闭事件、取消 READ_IO、`PostQuitMessage`；`stop_and_join` 在窗口已销毁时直接 join，避免向失效 HWND 发消息。
- 保留：`WM_CLOSE` 的 corehost 会话关闭语义、默认窗口文本标题、初始隐藏、同步消息不挂起。

仍刻意不做：UIA provider、进程级 ImmDisableIME、`GWL_CONSOLE_WNDALLOC` 的 PID/TID/BKCOLOR 额外字节。原版 0×0 初始尺寸已恢复。