# corehost 验收清单

测试目标：`corehostw.exe`

测试方式：每项测试启动一次窗口进行测试，必须在同一次打开的窗口内测多次功能（不是每项重新创建窗口）；测试穿插和 cmd/pwsh/edit 的交互，确保初始状态和结束状态正常。

review 目标：修复根因，不降低性能。

测试时禁用 IME，避免干扰；不测试无法截图的东西（光标闪烁、滚动动画等），这类功能测试最终静止状态。

状态：`[ ]` 通过，`[!]` 失败/待修，`[-]` 未测或环境阻塞。

## 配置覆盖清单

本表把当前 `corehost_config` 中的功能映射到验收项；没有对应验收项的功能不能视为已验收。

| 配置 | 验收区域 | 状态 |
|---|---|---|
| `console.text_measurement` console/graphemes/wcswidth | B6/B8 | `[-]` 只测默认 graphemes |
| `console.initial_cols/rows` | A1/A7 | `[-]` |
| `window.scrollback_lines` | D1 | `[-]` 环境限制 |
| `window.copy_ending` | C1/C8 | `[-]` |
| `font.family_fallback_list` + system fallback | A2/B6/B7 | `[-]` 默认 Cascadia Mono + 系统 fallback 已使用 |
| `font.size` | A2/B22 | `[-]` |
| `font.variation*` normal/bold/italic/bold-italic | B4 | `[-]` 默认轴，未切换轴值 |
| `font.ligatures` | B4 | `[-]` 默认关闭 |
| `font.color_emoji` | B7 | `[-]` 默认开启，非 BMP 未测 |
| `font.antialias` / `turning` / `rendering_mode` | B1-B5 | `[-]` 默认 grayscale |
| `color.mode` dark/light/auto | B1-B5 | `[-]` 默认 dark |
| `color.*` foreground/background/selection/cursor/hyperlink/scrollbar | B1-B5/C9 | `[-]` |
| `cursor.shape` | A4/C2 | `[-]` 默认 automatic |
| `cursor.blink/blink_rate` | A4 | `[-]` 测试时关闭闪烁 |
| `scrollbar.visibility` auto_hide/always | B22/C10 | `[-]` |
| `window.shadow/opacity/blur` | A3 | `[-]` 手动验收 |
| `window.always_on_top/launch/fullscreen` | A8 | `[-]` |
| `window.inherit_working_directory` | A7 | `[-]` 默认 USERPROFILE |
| `window.allow_move_resize` | A8 | `[-]` |
| `title.force_corehost` | B17 | `[-]` 默认关闭 |
| `mouse.scroll_multiplier` | C9/C10 | `[-]` |
| `mouse.right_click` | C9 | `[-]` |
| `mouse.middle_click_paste` | C9/C8 | `[-]` 默认关闭 |
| `mouse.shift_capture` | B21/C9 | `[-]` |
| `mouse.selection_mode` line/block/disable | C9/D1 | `[-]` |
| `mouse.selection_copy_on_select` | C1/C9 | `[-]` 默认关闭 |
| `mouse.selection_trim_block` | C1/C9 | `[-]` 默认关闭 |
| `mouse.selection_double_click/triple_click` | C9 | `[-]` |
| `clipboard.read/write_policy` | C1/C8 | `[ ]` 默认 allow |
| `clipboard.paste_max_size` | C8 | `[-]` |
| `clipboard.bracketed_paste` | C8 | `[-]` 未接入 |
| `uia_enable` / UIA events | G1 | `[ ]` `Window.UIA` |
| `ime.tsf_enabled` / `candidate_offset_cells` | G2 | `[-]` |
| `bell.mode` visual/auditory/window/taskbar | G3 | `[-]` |
| `graphics.sixel` | G4 | `[-]` |
| `accessibility.reduce_motion` | A4/C10 | `[-]` 默认关闭 |
| `accessibility.high_contrast` | B1-B5 | `[-]` schema-only |
| `window.dpi` / resize | B22 | `[-]` |

## A. 启动与会话

- [ ] A1 powershell.exe/pwsh.exe/cmd.exe 启动、提示符、标题
  - 已测 PowerShell 启动和标题；未逐一测 pwsh/cmd。
- [ ] A2 默认字体、fallback、字号显示
  - 已用默认 Cascadia Mono 截图；系统 fallback 的 CJK 见 B6。
- [ ] A3 窗口外观：shadow / opacity / blur
  - 需手动验收；默认 none/255。
- [ ] A4 光标形态/闪烁关闭状态
  - 测试使用不闪烁状态；cursor shape 默认 automatic 未切换。
- [ ] A5 关闭窗口
  - WM_CLOSE 后 corehostw 退出，无残留进程。
- [ ] A6 进程退出、退出码
  - `cmd /c exit 7` 后 corehostw 返回 0；按决定 GUI 宿主保持 0。
- [ ] A7 启动目录/命令行参数
- [ ] A8 topmost / maximized / fullscreen / allow move

## B. VT 序列与渲染

- [ ] B1 SGR 16 色前景/背景
  - 红、亮蓝、绿背景已截图通过。
- [ ] B2 SGR 256 色
  - 256-RED / 256-GREEN 已截图通过。
- [ ] B3 SGR RGB 真彩色
  - RGB-ORANGE / RGB-BLUE-BG 已截图通过。
- [ ] B4 粗体/斜体/下划线/反显/删除线
  - 五种样式已截图通过。
- [ ] B5 SGR reset
  - SGR-RESET-OK 后颜色恢复。
- [ ] B6 CJK 宽字符
  - `CJK-中文-测试-END` 截图通过。
- [-] B7 emoji / surrogate pair
  - PowerShell 不保证代理对；按用户决定暂不测非 BMP。
- [ ] B8 combining mark / grapheme cluster
  - `e` + U+0301 通过 Direct `WriteConsoleW` 复现并修复。
  - 根因：所有 cluster 都走 color atlas；monochrome cluster 被 color atlas 错位。
  - 修复：color atlas 只保留包含 color glyph 的文本；monochrome cluster 不再分配/绘制 color atlas，回退普通 glyph atlas。
  - 证据：`C:\1\wt_combining_fixed.png`。
- [-] B9 宽字符覆盖与尾部清理
- [ ] B10 DEC line drawing / charset
  - `ESC(0 lqk ESC(B` 显示为线框并恢复。
- [ ] B11 tab、backspace、CR/LF
- [ ] B12 CUP/CUU/CUD/CUF/CUB/CHA/VPA
  - CUP/CUU/CUD/CUF/CUB 已做同一窗口截图，未逐项断言坐标。
- [ ] B13 save/restore cursor（ESC 7 / ESC 8）
  - `SAVE-...RESTORED` 截图通过。
- [ ] B14 EL/ED 各模式
  - EL2 局部测试通过；其余模式未测。
- [-] B15 scroll region、LF/IND/RI
- [ ] B16 alternate screen 1049 进入/退出
  - MAIN-BEFORE/MAIN-AFTER 恢复，ALT 内容未残留。
- [ ] B17 OSC 0/2 窗口标题
  - `TITLE-OSC` 通过；`[Console]::Write` 不触发，PowerShell 使用 `Write-Host` 时触发。
- [ ] B18 OSC 8 hyperlink
  - 无 `id=` 的 OSC8 曾导致访问冲突：parser 未清空旧 `id` view；已修复并加 parser 测试。
  - 当前可打开/关闭 hyperlink，未验证点击行为。
- [-] B20 focus in/out 报告：`?1004` 已转发；原版 conhost 对照未观察到 FOCUS 事件，非 corehost 回归。
- [-] B21 mouse tracking 模式
- [ ] B22 窗口 resize 后重绘
  - 同一窗口 80 行输出后放大窗口，UIA 文本长度和 RESIZE-READY 保持；
  - 未保留截图，仅记录 UIA 前后长度一致。

## C. Win32 控制台输入

- [ ] C1 可打印字符、Unicode 粘贴
  - 复制选中 `COPY-TARGET-中文-END` 成功写入剪贴板；
  - cmd 的 `set /p` 可粘贴 `PASTE-中文-LINE`；
  - 默认 clipboard read/write 已改为 allow。
- [ ] C2 方向键/Home/End/Insert/Delete/PgUp/PgDn
  - 通过 `ReadConsoleInputW` 收到 VK 0x25-0x28、0x24、0x23、0x2D、0x2E、0x21、0x22。
- [ ] C3 Ctrl/Alt/Shift 组合
  - Shift+Tab control state=0x10；Ctrl+Z control=0x8。
- [ ] C4 F1–F12（测试 F1）
  - F1 VK=0x70 收到。
- [ ] C5 Tab / Shift+Tab
  - Tab 和 Shift+Tab 均收到 VK=0x09。
- [ ] C6 Ctrl+C
  - `ping -t` 被中断，interactive PowerShell 显示 `^C` 后回到提示符；
  - `ConsoleEndTask` 必须携带窗口 HWND，否则静默成功但不生效；
  - 按 `console_api` 的 process registry 枚举附着进程投递给 CTRL_C_EVENT；
  - cooked `ReadHost` / cmd `set /p` pending 通过 read termination 返回 STATUS_ALERTED；
  - raw `[Console]::ReadLine()` 按 conhost 语义 Ctrl+C 不终止；
  - Ctrl+Break 在 interactive PowerShell 显示 `^C`；ping 本身不支持 Ctrl+Break，不能作为失败依据。
  - cooked `Read-Host` 阻塞时 Ctrl+C 能完成 pending read 并退出；
  - raw `[Console]::ReadLine()` 按 conhost 语义 Ctrl+C 不终止，不作为失败。
- [ ] C7 Ctrl+Z（报告为 KEY=Z CHAR=26 MOD=Control）
  - 收到 VK=0x5A CHAR=0x1A control=0x8。
- [-] C8 多行粘贴
- [ ] C9 鼠标点击/拖动/滚轮
  - `ReadConsoleInputW` 收到左键 down/up、右键、中键；
  - 滚轮在普通屏幕未转发（代码要求 alternate + VT tracking），未判定；
  - 证据：`C:\1\wt_mouse_result.txt`。
- [-] C10 滚轮回滚/回到底部：原版 conhost 对照同样无 scrollback，非 corehost 回归
- [-] C11 控制台窗口大小变化事件
  - 窗口 resize 后 console log 显示 120x30 -> 132x34；
  - PowerShell ReadConsoleInput 未观察到 WINDOW_BUFFER_SIZE_EVENT，测试客户端限制，暂不判定。
- [ ] C12 ReadConsole 行编辑

## D. 控制台行为

- [-] D1 滚上/滚下与 scrollback：原版 conhost 对照同样无历史，非 corehost 回归
- [ ] D2 cls/clear 后内容与光标
  - `Clear-Host` 后旧内容不在 UIA 文本，CLEAR-READY/F1 输出正常。
- [ ] D3 resize 保留内容、重新换行
- [ ] D4 自动换行与行尾标志
  - 200 字符长行已截图折行，未验证行尾标志。
- [ ] D5 alternate screen 退出后主屏恢复
  - 见 B16。
- [-] D6 多标签同时输出：单窗口模式不适用
- [ ] D7 shell 嵌套 shell
  - PowerShell 内启动 cmd.exe，NESTED-CMD-OUTPUT 和 NESTED-AFTER 正常。
  - 曾因 `\\\\?\\` 前缀导致 cmd.exe UNC 警告；短路径不再加前缀，已修复。
- [ ] D8 进程退出、关闭标签页、退出码
  - 客户端退出码不传播，corehostw 固定返回 0。
- [ ] D9 ReadConsoleOutput / ReadConsoleOutputCharacter 往返

## E. 第三方程序

- [ ] E1 cmd.exe
  - 见 D7 嵌套 cmd 测试。
- [ ] E2 pwsh.exe
  - 启动 PowerShell 7.7.0，提示符正常。
- [ ] E3 edit.exe
- [ ] E4 vim.exe
  - vim 启动并正确显示文件内容和 `~` 行；退出流程未判定。
- [ ] E7 PowerShell / cmd 嵌套 shell
- [ ] E8 长输出程序（100 行输出）
- [ ] E9 python.exe
  - Python 3.15.0 REPL 启动正常。

## F. 压力与回归

- [ ] F1 大量文本快速输出
  - 200 行连续输出后 F1-READY 正常。
- [ ] F2 连续快速 resize
  - 20 次放大/缩小后进程存活，F2-READY 保持。
- [ ] F3 超长行/宽字符混合
- [ ] F4 alternate-screen 应用反复进出
  - 20 次进入/退出 alternate screen，主屏 F4-MAIN-READY 恢复。
- [ ] F5 深层嵌套 shell + 退出
- [ ] F6 关闭窗口时无崩溃/无残留进程
  - WM_CLOSE 后无 corehostw 残留。

## G. 配置功能与自动化

- [ ] G1 UIA：Document / TextPattern / TextRange / ScrollPattern / 文本内容
  - `Window.UIA` 自动通过。
  - 屏幕阅读器实测仍未做。
- [-] G2 TSF：composition / candidate / 焦点切换
  - 需手动验收；当前未接 `ITextStoreACP`。
- [-] G3 Bell：visual/auditory/window/taskbar
  - 视觉/声音需手动验收。
- [-] G4 Sixel：解码、颜色、屏幕内缓存、滚出释放
- [-] G5 config：dark/light 色板、font variation、ligatures、cursor shape
  - 需专门切换配置后截图。
- [-] G6 config：clipboard deny/allow、paste limit、right/middle click
  - 默认 deny；需允许后手动验收。
- [-] G7 DPI / resize / Mica / opacity
  - 需手动验收。

## PowerShell 测试限制

Windows PowerShell 的 console 输入/输出实现不保证代理对（surrogate pair）完整传递。所有涉及非 BMP/emoji 的 Unicode 测试不得使用 PowerShell 作为客户端；使用 PowerShell 的测试只验证 BMP 字符。需要测试非 BMP 时使用专门的原生测试客户端或其他 shell。

## 证据

截图目录：`C:\1\`

- `wt_visual2.png`：SGR 16/256/RGB、粗斜体/下划线/反显/删除线、CJK、宽字符、CUP、save/restore、EL2、DEC line drawing、长行。
- `wt_combining_direct.png`：修复前 combining 错位。
- `wt_combining_fixed.png`：修复后 `e` + U+0301 同一行。
- `wt_alt.png`：alternate screen 退出后主屏恢复。
- `wt_mouse_result.txt`：鼠标按键事件。
- `wt_keys_result.txt`：键盘事件。
- `wt_b9_b15.png`：B9/B15/B18。
