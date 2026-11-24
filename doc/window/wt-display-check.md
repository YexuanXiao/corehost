# WT Display / Non-IME Check

对照：`wt.exe` + Windows PowerShell 5.1
目标：`corehostw.exe` + `powershell.exe`
清单：`wt_test_checklist.md`

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

## 失败/阻塞

### PSReadLine 输入不回显

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

曾尝试把 `vtstate` 直接接入 `console_api::write_console_text`，但 mixed text/ESC 输出导致 PowerShell 初始屏幕变成空白；该尝试已回退，避免回归。正确做法需要把普通文本与 ESC 序列切分，或确保 parser 的 `continue_text` 文本被完整 flush。

### 其他

- C3/C4/C5/C6/C7 组合键尚未逐项验证；
- C9 鼠标事件未实现；
- C11 window size event 未验证；
- D1 scrollback 未实现；
- D8 client 退出默认 `keep_open`，与 WT 关闭标签页语义不同。

## 结论

显示基础（文本、颜色、wrap、resize、错误输出）已可用；  
**阻塞性问题是 PSReadLine/VT 输出路径**。下一步优先把 VT parser 接入 `WriteConsole`，用完整文本回退保证非 VT 输出不丢失。
