# 0006 进程列表 / 进程组列表：需求审查与简化方案

## 1. 从需求出发：到底需要哪些数据

只有下面四类行为依赖"当前控制台连接了哪些进程、各自在哪个进程组"：

1. **GetConsoleProcessList**：返回所有已连接 pid，顺序为新连接 → 旧连接
   （conhost `ConsoleProcessList::GetProcessList` 明确 reverse 遍历）；缓冲不足时
   `dwProcessCount` 返回所需数量，且 conhost 不写任何 pid（`E_NOT_SUFFICIENT_BUFFER`
   路径不设置 reply information）。
2. **键盘 Ctrl+C / Ctrl+Break**：投递给**所有**连接进程（不按组过滤）。
3. **GenerateConsoleCtrlEvent(event, group)**：
   - `group == 0`：所有进程；
   - `group != 0`：只投递给 `ProcessGroupId == group` 的进程；
   - conhost 对未知 group 返回 `E_INVALIDARG`（客户端 `BOOL FALSE`），并有一个
     legacy 兜底：若找不到该组，则把 group 当 pid、检查其父进程是否在本控制台，
     是则合成一个 group entry 再投递。
4. **AttachConsole / defterm 预附加**：CONNECT 加入、DISCONNECT 移除；同一 pid
   重复 CONNECT 去重。

close/logoff/shutdown 事件**不实现**（G18），不构成需求。

结论：只需要一个按连接顺序保存的 `(pid, group)` 列表；变更方是
CONNECT/DISCONNECT（`io_state`），读取方是 Ctrl 事件投递与两个 L3 API
（`pipe_bridge`）。

## 2. 当前实现的事实

- `io_state`：两个并行数组 `process_list[64]` / `process_groups[64]` + count；
  `add_process` 去重并把 group 0 归一化成 pid；`remove_process` 同时 `std::move`
  两个数组。
- `pipe_bridge`：`process_list_snapshot` 再把同样的两个数组复制一份；
  `message_router` 在 CONNECT/DISCONNECT 后调用 `sync_process_snapshot()`；
  `conpty.cpp` 另有一个 `copy_process_list()` 做同样的事（初始化 / defterm 预附加）。
- `snapshot` 的 `view()`、`group_view()` 无调用者；`max_processes = 64` 在两处重复。
- `copy_for_group`、`copy_newest_first` 分别服务 Ctrl 事件和 GetConsoleProcessList。
- `send_ctrl_event` 先在锁内复制 targets，再对每个 pid 调
  `ConsoleControl(ConsoleEndTask)`；调用时**仍持有会话锁**（输入线程的
  `on_input_bytes` / 主循环 dispatch 都持锁）。
- 现有行为与 conhost 不一致的点：
  - GetConsoleProcessList 缓冲不足时仍写前缀 pid，`Information = count*4`；
    conhost 不写数据。
  - `GenerateConsoleCtrlEvent` 未知 group 仍返回成功；conhost 返回 E_INVALIDARG。
  - group 0 归一化成 pid（conhost 原样保存 `Cac.ProcessGroupId`；对普通非组长
    进程，conhost 依靠"父进程在列表"的兜底支持按 pid 定向，而 corehost 直接按
    pid 匹配）。

## 3. 能否简化：可以，且收益明显

**目标结构**：单一 `process_registry`（session 持有），`io_state` 与
`pipe_bridge` 共享只读引用；一条 `std::vector<process_entry{DWORD pid; DWORD group;}>`。

删除：
- `process_list_snapshot.hpp` 整个类；
- `io_state` 的两个数组、`max_processes`、双数组 `remove`；
- `message_router::sync_process_snapshot`、`conpty.cpp::copy_process_list`；
- `assign`/`view`/`group_view` 与两处 64 常量；
- bridge 的测试注入改为 `set_process_registry`，不再保留旧的列表注入接口。

收益：
- 单一真相，不会再出现"CONNECT 后忘了 sync"的漂移（当前日志专门记录
  `io.process_count` vs `bridge.process_count`，本身就是同步复杂度的信号）；
- 去掉 64 进程的静默截断（conhost 是无上限 list；超过 64 时当前实现会漏投
  Ctrl+C、GetConsoleProcessList 计数错误）；
- 去掉两数组同序维护，`remove` 只需一次 `erase`；
- 净代码量预计减少 ~80 行，并少一个类。

**但不能只删除快照**：`send_ctrl_event` 在持会话锁时调用 `EndTask`，一次可能
阻塞约 5s；期间主循环无法处理 DISCONNECT/CONNECT（需要同一把锁）。当前"先复制
targets"恰好把"读列表"和"发送"分开，但复制并不能解决持锁发送。conhost 的模型是
控制事件只先记录目标/标志，解锁后再逐个投递 EndTask。
要真正简化并保持正确，应分两步。

## 4. 推荐的两阶段方案

### 阶段 A（低风险，纯结构简化，不改可观察行为）

1. 新增 `process_registry`（`libcorehost/process_registry.hpp`）：
   ```cpp
   struct process_entry { DWORD pid; DWORD group; };
   class process_registry {
     bool add(DWORD pid, DWORD group);   // 去重
     bool remove(DWORD pid);
     size_t count() const;
     size_t copy_for_group(DWORD group, std::span<DWORD> out) const;
     size_t copy_newest_first(std::span<DWORD> out) const;
     std::span<const process_entry> entries() const;
   };
   ```
2. session 创建 registry；`io_state` 与 `pipe_bridge` 构造函数接收 `process_registry&`；
   删除 snapshot/sync/copy 辅助与 64 上限。
3. defterm 预附加改为 `registry.add(pid, pid)`；CONNECT 仍由 `io_state` 调
   `registry.add`。
4. 发送语义暂时保持不变（锁内复制 targets 再 EndTask），确保这一步无行为变化。
5. 测试：向 `process_registry` 注入后调用 `bridge.set_process_registry`；现有 group 过滤、
   GetConsoleProcessList、defterm 自测全部保持通过。

### 阶段 B（行为修正 + 进一步简化，建议分项提交）

1. `GetConsoleProcessList`：缓冲不足时不写 pid，`Information = 0`，
   `dwProcessCount = required`（对齐 conhost `E_NOT_SUFFICIENT_BUFFER` 路径）。
   现有单测 `test_regression_l3_process_list_validation_and_output_size` 需要改为
   断言"无数据 + count=3"。
2. `GenerateConsoleCtrlEvent(group != 0)`：若没有进程的 group 匹配，返回
   `status_invalid_parameter`（客户端 FALSE）。conhost 的"group 当作 pid + 父进程
   在列表"兜底属于 legacy，可在文档中标注不复刻。
3. **控制事件延迟投递**（这一步才是真正的简化）：`bridge` 在锁内只记录
   `{event, targets}`，由 io_loop 在释放会话锁后调用 EndTask。这样：
   - 不再需要"发送前复制 targets"这一层；
   - 修复持锁 EndTask 阻塞其它客户端的最长 5s 问题；
   - 与系统宿主"记录事件、解锁后逐进程投递"的模型一致。
   代价：需要一个小队列/一次性 pending 字段（事件频率低，不需要通用队列）。

## 5. 关于 group 0 归一化（需要明确决策）

conhost 在 CONNECT 中原样保存 `Cac.ProcessGroupId`，过滤时按原值匹配；对普通
（非 CREATE_NEW_PROCESS_GROUP）进程，它依靠"group 找不到 → 用 group 当 pid →
父进程在本控制台则合成 entry"的兜底来支持"按子进程 pid 定向"。

corehost 当前把 0 归一化为 pid，效果上覆盖了"按普通子进程 pid 定向"的常见场景，
实现更简单，但会放过 conhost 会拒绝的边角情形（父进程不在本控制台却被定向）。
建议：
- 若以"简化 + 常见行为正确"为目标：保留归一化，并在注释/文档中写明这是有意的
  简化（不实现父进程校验与合成 entry）；
- 若以"逐项对齐 conhost"为目标：改为原样保存 + 未知 group 校验，并单独评估
  legacy 兜底是否值得实现（预计很少被依赖）。

## 6. 结论

- **能简化**：单一 `process_registry` + `vector<process_entry>` 可以删掉快照、
  同步辅助、双数组和 64 上限，代码更少、不变量更少。
- **简化顺序**：先做阶段 A（不改行为），再按需做阶段 B①②（对齐 conhost 的
  API 语义），最后做 B③（延迟投递，消除持锁 EndTask，进一步去掉复制层）。
- **不建议**：只删快照而保持"锁内直接遍历共享列表并 EndTask"（重入/阻塞风险）；
  继续用 64 上限（>64 时行为错误）；为了简化直接去掉 group（破坏
  GenerateConsoleCtrlEvent(group)）。
## 7. 实施状态（阶段 A 已完成）

已提交的改动：
- 新增 [process_registry.hpp](libcorehost/process_registry.hpp)：`vector<process_entry>`
  保存 `(pid, group)`，提供 `add/remove/count/copy_for_group/copy_newest_first/assign`。
- `io_state` 持有唯一的 registry，删除两个并行数组、`max_processes=64` 与
  `add_process/remove_process`；CONNECT/DISCONNECT 直接修改 registry。
- `message_router` 构造时调用 `bridge.set_process_registry(io.process_list())`，
  bridge 不再保存快照；删除 `sync_process_snapshot()`、`conpty.cpp::copy_process_list()`
  与 `process_list_snapshot.hpp`。
- `pipe_bridge` 的 Ctrl 事件投递、GetConsoleProcessList 直接读同一 registry；
  独立单测未绑定 registry 时回退到本地实例。
- 删除 64 上限：超过 64 个进程不再漏投 Ctrl+C / 截断进程列表。
- 行为保持：group 0→pid 归一化、group 过滤、GetConsoleProcessList 新到旧顺序不变。
- 新增单测 `Process registry operations` 覆盖去重、移除/重加、无上限（100 项）、
  组过滤、新到旧顺序。

阶段 B（已完成）：
1. **GetConsoleProcessList**：缓冲不足时不写 pid、`Information=0`、
   `dwProcessCount` 返回所需数量（conhost `E_NOT_SUFFICIENT_BUFFER` 路径），
   单测用哨兵数据验证客户端缓冲不被改写。
2. **未知 group**：`GenerateConsoleCtrlEvent(group != 0)` 在没有匹配进程组时返回
   `E_INVALIDARG`（客户端 `FALSE`）；conhost 的 "group 当 pid + 父进程兜底"
   legacy 不复刻，已在 `api_ctrl_event` 注释中说明。
3. **控制事件延迟投递**：`send_ctrl_event` 在会话锁内只入队 `{event, targets}`
   （targets 在锁内从 registry 解析），输入线程处理完一批输入、io_loop 完成一次
   dispatch 后，在锁外调用 `deliver_ctrl_events()` 执行 `EndTask`。这消除了
   持锁 `EndTask` 最长约 5s 阻塞其它客户端的问题；事件队列用独立 SRW 锁保护，
   输入线程与主循环都可以安全 drain。行为对齐 conhost 的
   "记录事件（置标志）+ 解锁后逐进程投递"模型。

代码量说明：删除 snapshot（80 行）与各处同步/数组维护，新增 registry（约 95 行），
**净行数接近持平**；简化体现在状态从"三份数据保持同序"降为"一份数据"，并消除了
同步调用与固定容量截断。