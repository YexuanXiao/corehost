# 设计取舍 0005：fork 只作参考，不作为正确性来源

状态：生效

## 1. 背景

仓库旁边有一个独立 fork（`corehost_another-fork/`，不进本仓库）。它的历史里有不少
修复说明，读起来很对路：例如"输入类 API 在输出句柄上应失败"、"VT 输入程序应收到
原始按键序列"、"挂起读期间仍要服务其它请求"。借鉴它的**问题分析**能省很多时间。

但 fork 不是规格，也不是 conhost 本身：它可能过期、可能只对作者的环境成立、可能
用错误的方式绕过问题。把 fork 的补丁当成"正确答案"会引入两类风险：

1. **错误的行为被当成正确行为**：测试只验证"和 fork 一样"，而不是"和 conhost 一样"；
2. **无法解释的改动**：代码里出现一句"fork 这么做"，但没人知道真正原因，后续重构
   会把它当噪声删掉，问题复发。

## 2. 规则

1. fork 的提交只用于**定位问题与提出假设**，不作为验收标准。
2. 每个从 fork 借鉴的行为，必须至少满足其一才能合入：
   - 在 `terminal/`（conhost 源码）里找到对应的真实行为/注释作为依据；
   - 有本仓库自己的自动化用例证明该行为（并且用例断言的是可观察契约，不是实现细节）；
   - 无法验证时，记录到 `tasks/04-open-gaps.md`，代码保持现状或标注未验证。
3. 提交信息与注释写"为什么"（conhost 语义 / 客户端依赖），不写"fork 也这么做"。

## 3. 已借鉴项与验证状态

| fork 提交 | 借鉴内容 | 验证 | 状态 |
| --- | --- | --- | --- |
| `42ef837` | CONNECT 的 `Process` 令牌用客户端 pid | `tests/test_console_state.cpp` 断言 joined CONNECT 的 Process = pid | 已验证（契约） |
| `42ef837` | 输入类 API 在输出句柄上返回 `STATUS_INVALID_HANDLE` | 单测 `test_input_apis_reject_output_handle` | 已验证（契约） |
| `5104668` | 挂起读期间继续服务其它 ConDrv 请求 | `serve` 用例（ReadConsole 挂起时查询光标立即返回） | 已验证（端到端） |
| `9742404` | `WriteConsoleInput` 唤醒挂起读；服务中的读只完成一次 | `wake` 用例；去重完成路径只有代码审查，无专门用例 | 部分验证（去重未测） |
| `319ed56` | VT 输入程序收到原始按键序列 | `vt` 用例（Up + Backspace = `1b 5b 41 7f`） | 已验证（端到端） |
| `42ef837` | `ConsoleControl(ConsoleEndTask)` 投递 Ctrl+C | `ctrl_c` 用例仍 SKIP（探针不退出） | 未验证，G11 |

## 4. 对 G11 的影响

fork 的 `send_ctrl_event` 与我们的实现参数一致，但 G11 的现象（调用返回成功、客户端
不结束）说明**"fork 这么做"不足以推断行为正确**。下一步按规则 2 处理：在 `terminal/`
里找 conhost 在 ConPTY 模式下投递 Ctrl+C 的真实路径（`ConsoleControl` 的调用点、
`ConsoleEndTask` 需要的 console 关联），找不到可验证依据就记录为平台限制，不再盲试。
