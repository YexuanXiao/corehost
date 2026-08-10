# Signal Pipe Overlapped 化 + 单线程 I/O 重构 设计方案

## 一、背景与目标

当前 signal pipe（信号管道）是 `CreatePipe` 创建的**同步匿名管道**，读端必须用
阻塞式 `ReadFile`，因此两条消费路径各开了一个专用线程：

| 路径 | 线程 | 文件 |
|---|---|---|
| defterm 第一跳 | `signal_thread_proc` | `corehost/defterm/signal.cpp` |
| conpty 会话 | `pty_signal_thread_proc` | `libcorehost/signal.cpp` |

`pipe.txt` 提供了用 NT API（`NtCreateNamedPipeFile` + `NtCreateFile`）创建匿名
overlapped 管道的方法（MSDN 声称匿名管道不支持异步 I/O 是谎言）。

**目标：**
1. libconpty 的 signal pipe 改为 overlapped（读端）。
2. 删除 `signal_thread_proc` / `pty_signal_thread_proc` 两个线程。
3. **真正实现单线程 I/O**：信号管道的完成事件直接纳入主循环的等待集合
   （`WaitForMultipleObjects`），与 ConDrv server / 终端进程一起等待。
4. 删除为信号线程设计的整套 shutdown_event 机制（事件 + 16ms 时间片轮询 +
   `shutdown_signaled` EOF 判定），断开由主循环显式驱动 EOF。
5. 不可恢复的错误抛异常，只有"管道断开"这一种可预期状态返回 false。

## 二、现状梳理

### 2.1 signal pipe 创建点（3 处）

| 创建点 | 现状 | 读端消费者 |
|---|---|---|
| `libconpty/libconpty.hpp::CreateSignalPipe` | `CreatePipe` + `SetHandleInformation`（同步） | corehost `--headless` 的 pty_signal_thread_proc |
| `corehost/defterm/terminal_handoff.hpp` | `win32::create_pipe()`（同步） | signal_thread_proc |
| `corehost/comserver/com_server.cpp::EstablishHandoff` | `win32::create_pipe()`（同步） | conpty pty_signal_thread_proc |

写端（WT / libconpty API）均使用**同步 `WriteFile`**，因此写端必须保持同步。

### 2.2 下游消费线程（2 个）

- `pty_signal_thread_proc`（libcorehost/signal.cpp）：读 2 字节 `PtySignal` id +
  变长 payload，更新 `console_state`/`screen_buffer`；退出时置位 `shutdown_event`
  唤醒 bridge 的 pending 等待。
- `signal_thread_proc`（corehost/defterm/signal.cpp）：读 1 字节 `CONSOLECONTROL`
  code + 变长 payload，调 `user32!ConsoleControl` 转发 CSRSS。

### 2.3 线程时代遗留的 shutdown 机制（本次重构要删除）

`pipe_bridge_io::_shutdown_event` 是为"信号线程退出时打断主循环阻塞等待"设计的：

- `set_shutdown_event` / `has_shutdown_event` / `shutdown_signaled`
- `wait_shutdown_slice`（16ms 时间片轮询）
- `pipe_bridge::set_signal_shutdown_event` / `is_signal_shutdown_signaled` /
  `wait_for_signal_shutdown_slice`
- `conpty.cpp` 中无 signal pipe 时创建的 `vt_input_poll_event` 退化轮询事件

单线程 I/O 下这些全部多余：主循环自己等待信号完成事件、自己发现断开、
自己驱动 EOF 完成，不需要任何跨线程通知。

## 三、设计

### 3.1 `common/win32/pipe.hpp` — `win32::create_overlapped_pipe()`

按 `pipe.txt` 的 NT API 方法创建匿名管道，**读端 overlapped、写端同步**：

```
overlapped_pipe { win32::handle read;   // server 端，CreateOptions=0 → overlapped
                  win32::handle write;  // client 端，FILE_SYNCHRONOUS_IO_NONALERT → 同步 }
```

- `PIPE_ACCESS_INBOUND` 方向（server 读端 / client 写端），访问与共享位照抄
  pipe.txt 的 INBOUND 分支。
- **不缓存 `\Device\NamedPipe\` 目录句柄**：信号管道创建是低频一次性操作，
  每次调用直接 `NtCreateFile` 打开、RAII 自动关闭。
- **NTSTATUS 失败一律抛异常**（`win32::error`），不返回错误码。
- 使用 ntdll 的 **14 参数 `NtCreateNamedPipeFile` 签名**（无
  `AllocationSize`/`FileAttributes`，与 ntifs.h 的 16 参数内核签名不同；
  pipe.txt 的调用正是 14 参数版本）。

### 3.2 `common/win32/overlapped.hpp` — overlapped 读辅助

```cpp
enum class overlapped_io_status { pending, done, closed };
struct overlapped_io_result { overlapped_io_status status; DWORD bytes; };

// 发起 overlapped read（内部用自动复位 event 作为 hEvent）
overlapped_io_result begin_overlapped_read(win32::handle_view pipe, void* buffer,
                                           DWORD size, OVERLAPPED& ov);
// 完成事件已触发后取回结果（GetOverlappedResult 非阻塞）
overlapped_io_result finish_overlapped_read(win32::handle_view pipe, OVERLAPPED& ov);
```

**错误策略**：
- `ERROR_IO_PENDING` → `pending`；立即完成 → `done(bytes)`；
- 0 字节 EOF / `ERROR_BROKEN_PIPE` / `ERROR_PIPE_NOT_CONNECTED` / `ERROR_NO_DATA`
  → `closed`（唯一可预期的"正常失败"）；
- **其余错误（如 invalid handle）直接 `throw_last_error()`**，不静默吞掉。

### 3.3 `pty_signal_consumer`（libcorehost/signal.hpp/.cpp）

```cpp
class pty_signal_consumer
{
  public:
    pty_signal_consumer() noexcept = default;                 // 无管道
    pty_signal_consumer(win32::handle pipe, console_state& state, screen_buffer& sbuf);
    void start_read();
    [[nodiscard]] win32::handle_view event() const noexcept;  // overlapped hEvent
    [[nodiscard]] bool valid() const noexcept;                // 有绑定管道
    [[nodiscard]] bool handle_event();   // false = 管道断开
    [[nodiscard]] bool try_handle_event();
  private:
    win32::handle _pipe;
    console_state* _state;   // 指针成员：类可移动/可默认构造
    screen_buffer* _sbuf;
    win32::event _read_event;
    OVERLAPPED _ov{};
    std::byte _buffer[64];
    // 流式解析状态机：need_sig(2B) → need_payload(n) → 处理 → need_sig
};
```

- **指针成员替代引用成员**：类获得默认构造 + 移动赋值，conpty.cpp 里
  **栈上声明**（`pty_signal_consumer sig;`），`if (signal_pipe.valid()) sig = {...};`
  ——不需要 `std::unique_ptr` 堆分配。
- 删除 `_shutdown_event` 成员与构造参数（3.5 节用完成事件直接驱动）。
- `handle_event` 返回 false 只表示断开；不可恢复错误由 overlapped 辅助抛异常。
- payload 长度表：ShowHideWindow=2、ClearBuffer=0、SetParent=8、ResizeWindow=4。

### 3.4 `signal_consumer`（corehost/defterm/signal.hpp/.cpp）

与 3.3 同构（CONSOLECONTROL 协议：code 1 字节 + payload，含 dwSize 校验与
跳过多余字节）。断开返回 false，不可恢复错误抛异常。

### 3.5 bridge：shutdown event → 信号完成事件（真正单线程等待）

`pipe_bridge_io`：

```cpp
void set_signal_event(win32::handle_view event) noexcept;  // 绑定信号完成事件
[[nodiscard]] bool has_signal_event() const noexcept;
// 等待 {信号完成事件, timeout}；true = 完成事件就绪（有数据或断开）
[[nodiscard]] bool wait_signal_slice(DWORD timeout_ms) const;
// 删除：shutdown_signaled() / wait_shutdown_slice()
```

`pipe_bridge`：

```cpp
bool should_exit() const noexcept      // = vt_eof && !has_pending
void set_signal_event(win32::handle_view event) noexcept;
bool wait_for_signal_slice();          // 16ms 片内等待信号完成事件
// 删除：is_signal_shutdown_signaled() / wait_for_signal_shutdown_slice()
```

`wait_for_pending_vt_input` 重构（不再有"EOF 完成"分支）：

```
drain_available_vt_input()        → 有数据返回
wait_for_signal_slice(16ms)       → 完成事件就绪 → 返回（交还 io_loop 处理信号）
drain_available_vt_input()        → 超时片：再试一次
无 signal event                   → read_blocking（原阻塞语义）
```

`on_idle` 删除 `is_signal_shutdown_signaled()` 分支：PeekNamedPipe 0 字节
只表示"暂时没输入"，EOF 只由 peek/read 失败或主循环驱动信号断开产生。

### 3.6 `message_router`：新增断开入口

```cpp
// 信号管道断开：按终端 EOF 处理（置 vt_eof + 完成 pending）
void on_signal_disconnected() { bridge.complete_pending_with_eof(); }
```

`complete_pending_with_eof` 内部对无 pending 安全（`complete_pending` 有
`has_pending` 保护）。

### 3.7 `io_loop.hpp`：统一等待集合

`run_io_loop_no_setup(server, event, router, pty_signal_consumer* signal)`：

```cpp
// 唯一的等待原语：server 与信号完成事件一起等
const auto wait_io = [&](DWORD timeout_ms) {
    if (signal != nullptr)
        return win32::wait_any(server, signal->event(), timeout_ms);
    return win32::wait_one(server, timeout_ms);
};
// 信号就绪 → handle_event；断开 → on_signal_disconnected，返回 false 表示退出
const auto handle_signal_ready = [&](const win32::wait_result& w) {
    if (signal == nullptr || w.index != 1) return true;
    if (signal->handle_event()) return true;
    router.on_signal_disconnected();
    return false;
};
```

- 循环内所有 `wait_one(server/ev, ...)` 换成 `wait_io(...)`；
- 每个等待点：`handle_signal_ready` 返回 false → 退出；返回 true 且
  `wait.index == 1`（信号就绪）→ `continue` 回到循环顶重新评估状态；
- pending 循环：

```cpp
while (router.has_pending()) {
    router.wait_for_pending_input();          // bridge 内部同时监视信号事件
    if (signal != nullptr && !signal->try_handle_event()) {
        router.on_signal_disconnected();      // 断开：EOF 完成
        break;
    }
}
```

信号完成事件被 bridge 的 `wait_signal_slice` 纳入等待 → pending 期间
信号是事件驱动到达的，不是轮询补丁。

### 3.8 `conpty.cpp`：栈上装配，删除全部事件

```cpp
pty_signal_consumer signal_consumer;
if (signal_pipe.valid()) {
    signal_consumer = pty_signal_consumer{std::move(signal_pipe), state, sbuf};
    signal_consumer.start_read();
    bridge.set_signal_event(signal_consumer.event());
}
run_io_loop_no_setup(server, event, router,
                     signal_consumer.valid() ? &signal_consumer : nullptr);
```

- 删除 `signal_shutdown_event`、`vt_input_poll_event`、`sig_thread`、
  `pty_signal_thread_params`、`unique_ptr`。
- 无 signal pipe 时 bridge 不绑任何事件 → pending 等待自然退化为阻塞读
  （原语义），不再需要退化轮询事件。

### 3.9 下游创建点

- `libconpty/libconpty.hpp::CreateSignalPipe`：改用 `create_overlapped_pipe`，
  读端置 `HANDLE_FLAG_INHERIT`（进 corehost 子进程）。C API 边界保留
  try/catch → HRESULT（不能跨 DLL 抛异常）。
- `corehost/defterm/terminal_handoff.hpp`：`create_overlapped_pipe`；主循环
  `wait_any(terminal_process, sig.event(), INFINITE)`（已是单线程）。
- `corehost/comserver/com_server.cpp`：`create_overlapped_pipe`。

## 四、行为差异说明

| 场景 | 原行为 | 新行为 |
|---|---|---|
| pending 读等待期间信号到达 | 信号线程即时处理 | bridge 等待集合含信号完成事件，事件驱动处理（无轮询延迟） |
| 信号管道断开 | 线程置位 shutdown_event → 时间片轮询发现 → EOF | 主循环直接发现 → `on_signal_disconnected` → EOF 完成 → 退出 |
| 不可恢复错误（invalid handle 等） | 静默按断开处理 | 抛异常（win32::error） |
| WT / libconpty 写端 | 同步 WriteFile | 不变 |

## 五、验证计划

1. `cmake --build build`（Debug + Release）全量编译。
2. CTest 13 项（含 ConPTY.E2E、Edit.ConPTY.Real 真实 ConPTY 会话）。
3. bench 全场景（worker + host 模式）。
4. `pty_signal_consumer` 单元验证：ResizeWindow 状态更新、批量消息流式解析、
   断开检测。

## 七、执行记录（已完成）

### V3 简化（分析后的第二轮重构）

在 V2 基础上进一步审查，发现并处理：

**1. 提取公共基类 `win32::overlapped_pipe_reader`（消除 ~150 行重复）**
- 两个 consumer（`pty_signal_consumer` / `signal_consumer`）的 I/O 机械部分——
  缓冲管理（memmove/满缓冲检测）、overlapped 读生命周期（begin/finish/立即
  完成重读）、断开检测（`handle_event`/`try_handle_event`）——逐字重复。
- 提取到 `common/win32/overlapped_reader.hpp`：基类只做 I/O，派生类只实现
  协议解析（`try_parse_message()` 虚函数 + payload 表 + 处理动作）。
- 基类同时修正移动赋值：`_ov.hEvent` 显式重绑到本对象的事件句柄（原先依赖
  "句柄值不变"的隐式巧合）。
- 两个 consumer 各从 ~200 行降到 ~90 行，修复协议/缓冲 bug 只需改一处。

**2. 修复 `signal_consumer::read_next() noexcept` 的 terminate bug**
- 该函数内部调用可能抛异常的 `begin_overlapped_read`，但被标 `noexcept` →
  任何不可恢复错误直接 `std::terminate`，违背"错误抛异常"策略。
- 基类化后 I/O 函数统一为无 noexcept 版本，异常正常传播。

**3. `io_loop.hpp` 等待点统一**
- 4 个等待点的"信号就绪/断开/abandoned"处理收敛为单一 `wait_handle` 辅助
  （返回四态 `io_wait_action`），每个等待点只需 3 行分发；
- 行为统一：任何等待点信号就绪都回到循环顶重新评估状态（原先点 2/3/4
  信号就绪后继续走 READ_IO，现在统一 continue）。

**4. 新增跨读边界单测场景**
- consumer 单元验证增加"消息分两次写入"场景（头部一次、payload 一次），
  验证流式状态机跨读边界的正确性。

**5. `common/CMakeLists.txt`** 登记 3 个新头文件（`pipe.hpp` /
`overlapped.hpp` / `overlapped_reader.hpp`）到 FILE_SET HEADERS。

### 各轮结果

- ✅ V1（基础版）：线程 → overlapped + WaitForMultipleObjects。
- ✅ V2（单线程 I/O 化）：删除整套 shutdown_event 机制，断开由主循环显式
  驱动 EOF；错误抛异常；去目录句柄缓存；consumer 指针成员栈上装配。
- ✅ V3（简化）：公共基类消除重复 + noexcept bug 修复 + io_loop 等待统一。
- ✅ V4（READ_IO overlapped 化）：主循环改事件驱动四阶段循环，删除同步
  READ_IO 遗留（no_message 分支 / 0ms 消化 pending / server 句柄轮询），
  `defterm.cpp` 初始 CONNECT 循环同步迁移；实验验证 ConDrv READ_IO 完整
  支持 overlapped（挂起/取消/取回正常）。
- ✅ V5-B（帧原子假设）：`pty_signal_consumer`/`signal_consumer` 解析改为
  无状态单帧解析——写端（libconpty/WT）总是用一次 WriteFile 写完一整帧，
  一次 ReadFile 返回整数个完整帧；删除跨读边界状态（need_payload/need_skip）、
  memmove、buffer-full 分支，半帧残留即协议损坏按断开处理。

### 最终验证

- ✅ Debug + Release 全量构建通过（重新 configure 后）。
- ✅ CTest 13/13 通过（含 ConPTY.E2E、Edit.ConPTY.Real 真实 ConPTY 会话；
  `Signal disconnect` 回归测试覆盖新断开语义）。
- ✅ bench 24 场景通过（25MB VT 输出 + 键盘输入路径正常）。
- ✅ `pty_signal_consumer` 单元验证（V3）：ResizeWindow 状态更新、批量流式
  解析、跨读边界解析、断开检测、栈上移动赋值装配。
- ✅ `pty_signal_consumer` 帧原子单测（V5-B）：单帧、多帧合并（3 帧一次写）、
  半帧拆写 → 断开（新行为）、写端关闭 → 断开。

### 实施中发现的关键问题

**ntdll 的 `NtCreateNamedPipeFile` 是 14 参数签名**（无 `AllocationSize`/`FileAttributes`），
与 ntifs.h（WDK）的 16 参数内核签名不同。按 16 参数声明调用会返回
`STATUS_INVALID_PARAMETER`/`STATUS_ACCESS_VIOLATION`。pipe.txt 中 WT 的调用正是
14 参数版本；`common/win32/pipe.hpp` 采用该签名后一切正常。

### 与方案的偏差

- `CreateSignalPipe` 中写端不再设置 `HANDLE_FLAG_INHERIT`（原实现两端均可继承）；
  新实现只有读端需要继承（进入 corehost 子进程的 HANDLE_LIST），写端保留在本进程，
  行为无实际影响。

## 六、涉及文件清单

| 文件 | 动作 |
|---|---|
| `common/win32/pipe.hpp` | 新增；不缓存目录句柄；失败抛异常 |
| `common/win32/overlapped.hpp` | 新增；不可恢复错误抛异常 |
| `common/win32/overlapped_reader.hpp` | 新增（V3）；两个 consumer 的公共 I/O 基类 |
| `libcorehost/signal.hpp` / `signal.cpp` | 重构为 `pty_signal_consumer`（继承基类，无状态单帧解析） |
| `corehost/defterm/signal.hpp` / `signal.cpp` | 重构为 `signal_consumer`（继承基类，无状态单帧解析） |
| `libcorehost/pipe_bridge_io.hpp` | shutdown event → 信号完成事件；删除 `shutdown_signaled` |
| `libcorehost/pipe_bridge.hpp` | 同上；`on_idle`/`should_exit`/`wait_for_pending_vt_input` 重构 |
| `libcorehost/message_router.hpp` | 新增 `on_signal_disconnected` |
| `libcorehost/io_loop.hpp` | 统一等待集合 + 单一信号处理点 |
| `libcorehost/conpty.cpp` | 栈上 consumer；删除全部事件与线程装配 |
| `corehost/defterm/terminal_handoff.hpp` | `create_overlapped_pipe`（已单线程） |
| `libconpty/libconpty.hpp` | `CreateSignalPipe` 改 NT API |
| `corehost/comserver/com_server.cpp` | `create_overlapped_pipe` |
| `plan.md` | 本文档 |
