# WT Display / Non-IME Check

对照：`wt.exe` + Windows PowerShell 5.1
目标：`corehostw.exe` + `powershell.exe`
清单：`wt_test_checklist.md`

## 最新状态

`CONSOLE_IO_RAW_WRITE` 已由窗口层统一解析，`ConsolepWriteConsole` 也走同一套 VT parser/applier。PSReadLine 输入回显、有效/无效命令、256/RGB 色、underline/strikethrough/bold、OSC 标题均已验证。

之前 `ưm` 前缀已确认是 PSReadLine 在历史文件错误时自己 `Console.Write("ưm")` 的结果；使用 `Set-PSReadLineOption -HistorySaveStyle SaveNothing` 后前缀消失，不是终端 parser 的 bug。

## 已验证

| 项 | 结果 | 说明 |
|---|---|---|
| A1 启动/提示符/标题 | 通过 | `corehostw.exe powershell.exe` 正常启动，提示符显示 |
| 有效命令 | 通过 | `Get-ChildItem C:\` 输出目录表，布局与 WT 一致 |
| 无效命令 | 部分通过 | `notacommand` 产生红色错误，长行自动折行；命令回显缺失 |
| B1 16 色 | 通过 | PowerShell 错误输出显示红色前景 |
| B22 resize 重绘 | 通过 | 窗口 resize 后 grid/内容重绘，无崩溃 |
| C2 方向键编辑 | 通过 | 输入 `abc`，Left，`X`，回车，执行 `abXc` |
| D4 自动换行 | 通过 | 长错误行在窗口右边缘折行 |

## 已定位并修复的根因

`window_console_session` 原来直接丢弃 `CONSOLE_IO_RAW_WRITE`。  
PowerShell/PSReadLine 通过 raw write 输出 ANSI 渲染输入行，因此字符和 cursor 控制被丢弃。

修复：

- `CONSOLE_IO_RAW_WRITE` 按 `state.output_code_page` 解码为 UTF-16；
- 交给独立的 `vtparser`/`vtstate` applier；
- `console_api::write_console_text` 切分 plain text 与 ESC 序列；
- 跨 WriteConsole 调用的未完成 ESC 序列保留在 pending buffer。

修复后 PSReadLine 输入已可见。

## 仍存在的显示问题

输入行最左侧仍有一个异常前缀字符（raw payload 中存在 `C6 B0 6D` 等非预期字节），需要继续定位 PSReadLine 该 raw write 的语义。

### PSReadLine 输入不回显（已修复）

现象：

- PowerShell 加载 PSReadLine 时，键盘输入的命令不显示在提示符后；
- 命令仍被接收并执行；
- 错误输出、结果输出正常。

对照：

- `Remove-Module PSReadLine` 后，由 console host 的 `ReadConsole` echo 路径处理，输入 `echo hi` 正常显示；
- WT 中 PSReadLine 输入正常回显。

结论：

- 不是键盘事件未送达；
- PSReadLine 的渲染路径依赖 VT/console 输出能力，而当前 `console_api::write_console` 对写入文本只做逐行文本写入，不解析 VT 序列；
- 需要为 `WriteConsole` 接入 VT state parser，或专门兼容 PSReadLine 的渲染方式。

### VT 输出未解析

当前 `write_console_text` 不解析 ESC 序列，因此：

- B2 256 色；
- B3 RGB 真彩色；
- B17 OSC 0/2 标题；
- PSReadLine 的光标/行渲染；

通过普通 `WriteConsole` 的 VT 序列都不可靠。  
`console_theme` 的 256/RGB 颜色解析和 `render_worker` 的 title 更新代码已存在，但缺少 VT 输出入口，无法端到端验证。

曾尝试把 `vtstate` 直接接入 `console_api::write_console_text`；clean rebuild 后普通 PowerShell 提示符可显示，混合 `AB ESC[31m CD` 的 `write_console_text` 单元测试也通过，但 `powershell -File` 输出 VT 脚本时窗口仍为空白，未定位。该尝试已整体回退，避免把不完整重构留在工作区。

已确认：
- 增量构建后曾出现空白，clean rebuild 后恢复为提示符，说明跨层移动 `console_state` 时存在 stale object/ABI 问题，不能只做增量验证；
- 直接 parser 路径不是唯一问题，仍需检查 PowerShell 的 `WriteConsoleOutput`/脚本启动路径。

补充控制实验（临时 `test_console_client`，未提交）：

- `WriteConsoleW` 普通文本：正常显示；
- `WriteConsoleW` 带 ESC 序列：baseline 下按字面显示 `[31m...`；
- `WriteConsoleOutputW` 的 CHAR_INFO：正常显示绿色 `OUTPUT`；
- 因此 WriteConsole/WriteConsoleOutput 的 plain 路径本身可用，PSReadLine 不回显不是这两个最基础路径失效。

正确做法需要把普通文本与 ESC 序列切分、确保 `continue_text` 文本被完整 flush，并 clean rebuild 后验证。

### 其他

- C3/C4/C5/C6/C7 组合键尚未逐项验证；
- C9 鼠标事件未实现；
- C11 window size event 未验证；
- D1 scrollback 未实现；
- D8 client 退出默认 `keep_open`，与 WT 关闭标签页语义不同。

## 结论

显示基础（文本、颜色、wrap、resize、错误输出）已可用；  
**阻塞性问题是 PSReadLine/VT 输出路径**。下一步优先把 VT parser 接入 `WriteConsole`，用完整文本回退保证非 VT 输出不丢失。
