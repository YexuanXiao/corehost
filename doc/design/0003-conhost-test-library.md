# 设计取舍 0003：测试库与测试框架

状态：已决定

## 1. 约束

- `agent.md`：禁止联网搜索。因此不能下载 Catch2/GoogleTest/doctest。
- `agent.md`：测试必须能自动化执行，并且要"学习流行的测试框架"。
- 现有测试是一个 `RUN_TEST` 宏 + 全局计数（`tests/test_common.hpp`），每个
  测试是可执行文件，CTest 逐个执行。风格简单，但缺超时、隔离、过滤、诊断。
- C（defterm）与 D（conpty）都需要会话驱动、输出泵、屏幕断言，重复建设会
  产生两套不一致的测试，因此库只建一次。

## 2. 候选方案

### 方案 A：自研轻量测试库（选定）

在工作区内实现最小但完整的 runner：用例注册（静态注册表）、过滤（标签/名称
子串）、断言（`REQUIRE` 失败即终止当前用例，`CHECK` 继续）、超时、跳过、
汇总报告、退出码；会话驱动、屏幕模型、诊断作为库的独立组件。

- 优点：零依赖、可控、与仓库风格一致；能精确控制超时与进程隔离。
- 缺点：需要自己写并维护；功能少于成熟框架。
- 代价可控：核心 runner 约几百行，测试库自身有测试。

### 方案 B：复制一个单头框架（如 doctest 单文件）进仓库

否决。禁止联网无法获取；即便从本机某处复制，也引入来源不明的大段第三方代码，
与本仓库"单一职责、清晰"的风格冲突，且需要额外维护补丁。

### 方案 C：沿用现有 `RUN_TEST` 宏

否决。它没有隔离与超时，一个用例挂在 `ReadFile` 上会拖死整个测试进程；也没有
跳过语义（WT/sshd 不可用时无法区分"跳过"与"通过"）。可以保留其风格作为底层
断言宏的写法参考。

## 3. 选定方案的结构

```
tests/lib/                 测试库（header-only 优先）
  runner.hpp               注册、过滤、执行、超时、报告
  assertions.hpp           REQUIRE / CHECK / 期望宏
  environment.hpp          前置条件与跳过
  session/                 conpty 会话驱动、输出泵
  defterm/                 defterm 驱动（delegation 切换、启动、恢复）
  screen.hpp               最小 VT 屏幕模型与断言
  diagnostics.hpp          失败产物收集
tests/defterm/             阶段 C 用例
tests/conpty/              阶段 D 用例
tests/lib_selftest/        测试库自测
```

关键决定：

### 3.1 用例隔离用"父 runner + 子进程"

每个用例在独立子进程中执行（父进程按用例名启动自身并带过滤参数），父进程
负责超时、捕获输出、汇总。理由：被测对象会创建线程、窗口、全局 COM、修改
进程级状态（控制台模式/代码页）；同进程串行执行会互相污染，且一次挂起会带走
全部用例。子进程隔离让超时可靠、失败可复现。

代价：启动开销。用例数量在几十到一百量级，可接受；用例内部可以一次验证多个
相关断言，避免碎片化。

### 3.2 等待一律"条件 + 截止时间"

库不提供 `sleep_for(固定)` 作为同步手段。所有等待是 `wait_until(predicate,
timeout)`：轮询间隔短（5–10ms），超时即失败并附带状态快照。固定 sleep 只允许
出现在"被测程序已知需要时间且无法观察信号"的极少数场景，且必须在用例中注释
原因。

### 3.3 屏幕断言用最小 VT 模型

不匹配原始字节：终端输出包含颜色、光标定位、清屏等噪声，字节匹配随实现变化
就碎。库实现最小屏幕模型（文本、光标、SGR、ED/EL、CUP、备用屏幕），断言
"最终屏幕内容/光标/属性"。模型遇到不支持的序列显式报错，避免静默忽略导致
假通过。

### 3.4 前置条件与跳过

`requires(cond, reason)`：环境不满足时用例记为 SKIP 并输出原因（WT 未安装、
非交互会话、sshd 不可用、vim/edit 缺失）。跳过不计为通过；汇总报告单独计数；
可用参数把 SKIP 升级为失败（供"必须覆盖"的 CI 使用）。

### 3.5 真实 WT 与确定性替身分层（阶段 C）

- L1：测试实现一个最小 handoff 接收方（COM 本地服务器），验证 corehost 的
  defterm 入口与协议，确定性、可在无头环境运行。
- L2：真实 WindowsTerminal，验证端到端与窗口交互；环境不满足则 SKIP。

理由：真实 GUI 的启动/焦点/版本让回归极不稳定，而 defterm 入口的逻辑错误
（CONNECT 处理、信号转发、退出）可以在 L1 稳定捕获。任务要求"唤起
WindowsTerminal 并交互"，所以 L2 是验收项，L1 是日常回归主力。

## 4. 测试库自身的质量要求

1. runner 的通过/失败/超时/跳过/过滤路径都有自测。
2. 库代码符合仓库风格：现代 C++、单职责、错误检查、无 `catch` 滥用（runner
   捕获子进程失败状态而不是 C++ 异常）。
3. 库不得依赖被测对象之外的全局状态；不得修改用户环境（注册表、sshd 配置）
   而不恢复。
4. 失败产物默认落盘到构建目录下的 `test-artifacts/<用例>/`，包含：用例参数、
   子进程 stdout/stderr、会话日志、屏幕快照、耗时段。

## 4.1 已实现（C1 第一批）

- `tests/lib/runner.hpp`：表驱动用例（`test_case{name, tags, fn}`）、`REQUIRE` 失败即结束
  当前用例、`SKIP(reason)` 记录跳过原因、按名称/标签过滤、返回 `run_stats`。
- 断言用返回值而不是异常，避免在测试库引入 `try/catch`。
- 自测 `tests/test_runner_selftest.cpp`（`Runner.SelfTest`）覆盖通过/失败/跳过/过滤。
- 未实现：子进程隔离、用例超时、环境探测。它们落地前，新用例必须保证自己不会
  无限阻塞（否则整个测试进程挂起）。

## 5. 备选与后续

- 若后续允许引入第三方框架，runner 的断言与用例注册接口保持"可迁移"形态，
  避免用例大面积重写。
- 若子进程隔离开销成为瓶颈，可对纯逻辑用例（无会话）提供"同进程执行"标记，
  但默认仍是隔离执行。
## 测试进程不得弹出可见窗口

测试运行时出现的 conhost 窗口通常来自两类进程创建：

1. **真实默认终端冒烟**：`Defterm.RealTerminal` 用 `CREATE_NEW_CONSOLE` 启动
   `cmd.exe`，让 Windows 把控制台创建委托给默认终端。这会在桌面上真实创建
   控制台窗口（默认终端为 WT 时表现为 WT/conhost 窗口一闪而过）。该用例改为
   **显式 opt-in**：只有设置 `COREHOST_TEST_REAL_TERMINAL=1` 才运行，否则 SKIP。
2. **测试子进程**：`runner::run_isolated` 的每个隔离用例、`condrv` 的宿主进程
   都是控制台程序。父进程有控制台时它们继承，不新建窗口；父进程没有控制台时
   （Visual Studio Test Explorer、GUI 宿主等）Windows 会给每个子进程新建一个
   可见控制台 → conhost 闪窗。现在：
   - `run_isolated` 在 `GetConsoleWindow() == nullptr` 时用 `CREATE_NO_WINDOW`；
   - `condrv` 启动宿主固定加 `CREATE_NO_WINDOW`（宿主不需要控制台）；
   - ConPTY 客户端由 `PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE` 提供伪控制台；
     libconpty 启动的 conhost 一律带 `--headless`；`ssh_server`/`run_process`
     已使用 `CREATE_NO_WINDOW`。

因此正常 `ctest` 只会出现 `--headless` 的 ConPTY 宿主和 `CREATE_NO_WINDOW`
（命令行 `conhost.exe 0x4`）的控制台宿主，二者都没有可见窗口。
3. **ssh 非交互远程命令**：Windows OpenSSH sshd 为非 PTY 命令启动的
   `cmd.exe` / `powershell.exe` 会新建**可见**的控制台窗口（sshd 自己虽然用
   `CREATE_NO_WINDOW` 启动，子进程仍会拿到新控制台）。`ssh_server` 现在用
   `CreateDesktopW` 创建一个隐藏桌面，并通过 `STARTUPINFO.lpDesktop` 把 sshd
   放上去；sshd 的子进程继承该桌面，窗口不再出现在用户桌面。`CreateDesktopW`
   失败时退回默认桌面（测试仍可运行，只是可能闪窗）。