# corehost 文档索引

本目录记录 agent.md 四项任务的计划、子任务状态和设计取舍。文档是任务的一部分：
任务没写进文档，就等于没计划；实现没更新文档，就等于没完成。

## 目录结构

| 目录 | 内容 | 是否与代码有关 |
| --- | --- | --- |
| `plan/` | 计划。目标、范围、需求规格、验收标准、实施顺序、风险 | 否，只描述行为与验收 |
| `tasks/` | 子任务看板与实现状态。每完成一个子任务更新一次 | 否 |
| `design/` | 设计取舍（ADR）。选项、决定性理由、结论、被否决的方案 | 是，记录"怎么做/为什么这样做" |

计划与代码无关，是为了让计划在实现细节变化时保持有效；设计取舍必须写清
"为什么不选另一条路"，否则后续维护者会重复讨论已经付出代价的决定。

## 状态约定

| 记号 | 含义 |
| --- | --- |
| `TODO` | 尚未开始 |
| `DOING` | 当前正在做（同一时刻只允许一个主任务处于 DOING） |
| `BLOCKED` | 因外部条件无法继续，必须写明条件与已尝试手段 |
| `DONE` | 代码、测试、文档、提交全部完成 |

每个子任务完成的判定见 `plan/00-overview.md` 的完成定义（DoD）。

## 更新纪律

1. 开始一项任务前，计划文件中的该阶段必须已存在且可执行。
2. 每完成一个子任务：`git commit` 一次，然后更新 `tasks/00-board.md` 的状态，
   必要时补充该子任务的 `design/` 结论。
3. 设计过程中做出的取舍，必须在对应 `design/` 文件里新增一节，而不是只写在
   提交信息里。
4. 遇到阻塞：在 board 中标记 `BLOCKED` 并写明阻塞条件，然后继续其他可做项，
   不允许静默跳过。
5. 遇到 BUG：按 `plan/00-overview.md` 的 BUG 流程处理（独立分支、修复、清理
   无效代码、合并），并在 board 中留一条记录。

## 文档清单

| 文件 | 内容 | 状态 |
| --- | --- | --- |
| `plan/00-overview.md` | 总体计划：约束、阶段、依赖、完成定义、风险、工作方式 | DONE |
| `plan/01-io-model.md` | 任务 1：非轮询 I/O 模型 | DONE |
| `plan/02-console-window.md` | 任务 2：GetConsoleWindow 伪窗口 | DONE |
| `plan/03-defterm-tests.md` | 任务 3：defterm 自动化测试 | DONE |
| `plan/04-conpty-tests.md` | 任务 4：conpty 自动化测试 | DONE |
| `tasks/00-board.md` | 子任务看板与实现状态 | DONE |
| `tasks/01-a1-baseline.md` | A1 基线调查：轮询点、状态归属、回归与测量方法 | DONE |
| `tasks/03-a9-error-audit.md` | 错误分类审计（新建路径） | DONE |
| `tasks/04-open-gaps.md` | 未完成项与阻塞原因（G1–G10） | DONE |
| `design/0001-io-model.md` | I/O 模型：线程、状态归属、退出协议 | DONE |
| `design/0002-console-window.md` | 伪窗口：窗口类型、消息策略、生命周期 | DONE |
| `design/0003-conhost-test-library.md` | 测试库：框架、隔离、驱动方式 | DONE |
| `design/0004-input-thread-split.md` | 输入侧拆分：状态先搬、引用别名、分步实施 | DONE |
| `design/0005-fork-reference-policy.md` | fork 只作参考：借鉴项必须有依据或有本仓库用例 | DONE |

新增设计取舍时按 `NNNN-kebab-case.md` 递增编号，并在上表登记。
