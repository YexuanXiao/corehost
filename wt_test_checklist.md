# corehost2 as OpenConsole.exe — Windows Terminal 验收清单

测试目标：`corehost.exe`
对照目标：`C:\1\OpenConsole.exe` (原版)
宿主：`C:\1\terminal-1.24.12741.0\WindowsTerminal.exe`
部署方式：复制目标为 `C:\1\terminal-1.24.12741.0\openconsole.exe`，然后启动宿主
目标：使 corehost 和 openconsole 行为一致（除非明显是 openconsole 的 bug），必须修复根因

状态：`[ ]` 通过，`[!]` 失败/待修，`[-]` 未测或环境阻塞。

## A. 启动与会话

- [ ] A1 powershell.exe/pwsh.exe/cmd.exe 启动、提示符、标题
- [ ] A4 分屏/多窗格（WT CLI split-pane）
- [ ] A5 关闭标签页、关闭窗口
- [ ] A6 多标签切换与独立状态
- [ ] A7 启动目录/命令行参数（`-d C:\1`）

## B. VT 序列与渲染

- [ ] B1 SGR 16 色前景/背景
- [ ] B2 SGR 256 色
- [ ] B3 SGR RGB 真彩色
- [ ] B4 粗体/斜体/下划线/反显/删除线
- [ ] B5 SGR reset
- [ ] B6 CJK 宽字符
- [ ] B7 emoji / surrogate pair
- [ ] B8 combining mark / grapheme cluster
- [ ] B9 宽字符覆盖与尾部清理
- [ ] B10 DEC line drawing / charset
- [ ] B11 tab、backspace、CR/LF
- [ ] B12 CUP/CUU/CUD/CUF/CUB/CHA/VPA
- [ ] B13 save/restore cursor（ESC 7 / ESC 8）
- [ ] B14 EL/ED 各模式
- [ ] B15 scroll region、LF/IND/RI
- [ ] B16 alternate screen 1049 进入/退出
- [ ] B17 OSC 0/2 窗口标题
- [ ] B18 OSC 8 hyperlink（序列被接受；点击行为未自动验证）
- [-] B20 focus in/out 报告：`?1004` 已转发；原版 conhost 对照同样未观察到 FOCUS 事件，非 corehost 回归
- [-] B21 mouse tracking 模式
- [ ] B22 窗口 resize 后重绘

## C. Win32 控制台输入

- [ ] C1 可打印字符、Unicode 粘贴
- [ ] C2 方向键/Home/End/Insert/Delete/PgUp/PgDn
- [ ] C3 Ctrl/Alt/Shift 组合
- [ ] C4 F1–F12（测试 F1）
- [ ] C5 Tab / Shift+Tab
- [ ] C6 Ctrl+C
- [ ] C7 Ctrl+Z（报告为 KEY=Z CHAR=26 MOD=Control）
- [-] C8 多行粘贴
- [ ] C9 鼠标点击/拖动/滚轮（MOUSE x=60 y=14 buttons=0x1 / 0x0 / wheel flags=0x4）
- [-] C10 滚轮回滚/回到底部：原版 conhost 对照同样无 scrollback，非 corehost 回归
- [ ] C11 控制台窗口大小变化事件（WINDOW EVENT 79x20 / 120x30）
- [ ] C12 ReadConsole 行编辑：方向键、Home/End、历史输入

## D. 控制台行为

- [-] D1 滚上/滚下与 scrollback：原版 conhost 对照同样无历史，非 corehost 回归
- [ ] D2 cls/clear 后内容与光标
- [ ] D3 resize 保留内容、重新换行
- [ ] D4 自动换行与行尾标志（200 字符长行）
- [ ] D5 alternate screen 退出后主屏恢复
- [ ] D6 多标签同时输出
- [ ] D7 shell 嵌套 shell
- [ ] D8 进程退出、关闭标签页、退出码
- [ ] D9 ReadConsoleOutput / ReadConsoleOutputCharacter 往返（READOUT / HELLO-STRING）

## E. 第三方程序

- [ ] E1 cmd.exe
- [ ] E2 pwsh.exe
- [ ] E3 edit.exe
- [ ] E4 vim.exe
- [ ] E7 PowerShell / cmd 嵌套 shell
- [ ] E8 长输出程序（100 行输出）
- [ ] E9 python.exe

## F. 压力与回归

- [ ] F1 大量文本快速输出
- [ ] F2 连续快速 resize
- [ ] F3 超长行/宽字符混合
- [ ] F4 alternate-screen 应用反复进出
- [ ] F5 深层嵌套 shell + 退出
- [ ] F6 关闭窗口时无崩溃/无残留进程

## 证据

截图目录：`C:\1\wt_shots\`，包含启动、SGR、Unicode、光标/保存恢复、erase、scroll region、alt screen、标题、hyperlink、keytest、resize、长行、edit、vim、sshd 等截图。
