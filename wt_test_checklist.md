# corehost2 as OpenConsole.exe — Windows Terminal 验收清单

测试目标：`C:\1\terminal-1.24.12741.0\OpenConsole.exe`（由 `build\Release\corehost2.exe` 部署）
宿主：`C:\1\terminal-1.24.12741.0\WindowsTerminal.exe`
测试日期：2026-11-16

状态：`[x]` 通过，`[!]` 失败/待修，`[-]` 未测或环境阻塞。

## A. 启动与会话

- [x] A1 PowerShell 5.1 启动、提示符、标题
- [x] A2 新建 cmd 标签页
- [x] A3 新建 pwsh 7 标签页
- [x] A4 分屏/多窗格（WT CLI split-pane）
- [x] A5 关闭标签页、关闭窗口（多次重启）
- [x] A6 多标签切换与独立状态
- [x] A7 启动目录/命令行参数（`-d C:\1`）

## B. VT 序列与渲染

- [x] B1 SGR 16 色前景/背景
- [x] B2 SGR 256 色
- [x] B3 SGR RGB 真彩色
- [x] B4 粗体/斜体/下划线/反显/删除线
- [x] B5 SGR reset
- [x] B6 CJK 宽字符
- [x] B7 emoji / surrogate pair
- [x] B8 combining mark / grapheme cluster
- [x] B9 宽字符覆盖与尾部清理
- [x] B10 DEC line drawing / charset
- [x] B11 tab、backspace、CR/LF
- [x] B12 CUP/CUU/CUD/CUF/CUB/CHA/VPA
- [x] B13 save/restore cursor（ESC 7 / ESC 8）
- [x] B14 EL/ED 各模式
- [x] B15 scroll region、LF/IND/RI
- [x] B16 alternate screen 1049 进入/退出
- [x] B17 OSC 0/2 窗口标题
- [x] B18 OSC 8 hyperlink（序列被接受；点击行为未自动验证）
- [-] B19 bracketed paste：原版 conhost 对照同样拆分，非 corehost 回归；PowerShell/WT 行为待确认
- [-] B20 focus in/out 报告：`?1004` 已转发；原版 conhost 对照同样未观察到 FOCUS 事件，非 corehost 回归
- [-] B21 mouse tracking 模式：自动注入未可靠触发回滚/鼠标事件
- [x] B22 窗口 resize 后重绘

## C. Win32 控制台输入

- [x] C1 可打印字符、Unicode 粘贴
- [x] C2 方向键/Home/End/Insert/Delete/PgUp/PgDn
- [x] C3 Ctrl/Alt/Shift 组合
- [x] C4 F1–F12（测试 F1）
- [x] C5 Tab / Shift+Tab
- [x] C6 Ctrl+C
- [x] C7 Ctrl+Z（报告为 KEY=Z CHAR=26 MOD=Control）
- [-] C8 多行粘贴（与 B19 同现象；原版 conhost 对照同样拆分）
- [x] C9 鼠标点击/拖动/滚轮（MOUSE x=60 y=14 buttons=0x1 / 0x0 / wheel flags=0x4）
- [-] C10 滚轮回滚/回到底部：原版 conhost 对照同样无 scrollback，非 corehost 回归
- [x] C11 控制台窗口大小变化事件（WINDOW EVENT 79x20 / 120x30）
- [x] C12 ReadConsole 行编辑：方向键、Home/End、历史输入

## D. 控制台行为

- [-] D1 滚上/滚下与 scrollback：原版 conhost 对照同样无历史，非 corehost 回归
- [x] D2 cls/clear 后内容与光标
- [x] D3 resize 保留内容、重新换行（已修复底端锚定）
- [x] D4 自动换行与行尾标志（200 字符长行）
- [x] D5 alternate screen 退出后主屏恢复
- [x] D6 多标签同时输出
- [x] D7 shell 嵌套 shell
- [x] D8 进程退出、关闭标签页、退出码
- [x] D9 ReadConsoleOutput / ReadConsoleOutputCharacter 往返（READOUT / HELLO-STRING）

## E. 第三方程序

- [x] E1 cmd.exe
- [x] E2 pwsh.exe
- [x] E3 edit.exe（全屏 UI、状态栏、菜单）
- [x] E4 vim.exe（修复 Console 字符串 API 后全屏渲染正常）
- [-] E5 ssh 客户端连接本机：本机 sshd 无法完成账户/主机密钥初始化
- [-] E6 sshd.exe -d 调试服务器：`get_passwd lookup_sid failed 1332`、host key 加载失败
- [x] E7 PowerShell / cmd 嵌套 shell
- [x] E8 长输出程序（100 行输出；CRT raw write 也已修复）

## F. 压力与回归

- [x] F1 大量文本快速输出
- [x] F2 连续快速 resize
- [x] F3 超长行/宽字符混合
- [x] F4 alternate-screen 应用反复进出
- [x] F5 深层嵌套 shell + 退出
- [x] F6 关闭窗口时无崩溃/无残留进程

## 本轮发现并修复的 bug

1. **SGR 属性丢失**：bold/italic/underline/negative/strikethrough 没有进入 `text_style` 或 serializer，仅颜色生效。已增加 `text_style_attribute` 并输出对应 SGR。
2. **scroll region 内 LF/IND 不滚动**：`apply_line_feed` / `apply_index` 只在实际屏幕最后一行滚动，忽略 DECSTBM 底边。已改为在 scroll_bottom 处滚动区域。
3. **resize 保留顶部、丢失底部**：缩小窗口后看不到最新输出；且 VT resize 未更新 viewport。已增加 bottom-anchor resize、cursor 调整和 viewport 同步。
4. **WriteConsoleOutputCharacter/Attribute 只写 1 个字符**：协议中 `NumRecords` 是 OUT 字段，旧代码把它当输入 count；已改为按输入载荷长度计算。
5. **ReadConsoleOutputString 完成载荷偏移/容量错误**：读回为空；已按 header+struct+数据计算容量和完成 Write 指针。
6. **Vim 全屏空白**：根因是上述字符串 API；修复后 Vim 正常显示波浪线和状态栏。

## 证据

截图目录：`C:\1\wt_shots\`，包含启动、SGR、Unicode、光标/保存恢复、erase、scroll region、alt screen、标题、hyperlink、keytest、resize、长行、edit、vim、sshd 等截图。
