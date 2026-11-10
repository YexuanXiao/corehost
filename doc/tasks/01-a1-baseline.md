# A1：基线调查

状态：DONE（结论见下；性能数值在 A6 改造前采集，方法已固定）

## 1. 现状唤醒路径

会话主循环（`run_io_loop_no_setup`）与初始 CONNECT 循环（`defterm`）都是
"短超时等待 + 主动窥探"结构，一个线程轮流服务全部输入源：

```
for (;;) {
    若 无 pending 且 无待提交 completion:
        on_idle():                       # 主动处理一次输入
            poll 信号管道 (PeekNamedPipe)
            peek vt_in (PeekNamedPipe) → 有数据就读、解析
        should_exit?
        wait(server, 16ms)               # ← 空转来源
    提交上一条 completion / READ_IO
    若 no_message:
        flush 输出
        wait(server, 0ms)
        on_idle()
        wait(server, 16ms)               # ← 空转来源
    若 请求挂起:
        while (has_pending) wait_for_pending_input():
            drain vt_in
            poll 信号管道
            wait(vt_in, 16ms)            # ← 空转来源
            再 drain vt_in
}
```

defterm 侧还有两处同构结构：初始 CONNECT 循环的 `wait(input_event, 16ms)`，以及
handoff 完成后等待终端进程退出的 `wait(terminal_process, 16ms)` + 信号管道轮询。

## 2. 轮询点清单

| 位置 | 形式 | 周期 | 服务对象 | 处理计划 |
| --- | --- | --- | --- | --- |
| `libcorehost/io_loop.hpp:66` | `wait(server, 16ms)` | 16ms | 等 ConDrv 新消息 | 改为等待 InputAvailableEvent，无限等待（A6） |
| `libcorehost/io_loop.hpp:94` | `wait(ev, 0)` | 每轮 | 判断是否先 flush 输出 | 合并进等待模型，去掉 0ms 探测（A6） |
| `libcorehost/io_loop.hpp:132` | `wait(server, 0)` | 每轮 | 让驱动消化 pending | 删除；由事件驱动（A6） |
| `libcorehost/io_loop.hpp:147` | `wait(server, 16ms)` | 16ms | no_message 后等待 | 同上（A6） |
| `libcorehost/pipe_bridge.hpp:1261` | `wait(vt_in, 16ms)` | 16ms | 等终端输入字节 | 由终端输入线程阻塞读替代（A4） |
| `libcorehost/pipe_bridge.hpp`（`on_idle`/`drain`） | `PeekNamedPipe` 轮询 | 每轮 | 抽干 vt_in | 线程内阻塞读，主线程不再窥探（A4） |
| `libcorehost/signal.cpp`（`poll`） | `PeekNamedPipe` 轮询 | 每轮 | WT 信号（resize/clear/parent/show） | 信号线程阻塞读（A5） |
| `corehost/defterm/defterm.cpp:210,296` | `wait(input_event, 16ms)` | 16ms | 初始 CONNECT 循环 | 收敛到统一等待模型（A7） |
| `corehost/defterm/terminal_handoff.hpp:115` | `wait(terminal_process, 16ms)` | 16ms | handoff 后等终端退出 / 转发信号 | 信号线程 + 无限等待（A7） |

结论：空闲时主循环约每 16ms 醒一次（≈62 次/秒），每次醒来至少两次 `PeekNamedPipe`
和一次 `DeviceIoControl`。延迟下限由 16ms 决定。

## 3. 输入源与状态归属（改造目标）

| 状态 | 现状归属 | 目标归属 |
| --- | --- | --- |
| vt_in 原始读缓冲、跨请求残留、UTF-8 增量解码器 | `pipe_bridge` | 终端输入线程（私有） |
| VT 输入解析器 + 原文缓冲 | `pipe_bridge` | 终端输入线程（私有） |
| 键盘修饰键/Win32 Input 解码状态 | `pipe_bridge`（`vt_input_engine`） | 终端输入线程（私有） |
| cooked 行缓冲与行内编辑光标 | `pipe_bridge` | 终端输入线程（私有） |
| 输入模式快照（Ctrl+Z/Ctrl+C 语义） | `console_state`（主线程） | 主线程发布只读快照，输入线程读取 |
| Console API 可见 `INPUT_RECORD` 队列 | `input_buffer`（主线程） | 不变（仅主线程写） |
| ConDrv 挂起请求 | `pending_io_state`（主线程） | 不变 |
| 屏幕/光标/属性/CP/标题 | `console_state`/`screen_buffer`（主线程） | 不变 |
| 终端光标与行编辑边界追踪 | `pipe_bridge`（主线程） | 不变（回显渲染也在主线程） |
| VT 输出字节缓冲 | `pipe_bridge`（主线程） | 不变（单写者） |
| 信号消息解析中间态 | `pty_signal_reader`（主线程） | 信号线程（私有）；命令交主线程 |
| 窗口句柄/窗口过程状态 | 无 | 窗口线程（阶段 B） |

## 4. 回归基线

| 项 | 值 |
| --- | --- |
| 提交 | `bc1432a`（父提交）+ 文档提交 |
| 配置 | Ninja / Debug / MSVC 14.52.37025 / `COREHOST_DISABLE_LOG=OFF` / `COREHOST_LOG_LEVEL=2` |
| 构建 | 通过，无待编译目标 |
| 测试 | 15/15 通过，总耗时 7.30s（`ctest --test-dir build --output-on-failure`） |
| 环境 | 交互式桌面；WindowsTerminal 已安装（1.24.12741 / 1.25.3131）；`ssh.exe`、`sshd.exe` 存在；`edit` 可用 |

15 个用例构成 A 阶段的回归门槛：任何一步改造后必须保持全绿。

## 5. 测量方法（A6 前采集基线，A10 复测）

固定方法，避免"看起来更快"：

1. **空闲 CPU**：启动一个 conpty 会话（80×25），客户端执行
   `cmd /c "ping -n 31 127.0.0.1 >nul"`（约 30 秒无输出）。用
   `(Get-Process corehost).CPU` 在 t0/t30 采样，记录 30 秒内 CPU 秒数与占单核
   百分比；重复 3 次取中位数。
2. **唤醒次数**：在会话结束时打印主循环唤醒计数（A6 引入计数器；基线用 CPU
   与"每 16ms 一次"的理论值对照，不单独测量）。
3. **输入时延**：harness 向 conpty 输入端写入字符 `a`，同时记录时间；从输出端
   读到该字符的回显时停止。重复 100 次（会话内连续输入，避免启动开销），记录
   P50/P95/最大值；同一个 harness 在改造前后各跑一次。
4. **退出时延**：关闭 conpty（`ClosePseudoConsole`）到 corehost 进程退出（`wait`
   返回）的时间，重复 10 次取中位数。

## 6. 结论与下一步

- 轮询点共 9 处，全部集中在"会话主循环 + 输入/信号 + defterm 两个循环"，没有
  隐藏的第三方计时器。
- 主循环改造（A6）可以一次性去掉其中 5 处；输入线程（A4/A5）去掉 2 处；defterm
  循环（A7）去掉剩余 2 处。
- 回归门槛明确：`ctest` 15 个用例；性能指标方法已固定，A6 前采集。
- 下一步 A2：`common/win32` 的 SRWLock 封装。理由：A3 的线程工具与 A4/A5 的
  队列都依赖它，先做可以避免返工。
