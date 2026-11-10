# 设计取舍 0004：把终端输入从 pipe_bridge 拆到输入线程

状态：方案已定，待 A4 实施（本文是实施顺序与风险，不含代码）

## 1. 问题

`pipe_bridge` 是一个 3000 行的混合体：它同时持有

- 输入侧：`_readbuf`、`_read_total`、`_queued_vt_input`、`_utf8_decoder`、
  `_input_parser`、`_input_raw_buf`、`_engine`、`_cooked_buf`、`_cooked_cursor`；
- 输出侧：`_vt_output`、`_output_parser`、`_output_raw_buf`、`_terminal`；
- 会话侧：`_pending`、`_io`、`_signal`、`_processes`、`_conversion`、`inp`。

A4 要求输入线程拥有自己的缓冲与状态。直接把 1000+ 行处理代码整体搬家风险极高：
`process_input`、cooked 编辑、`complete_pending` 与输出/会话状态交织，一次改错
很难定位。

## 2. 拆分策略：状态先搬、代码后搬

### 2.1 状态对象

新增 `vt_input_state`（`libcorehost/input_thread.hpp`），成员就是上表"输入侧"那
几项，语义与原成员一一对应。它由 `vt_input_thread` 持有（随线程对象构造/析构），
生命周期覆盖整个会话。

### 2.2 用引用别名避免大规模改名

`pipe_bridge` 增加构造函数参数 `vt_input_state&`，并把原来的成员变成**引用别名**：

```cpp
struct pipe_bridge {
    vt_input_state &_input;
    std::array<char8_t, ...> &_readbuf = _input.readbuf;
    DWORD &_read_total = _input.read_total;
    // ... 其余输入侧成员同样处理
};
```

这样 `process_input`、cooked 编辑、`complete_pending` 里所有 `_readbuf` 之类的
写法**一处都不用改**，代码仍是原来那份，但状态实际存放在输入线程对象里。

代价：`pipe_bridge` 变成不可赋值（引用成员），不可默认构造。它本来就只在会话
装配处构造一次，因此可接受；构造顺序必须保证 `vt_input_state` 先于 bridge 构造、
后于 bridge 析构。

### 2.3 处理调用点

`vt_input_thread` 的循环：

```
read（阻塞，cancellable_read，写线程私有 staging 缓冲，锁外）
  -> 取会话锁
  -> bridge.on_input_bytes(staging)     // 追加到 _readbuf，调用原有解析/完成逻辑
  -> 解锁
  -> SetEvent(input_ready)              // 仅用于唤醒主线程重新评估 pending/退出
```

两个要点：

1. **锁内不做阻塞读**。`_readbuf` 是桥接给 completion 用的持久缓冲，不能在锁外被
   写、锁内被读。因此线程先在锁外把管道数据读进自己的 4KB staging 缓冲，再在
   锁内拷进 `_readbuf` 并解析。多一次 4KB 拷贝，换取"锁内无阻塞"。
2. `on_input_bytes` 需要处理"空间不足"：`_readbuf` 剩余空间为 0 时先
   `complete_pending()`（原有语义：`vt_read_status::full`），再继续拷贝剩余字节。
   这个循环必须保留 `_read_total` 的原有含义，否则 RawRead/ReadConsole 的返回
   字节会错位。

因此原有输入处理函数不需要复制一份"线程版本"，只是调用者从主循环变成了输入
线程。这是本方案最重要的性质：**行为等价性靠"同一份代码、不同调用者"保证，
而不是靠重写一份等价逻辑。**

## 3. 分步实施（每步可构建、可回归）

| 步 | 内容 | 验证 |
| --- | --- | --- |
| S1 | 引入会话锁与 `input_ready` 事件；主循环每轮在锁内 dispatch/flush，等待时释放；行为不变 | 构建 + ctest 15/15；单线程无竞争 |
| S2 | 新增 `vt_input_state` 与引用别名；状态从 bridge 成员迁到 state；线程尚未接入 | 构建 + ctest；`sizeof` 与字段访问自检 |
| S3 | 新增 `vt_input_thread`（阻塞读 + 锁内调用现有处理）；会话装配处启动线程；主循环不再调用 `on_idle` | 键盘/行编辑/粘贴/CPR 用例；ctest |
| S4 | 主循环等待集改为 {InputAvailableEvent, input_ready}，删除 16ms 与 0ms 等待；pending 等待改为等事件 | 空闲 CPU/唤醒计数；ctest |
| S5 | 退出协议：停止标志 + `cancellable_read::wait_for_exit` + join；删除 `pipe_bridge_io` 的 peek 路径 | 关闭/断开/强制停止用例；句柄与线程计数 |
| S6 | 审计与日志：线程名、唤醒计数、错误分类 | 代码审查 + 测量记录 |

S1 与 S2 都不改变行为，可以单独提交；S3 是风险最高的一步，必须单独提交以便
回滚。

## 4. 关键风险与对策

| 风险 | 对策 |
| --- | --- |
| 引用成员导致对象不可移动/赋值，装配处编译错误 | 装配处一次构造；如需移动，改为指针成员 + 空指针断言 |
| 线程在 bridge 析构后仍访问 state | 线程先 join 再析构 bridge；析构顺序写进会话装配的注释 |
| 锁内调用 `flush` 可能阻塞（对端不读） | S4 之前保持现状；S4 评估"锁内追加、锁外写"的拆分 |
| 主循环与输入线程同时处理 pending 导致双完成 | pending 的读写全部在锁内；`has_pending()` 判定与完成在同一临界区 |
| 输入线程持有锁时间过长导致 ConDrv 消息饥饿 | 处理一块数据即释放；用大段粘贴压力测试测量持锁时长 |
| `on_idle` 原本还负责信号管道轮询 | 信号线程（A5）接管；S4 删除 `on_idle` 前必须确认 A5 已接入 |

## 5. 需要同步修改的调用点（清单）

- `conpty.cpp`：会话装配顺序（state → bridge → 线程 → 主循环）。
- `io_loop.hpp`：去掉 `on_idle`/`has_pending` 轮询，改为锁 + 事件等待。
- `message_router.hpp`：`on_idle`、`wait_for_pending_input` 删除或改语义。
- `pipe_bridge.hpp`：`on_idle`、`wait_for_pending_vt_input`、`read_*_vt_input`、
  `peek_vt_input` 的职责移交；`pipe_bridge_io` 只保留 server 相关操作。
- `defterm.cpp`：初始 CONNECT 循环同样去轮询（A7）。

## 6. 验收

沿用 `plan/01` 第 10 节；本文补充三条过程性要求：

1. 每个 S 步单独提交，提交信息写明"行为是否变化"。
2. S3 之后必须手工验证一次真实终端输入（WT 下打字、退格、方向键、Ctrl+C）。
3. S4 之后必须给出空闲 30 秒的唤醒计数与 CPU 实测值，与 A1 基线对照。
