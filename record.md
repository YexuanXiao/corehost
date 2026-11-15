# corehost2 真实 Windows Terminal 测试记录

目标程序：`build/Release/corehost2.exe`
测试宿主：`C:\1\terminal-1.24.12741.0\WindowsTerminal.exe`
被替换宿主：`C:\1\terminal-1.24.12741.0\OpenConsole.exe`
原始备份：`C:\1\terminal-1.24.12741.0\OpenConsole.exe.orig`
对照宿主：`C:\1\OpenConsole.exe`（原始 OpenConsole）

## 构建

```text
corehost.exe   -> libcorehost::conpty_entry
corehost2.exe  -> libcorehost2::conpty_entry
```

两个目标可同时构建：

```powershell
cmake --build build --target corehost corehost2 --config Release
```

## 已通过

| 项目 | 操作 | 结果 | 截图 |
|---|---|---|---|
| 启动 | WT 默认 PowerShell | 提示符正常 | build/ps5.png |
| 输入 | `echo hello-wt` | 命令执行、输出正确 | build/ps5.png |
| 换行 | PowerShell 多行输出 | 输出从列 0 开始 | build/ps5.png |
| cmd 输入 | `ECHO ABC-DEF` | 大小写、连字符保留 | build/cmd_upper.png |
| cmd 输出 | `echo cmd-ok`、`ver` | 输出正确 | build/cmd_resize.png |
| 调整大小 | 1000x600 -> 820x500 后执行命令 | 新窗口尺寸生效 | build/cmd_resize.png |
| 控制台尺寸 | `mode con` | 返回 Lines/Columns | build/cmd_resize.png |
| 差分测试 | `ConPTY.Differential` | 通过 | - |
| 与 conhost 差分 | `COREHOST_TEST_DIFFERENTIAL=1` | 通过 | - |

## 已修复

| 症状 | 根因 | 修复 |
|---|---|---|
| 所有输入无响应 | `ApiDescriptorSize` 被错误地与 `sizeof(CONSOLE_MSG_HEADER)` 比较，合法 `desc=4` 请求被拒绝 | 只与 body 可用长度比较 |
| `GetConsoleTitle` 截断 | 完成时 `Write.Size` 未包含 `CONSOLE_GETTITLE_MSG` | `sizeof(CONSOLE_GETTITLE_MSG) + written` |
| PowerShell 输出写到行尾 | `line_feed` 保留列坐标 | LF 回列 0 |
| 输出对象被当成输入对象 | CONNECT 未设置 output/alternate object | CONNECT/CREATE/CLOSE 都同步 |
| `WriteConsoleOutput` 大块数据错误 | 未从 ConDrv 读取 body 外 payload | 使用 `input_payload_reader` |

## 待修复

| 项目 | 现象 | 初步原因 |
|---|---|---|
| edit.exe 启动 | alternate screen 已进入，但菜单/编辑区不绘制，进程停在初始化 | 已与 `build/Release/corehost.exe` 逐条对比：libcorehost 在 RAW_WRITE 137 字节后收到 `GetConsoleInput` 返回 560 字节终端应答，再写 RAW_WRITE 4351 字节绘制 UI；corehost2 写 137 字节后没有后续 API。直接同步透传 RAW_WRITE 会在 `terminal_writer::flush()` 阻塞，说明需要独立输出队列，不能在 server 线程同步写 |
| PowerShell/pwsh 大小写与连字符 | 自动输入 `A-` 只显示 `a`；`Write-Host` 变成 `writehost` | 已对比 libcorehost：同一输入下 libcorehost 显示 `A-`，corehost2 只显示 `a`。原始 Win32 序列为 `ESC[65;0;65;1;48;1_` 与 `ESC[189;0;45;1;32;1_`；ReadConsoleInput 记录也带 char=65/45。需要继续对比 libcorehost 的输入队列/修饰键处理 |

## 复现命令

```powershell
Copy-Item build\Release\corehost2.exe 'C:\1\terminal-1.24.12741.0\OpenConsole.exe' -Force
Start-Process 'C:\1\terminal-1.24.12741.0\WindowsTerminal.exe'
```

## 当前诊断日志

corehost2 保留以下日志：

- `dispatch func/object/input/output`
- `dispatch api/desc`
- `connect input/output/alternate`
- `raw write bytes/alt`
- `raw output` hex
- `raw write prepared/completed inline`
- `forward id/chars`
- `parser write_raw id`
- `input bytes/alt/raw_pending/console_read`
- `input` hex
- `input parsed text/queue/read_pending/completed`
- `terminal response id/chars`
- `handler flush begin/end`

最近一次 edit 对比：

```text
corehost2: raw write 137 -> input 517 -> queue 1034 -> 无 GetConsoleInput
libcorehost: raw write 137 -> GetConsoleInput 560 -> raw write 4351
```

说明查询已经送到 WT，WT 也已应答，但 edit 仍未进入 `GetConsoleInput`。

## 与 libcorehost 的直接对比

| 场景 | libcorehost | corehost2 |
|---|---|---|
| PowerShell 输入 `A-` | 显示 `A-` | 只显示 `a` |
| edit 启动 | 菜单/编辑区正常绘制 | alt screen 空白 |

截图：

- libcorehost：`build/corehost1_A.png`
- corehost2：`build/scan2.png`
- 原始 conhost edit：`build/edit_orig.png`
- corehost2 edit：`build/edit_route.png`

## 备注

- 自动化输入使用 SendKeys/SendInput；PowerShell 的 PSReadLine 会重绘输入行，截图中可能出现淡色重复文本。
- 左侧边缘的竖排字符来自 WT 透明边框后的桌面内容，不属于控制台输出。
- `openconsole.exe` 当前仍是 corehost2；恢复原始宿主请复制 `OpenConsole.exe.orig`。

## 2026-11-15 追加对比

- 真实 WT + PowerShell 5.1：
  - libcorehost：输入 `echo HELLO-123`，命令行和输出均保留大写与连字符。
  - corehost2：同一 SendKeys 输入显示为 `echo hello123`，大写和连字符丢失。
  - corehost2 原始 Win32Input 日志中 `Uc=104`、`ControlKeyState=0`；libcorehost 同场景为 `Uc=104`、`ControlKeyState=32`，说明 corehost2 的输入模式/修饰键路径仍与 libcorehost 不同。
- edit：
  - corehost2 仍只进入 alternate screen，菜单/编辑区不绘制。
  - 日志显示 edit 的 RAW_WRITE 137 字节完成，WT 返回 517/1034 字节终端应答；corehost2 将应答解析为 terminal response，但 `complete_pending` 时 `pending_active=0`，之后没有新的 `GetConsoleInput` 请求。
  - 尝试改为 piggyback completion 后 edit 仍空白，已撤销该无效修复。

## 修复记录

- `a6f1f8e fix(corehost2): preserve Win32Input key metadata in VT input mode`
  - 在 `raw_passthrough` 前优先处理 `win32_input_key`，避免把 Win32Input CSI 原文重新编码成 KEY_EVENT。
  - 新增 `test_corehost2` 回归：raw_passthrough 下保留 VK/Uc/ControlKeyState。
  - 该修复通过单元测试，但尚未解决 PS5.1 大写/连字符问题。

## 2026-11-15 PS5.1 输入模式排查

- PowerShell 5.1 的 `SetConsoleMode` 请求为 `0x000001E4`，corehost2 存储为 `0x00000004`，没有设置 `ENABLE_VIRTUAL_TERMINAL_INPUT (0x0200)`。
- 因此 PS5.1 输入走的是 Win32Input/KEY_EVENT 路径，不是 raw_passthrough。
- 同一物理输入下：
  - libcorehost 原始 Win32Input 的 `ControlKeyState=32`；
  - corehost2 原始 Win32Input 的 `ControlKeyState=0`。
- 该差异发生在 WT 发送输入字节之前，继续从 corehost2 的终端模式/初始化输出与 libcorehost 对比。
- edit 的 RAW_WRITE 137 已 inline 完成，WT 返回 517/1034 字节应答；corehost2 将应答消费为 terminal response，但 edit 不再发新的 GetConsoleInput，仍未定位到最小修复。

## 2026-11-15 GetConsoleInput 修复

根因：

`read_console_input` 的 completion 只把 `INPUT_RECORD` 数据当作 `Write.Size`，但客户端回复区实际是：

```text
CONSOLE_GETCONSOLEINPUT_MSG + INPUT_RECORD[]
```

导致 PowerShell 5.1 从错误偏移读取记录，表现为：

```text
输入: echo A-\
显示: echo a
```

大小写、连字符、反斜杠全部丢失。

修复：

- `Information` 仍为 record 数据字节数；
- `Write.Size` 改为 `sizeof(CONSOLE_GETCONSOLEINPUT_MSG) + record_data`；
- `Write.Data` 指向 body + header。

真实 WT + PowerShell 5.1 验证：

```text
echo A-\
A-\
```

已恢复。

新增单元回归：`test_corehost2` 检查 `Write.Size`、`Information`、`Write.Data` 和 `NumRecords`。

提交：

```text
b94a3f3 fix(corehost2): correct GetConsoleInput completion layout
```

edit 仍空白，未受此修复影响。

## 2026-11-15 修复后 shell / resize 矩阵

同一 corehost2 会话（真实 WT，PowerShell 5.1 默认）：

```text
PS C:\Users\xyx> echo PS5-A-\
PS5-A-\
PS C:\Users\xyx> cmd /c echo CMD-X-1
CMD-X-1
PS C:\Users\xyx> pwsh -NoLogo -Command "Write-Output PWSH-OK"
PWSH-OK
```

- PowerShell 5.1：大小写、连字符、反斜杠正确；
- cmd：大小写、连字符正确；
- pwsh：独立进程启动和输出正确；
- WT 窗口从 1129x635 调整到 820x500；
- `mode con` 返回 `Lines: 22`、`Columns: 85`，与调整后窗口匹配。

截图：`C:\1\c2_matrix.png`、`C:\1\c2_resize.png`（测试后清理）。

edit 仍停留在 alternate screen，菜单/编辑区不绘制；尚未找到最小修复。

## 2026-11-15 原始 OpenConsole 截图基线

将 `C:\1\OpenConsole.exe` 临时复制为 WT 的宿主，执行相同矩阵：

```text
PS C:\Users\xyx> echo PS5-A-\
PS5-A-\
PS C:\Users\xyx> cmd /c echo CMD-X-1
CMD-X-1
PS C:\Users\xyx> pwsh -NoLogo -Command "Write-Output PWSH-OK"
PWSH-OK
```

原始 OpenConsole 与修复后的 corehost2 输出一致（PS5、cmd、pwsh 的大小写/连字符/反斜杠均正确）。

edit 原始 OpenConsole 截图：

- 菜单栏 `File Edit View Help`；
- 行号 `1`；
- 底部状态栏 `[CRLF] [UTF-8] [Spaces:4] 1:1 [Untitled-1.txt]`；
- 编辑区正常绘制。

corehost2 仍只有 alternate screen 光标，菜单和编辑区为空。

截图基线：`C:\1\orig_matrix.png`、`C:\1\orig_edit.png`（测试后清理）。

## 2026-11-15 libcorehost / corehost2 edit 直接对比

libcorehost 日志：

```text
RAW_WRITE 137 (api_raw_write_console)
...
RAW_WRITE 5105
```

随后 edit 菜单和编辑区正常绘制。

corehost2 日志：

```text
RAW_WRITE 137 -> completed inline
WT 517/1034 bytes terminal response
无后续 RAW_WRITE 5105
```

因此 edit 在 corehost2 中卡在 137 字节查询写之后的完成/读取握手，尚未进入绘制阶段。下一步应继续对比 RAW_WRITE completion 的提交时机和 NtWriteFile/NtReadFile 的 pending 状态。

## 2026-11-15 颜色属性修复

复现：

```powershell
Write-Host "COLOR-WHITE" -ForegroundColor Red -BackgroundColor White
Write-Host "COLOR-DEFAULT"
```

修复前 corehost2 中 `COLOR-WHITE` 与默认颜色相同；原始 OpenConsole 为红色前景、白色背景。

根因：

```text
screen_buffer::style_for_legacy
```

只写入 `text_style::legacy_attributes`，没有把 Win32 BGRI 属性投影到 `foreground`/`background`；而 `append_sgr_style` 只读取颜色字段，因此 legacy color 全部退化为终端默认色。

修复：

- 把 Win32 BGRI 映射为 SGR indexed 颜色；
- 属性 `0x07` 保持默认前景/背景，避免默认白字黑底覆盖 WT 主题；
- 新增 `LibScreen.Reference` 回归测试：
  - `0x1C` -> fg 9 / bg 4；
  - `0x07` -> default fg/bg。

提交：

```text
a3fa08d fix(libscreen): project legacy attributes to terminal colors
```

真实 WT 验证：

- `COLOR-WHITE` 显示红色前景；
- `COLOR-DEFAULT` 和 prompt 恢复 WT 默认浅色背景。

### cmd color 对比

执行：

```cmd
cmd /c "color 4F & echo CMD-COLOR-4F"
```

corehost2 与原始 OpenConsole 都显示红底，文字与背景关系一致；legacy `color` 命令的屏幕属性切换通过。

## 2026-11-16 edit 输入事件与标题修复

edit 的 `read_stdin` 使用：

```rust
WaitForSingleObject(STATE.stdin, timeout)
ReadConsoleInputExW(...)
```

corehost2 之前没有把 console input event 设置为有信号，导致 edit 阻塞在启动读取。

修复：

- `server_session` 保存 COM handoff 的 input event；
- 输入入队/raw 入队时 `SetEvent`；
- API 读取后输入队列为空时 `ResetEvent`。

提交：

```text
3c97860 fix(corehost2): signal console input event for reader waits
```

效果：

- edit 可以进入编辑页面；
- 出现菜单、行号、编辑区和状态栏；
- `Ctrl+O` 打开文件对话框正常。

标题问题：

edit 通过 OSC 0 设置标题，但 `vtstate::apply_window_title` 只更新 `state.title`，没有设置 `title_changed`，导致 corehost2 不发送窗口标题。

修复：

```cpp
state.title_changed = true;
```

提交：

```text
d127fc5 fix(vtstate): mark window title changes for terminal updates
```

效果：

```text
WT tab title: Untitled-1.txt - edit
```

与原始 OpenConsole 一致。

### 当前 edit 对比结果

- 菜单栏、行号、编辑区、状态栏与原始 OpenConsole 基本一致；
- 打开文件对话框布局和颜色一致；
- 标题已一致；
- 输入 `HELLO`、回车、状态栏 `2:1 *` 正常；
- 仍有一个差异：F10 未打开 File 下拉菜单，而原始 OpenConsole 会打开。
- `Ctrl+O` 对话框正常，`COLOR-WHITE` 和 cmd `color 4F` 已对比通过。

### 测试

```text
CoreHost2.Reference  Passed
LibScreen.Reference  Passed
```

`VtState.Reference` 当前在测试程序中报告 `index failed`；用 `d127fc5^` 的原始测试文件复跑同样失败，确认是既有基线失败，不是标题修复引入。

### edit 剩余差异

- F10 未打开 File 下拉菜单；原始 OpenConsole 会打开。
- 鼠标点击菜单栏后，corehost2 底部出现两行状态栏，原始 OpenConsole 只有一行。
- 这属于 edit 重绘/鼠标输入路径的输出问题，初步怀疑与 alternate screen 下 raw 输出和 screen_buffer 重绘同时发生有关，尚未修复。

## 2026-11-16 console code page 初始化

`mode con` 对比：

原始 OpenConsole：

```text
Lines: 28
Columns: 105
Code page: 65001
```

corehost2：

```text
Lines: 28
Columns: 105
Code page: 0
```

根因：`run_conpty_session` 创建 `console_state` 后没有设置默认输入/输出代码页。

修复：

```cpp
state.state().input_code_page = 65001;
state.state().output_code_page = 65001;
```

提交：

```text
aa891b9 fix(corehost2): initialize conpty code pages to UTF-8
```

修复后 corehost2 `mode con`：

```text
Code page: 65001
```

与原始 OpenConsole 一致。

该修复不影响 edit 的重复状态栏问题；重复状态栏仍然存在。

## 2026-11-16 SGR mouse tracking

WT 对 edit 发送的是 X10 鼠标序列：

```text
ESC [ M 20 40 2E
```

corehost2 的 parser 只识别 SGR 1006（`ESC[<b;x;yM`），X10 序列被当成普通字符，点击编辑区会插入 `@.#` 之类的文本，并触发错误重绘。

修复：

- conpty 启动时主动发送：

  ```text
  ESC[?1002;1006;2004h
  ```

- 同步设置 `private_modes` 1002/1006/2004；
- WT 改为发送 SGR 鼠标序列，parser 生成 `mouse_sgr`；
- 回归测试检查 SGR `0;2;2M` 对应 0-based `(1,1)`。

提交：

```text
87bbf61 fix(corehost2): enable SGR mouse tracking for conpty sessions
```

效果：

- 点击编辑区不再插入 `@.#`；
- 点击菜单栏不再出现重复状态栏；
- File 菜单仍然没有通过鼠标打开，需继续定位。

## 2026-11-16 F10 / 快捷键修复

WT 发送的 F10 Win32Input：

```text
ESC[121;68;0;1;0;1_
```

`Uc=0`，corehost2 之前只生成 `KEY_EVENT(character=0)`，edit 的 `read_stdin` 只收集字符，因此 F10 丢失。

修复：

```text
f25c1d5 fix(corehost2): translate Win32Input function keys to VT sequences
```

- F1-F12、方向键、Home/End/Insert/Delete/PageUp/PageDown 转为 VT 序列字符；
- 新增单元测试：F10 -> `ESC[21~`。

真实 WT 验证：

- 先点击终端获得焦点；
- F10 打开 File 下拉菜单；
- `New File / Open File / Save / Save As / Close File / Exit` 显示正常；
- `Ctrl+Q` 打开 `Unsaved Changes` 对话框；
- 选择 `Don't Save` 后 edit 退出并回到 PowerShell prompt。

edit 快捷退出流程通过。

## 2026-11-16 alternate screen 鼠标输入

原始 OpenConsole 中鼠标点击 `File` 菜单会打开下拉菜单；corehost2 之前虽然已经收到 SGR 鼠标事件，但 edit 使用 VT 字符流解析鼠标，`MOUSE_EVENT` 被 read_stdin 忽略。

修复：

```text
89e2899 fix(corehost2): pass alternate-screen mouse input as VT sequences
```

- alternate screen 下把 SGR 鼠标序列作为 VT 字符放入输入队列；
- 普通屏幕仍生成 MOUSE_EVENT；
- 新增 alternate screen 鼠标回归测试。

真实 WT 验证：

- 鼠标点击 `File` 菜单成功打开；
- 菜单颜色、布局与原始 OpenConsole 一致；
- F10 菜单也正常；
- 编辑区点击不再插入 `@.#`；
- 状态栏不再重复。

### 当前 edit 已验证

```text
页面/菜单/行号/编辑区/状态栏
标题 Untitled-1.txt - edit
输入 HELLO + 回车
Ctrl+O 文件对话框
F10 File 菜单
鼠标点击 File 菜单
Ctrl+Q Unsaved Changes
Don't Save 退出
```

### 保存/打开文件

已验证：

```text
edit
输入 HELLO-SAVE
Ctrl+S
Save As...
Ctrl+A -> c2save.txt -> Enter
```

生成：

```text
C:\Users\xyx\c2save.txt
内容 HELLO-SAVE
```

随后：

```text
Ctrl+Q
edit C:\Users\xyx\c2save.txt
```

打开后：

```text
标题 c2save.txt - edit
编辑区 HELLO-SAVE
状态栏 [CRLF] [UTF-8] [Spaces:4] 1:11   [c2save.txt]
```

保存和打开文件的实际读写通过。

仍有一个启动问题：首次启动 `edit <file>` 后窗口可能保持空白，需要一次 resize/鼠标点击后才显示 UI；该问题尚未修复。

## 2026-11-16 IO 状态：提交后 completion 未清空

对比 libcorehost `message_router`：无消息时会把 `prev_done` 置空，下一轮 READ_IO 不再重复提交已提交的 completion。

libcorehost2 的 `coreio::run_server` 在 `no_message` 后保留 `previous`，下一轮会再次提交同一个 completion。

修复：

```text
a32e877 fix(coreio): clear submitted server completion when idle
```

```cpp
previous = nullptr;
transport.wait();
```

新增 CoreIO 回归测试，确认 idle 后下一轮 READ_IO 的 previous 为 null。

该修复没有解决 edit 首次启动空白。

### edit 初始 IO 诊断

日志：

```text
RAW_WRITE 4700 alt=1
API4 read_console_input complete count=64 ... count=10
API4 read_console_input pending flags=0x0000
server_session pending kind=2
```

说明 edit 已写出初始 UI，随后读走终端应答并进入 pending 输入等待；UI 仍不可见。

本轮尝试：

- 在第一次 pending ConsoleInput 时合成一个 `WINDOW_BUFFER_SIZE_EVENT` 并立即完成，让 edit 重绘；
- 结果仍空白，已撤销。

因此首次空白不是单纯的 input pending 状态；需要继续对比 libcorehost 的初始渲染/IO 时序。

## 2026-11-16 首次启动空白修复

根因：edit 的初始 alternate-screen UI 帧在 WT 完成初始 viewport 调整前写出；WT 清空 alternate screen 后，帧丢失。edit 随后进入 `GetConsoleInput` pending 等待，直到 resize/点击才重绘。

修复：

- `server_session::write_raw` 在 alternate screen 下累计原始输出帧；
- 当 `GetConsoleInput` 第一次进入 pending 且尚未重放时，把累计的 alternate 帧重新写入终端并 flush；
- 每个 session 只重放一次。

提交：

```text
e27f416 fix(corehost2): replay initial alternate frame before blocking input
```

真实 WT 验证：

```text
edit C:\Users\xyx\c2save.txt
```

不再需要 resize/点击，直接显示：

```text
标题 c2save.txt - edit
File Edit View Help
1 HELLO-SAVE
状态栏 [CRLF] [UTF-8] [Spaces:4] 1:1
```

此前 `mode con` 代码页修复和 CoreIO `previous=nullptr` 修复也保留：

```text
aa891b9 fix(corehost2): initialize conpty code pages to UTF-8
a32e877 fix(coreio): clear submitted server completion when idle
```

### 后续修正：不重放原始帧，改为重绘 screen buffer

`e27f416` 的原始 alternate 帧重放不是 libcorehost 的模型，只是临时 workaround。

libcorehost 的模型是：

```text
RAW_WRITE 更新 screen_buffer
-> render_visible_viewport() 从 screen_buffer 重绘
```

corehost2 正确做法也应是重绘本地 screen buffer，而不是重放旧原始字节。

已替换为：

```cpp
vtserializer::render_viewport(writer_->buffer(), screen_, cursor, &state_);
if (!state_.title.empty())
    vtserializer::append_window_title(writer_->buffer(), state_.title);
```

提交：

```text
3e6b3df fix(corehost2): repaint initial alternate viewport from screen buffer
```

验证：

```text
edit C:\Users\xyx\c2save.txt
```

首次启动直接显示 UI 和标题，无需 resize/点击；日志：

```text
render initial alternate viewport
```

### edit 编辑、resize、保存验证

- 打开 `c2save.txt` 后点击编辑区，`HELLO-SAVE` 后：
  - `{END}`、`!`、Backspace、左移、`X`、Enter；
  - 结果第一行 `HELLOX`，第二行 `-`；
  - 状态栏 `2:1 *`。
- 窗口从 `1129x635` resize 到 `900x540` 后 UI 仍正常，无重复状态栏。
- `Ctrl+S` 覆盖保存 `c2save.txt`，文件内容：

```text
HELLOX
-
```

- `Ctrl+Q` 退出并返回 PowerShell prompt。

## 2026-11-16 PageUp/特殊键与正常屏幕

测试 PowerShell 输出 100 行后按 PageUp：

corehost2 之前把 PageUp 的 Win32Input 特判转换成 `ESC[5~` 字符，PowerShell 提示符出现：

```text
PS C:\Users\xyx> 5~
```

原始 OpenConsole 保持为 KEY_EVENT(PageUp)，提示符不出现 `5~`。

根因：`vt_input_sequence` 转换被无条件用于正常屏幕。

修复：

```cpp
if (key->character == 0 && (raw_passthrough || screen_.alternate_active()))
```

- 正常屏幕保持 KEY_EVENT；
- alternate screen（edit）转换为 VT 序列；
- 新增 PageUp 正常屏幕回归测试；
- F10 alternate screen 测试更新为先进入 1049h。

提交：

```text
dddf2b4 fix(corehost2): keep special keys as key events on normal screen
```

真实 WT 验证：

```text
powershell -File C:\1\scroll_test.ps1
PageUp
```

提示符不再出现 `5~`，行为与原始 OpenConsole 一致；edit F10 菜单仍正常。

## 2026-11-16 legacy 前景色不应带黑底

复现：

```powershell
$e=[char]27
[Console]::Write("$e[38;2;255;0;0mRGB-RED$e[0m`n")
[Console]::Write("$e[38;5;196mIDX-RED$e[0m`n")
Write-Host "HOST-RED" -ForegroundColor Red
```

原始 OpenConsole：三行均为红字、默认浅色背景。

corehost2：`HOST-RED` 红字黑底。

日志：

```text
set_text_attribute attr=0x000C
```

`attr=0x000C` 表示红色前景 + 背景索引 0。`style_for_legacy` 把背景 0 映射为 indexed black，而 conhost 的 VT 投影把背景 0 视为默认背景。

修复：

```cpp
value.background = ((attributes >> 4) & 0x0F) == 0
                       ? color{}
                       : color{color_kind::indexed, to_indexed((attributes >> 4) & 0x0F)};
```

提交：

```text
65a41a2 fix(libscreen): keep default background for legacy foreground attributes
```

新增测试：

```text
0x0C -> fg 9, bg default
```

真实 WT 验证：RGB-RED / IDX-RED / HOST-RED 均为红字、默认背景，与原始 OpenConsole 一致。

补充：`Write-Host "BLACK-BG" -ForegroundColor White -BackgroundColor Black` 在原始 OpenConsole 和 corehost2 中都显示为白字默认背景，行为一致。

## 2026-11-16 console API probe 对比

脚本：

```text
sbi
settitle API-TITLE
gettitle
write API-WRITE
sbi
exit
```

corehost2 与原始 OpenConsole 输出一致：

```text
[script] sbi size=120x30 cursor=0,4 attr=0x0007
[script] settitle ok
[script] gettitle "API-TITLE"
API-WRITE[script] write ok
[script] sbi size=120x30 cursor=0,8 attr=0x0007
```

说明：

- `GetConsoleScreenBufferInfo` 尺寸/光标/属性正确；
- `SetConsoleTitleW` / `GetConsoleTitleW` 正确；
- `WriteConsole` 不换行、光标推进正确；
- 属性保持 0x0007。

## 2026-11-16 cmd cls / title

```cmd
cmd /k "title CMD-TITLE & cls & echo AFTER-CLS"
```

corehost2 与原始 OpenConsole 都：

- 标题变为 `CMD-TITLE`；
- 清屏后只显示 `AFTER-CLS`；
- 提示符 `C:\Users\xyx>` 正常。

## 验证汇总

已验证的主要组合：

```text
PowerShell 5.1 输入/输出
cmd 输入/输出/color/cls/title
pwsh 命令输出
edit 启动/输入/编辑/resize/保存/打开/退出
edit F10 菜单/鼠标菜单
PageUp 正常屏幕与 edit alternate screen
RGB/256/16 色与 legacy 背景
GetConsoleScreenBufferInfo/SetConsoleTitle/GetConsoleTitle/WriteConsole
窗口 resize 与 mode con
```

测试：

```text
CoreIO.Reference     Passed
CoreHost2.Reference  Passed
LibScreen.Reference  Passed
```

## 2026-11-16 基础设施整改

提交：

```text
8aa7e7f refactor: tighten pty signal io, utf8 decoding, and console defaults
```

- `pty_signal_thread` 按 id 读取局部 `uint16_t/uint32_t/uint64_t`，不再保留 8 字节 payload 成员；未知 id、截断 payload 记录日志后正常返回，让会话线程有序收尾。
- `thread_entry` 不捕获异常，异常不再被转成 abort。
- 删除仅测试使用的 `libconptyio/signal_reader.hpp`。
- 新增 `vtparser::utf8_decoder`，pending 固定为 `std::array<char8_t,4>`；`terminal_reader` 使用 `std::vector<char8_t>`，删除 `std::string pending_`。
- `read_raw` 不再创建临时 `std::string line`，也移除了 `emit` lambda，直接写入输出缓冲完成 CR/LF 归一化。
- `console_state` 按 libcorehost 规则初始化输入/输出代码页；`from_chars` 失败 fallback 到 `CP_UTF8`，删除 console API 中 `code_page != 0` fallback。
- `WriteConsole`/`WriteConsoleOutputString` 的非 Unicode 路径不再分配临时 `std::u16string`，改为复用已有缓冲。
- `conpty_entry` 断言 `vt_in`/`vt_out` 有效；输入线程无条件启动。
- 删除等待数组的 `wait_count == 0` abort，以及输入/窗口句柄的显式合法性检查，交给 `WaitForMultipleObjects` 报错。
- 生产构建始终把 server/signal 线程句柄加入等待；只有 `test_corehost2` 定义 `COREHOST2_TESTING`，测试构建才跳过未启动的可选线程。
- 删除启动时无条件发送的 `?1002;1006;2004h` 和本地 private mode 强制启用。
- 增加 `server_session::should_forward_to_terminal(set_private_mode)` 测试。

关键回归修复：删除启动鼠标模式后，edit 的鼠标点击最初退化成 X10 文本；根因是 `set_private_mode` 只更新本地状态，没有转发给 WT。加入该转发后：

```text
real ConDrv RAW_WRITE
?1049h ?1002;1006;2004h
```

WT 收到应用自己的鼠标模式，edit 鼠标菜单正常，且不再需要全局强制启用。

验证：

```text
edit 首次启动直接显示 UI
edit 鼠标 File 菜单正常
edit F10 菜单正常
edit Ctrl+S/Ctrl+Q 返回 PowerShell
PowerShell 100 行 PageUp 后提示符无 5~
RGB/256/legacy 颜色正常
mode con Code page 65001
```

测试：

```text
ConptyIO.Reference   Passed
VtParser.Reference   Passed
CoreHost2.Reference  Passed
ConsoleAPI.Reference Passed
CoreIO.Reference     Passed
LibScreen.Reference  Passed
ConsoleState         Passed
```

## 2026-11-16 高频 I/O 与缓冲优化

### server / vt_in / vt_out

- `vt_in` 保持 `vt_input_thread` 的固定 64 KiB 批量读取。
- `server` 的 `READ_IO` 一次只返回一条消息；保留当前把上一条 completion 作为下一次 READ_IO 输入的 piggyback 机制。
- `vt_out` 删除 `message_handler` 内部 flush 和 `run_server` 无条件 flush 的重复：只有 writer 有数据时才 flush；进入 wait、disconnect、stop 前保证 flush。
- 真实 WT 验证发现：服务端 READ_IO 是同步阻塞的，不能在“下一条消息未到达”时延迟 flush，否则 edit 初始帧会一直留在缓冲里。因此当前按“有输出就 flush”执行，批处理边界受同步 READ_IO 限制；没有引入额外线程或异步队列。

### 固定缓冲/复用

- `terminal_reader` 的 4096 字节缓冲从 `std::vector<char8_t>` 改为 `std::array<char8_t, 4096>`。
- 删除 `terminal_reader::closed_` 和未调用的 `closed()`。
- `message_handler` 复用 ctrl 事件 vector。
- `console_api` 复用：
  - `text_buffer_`
  - `ansi_buffer_`
  - `char_buffer_`
  - `prepend_buffer_`
- `take_ctrl_events` 使用 `append_range` 并保留容量。
- 新增/更新 `CoreIO.Reference` 的 flush 测试。

### 测试宏隔离

`COREHOST2_TESTING` 只定义在测试目标：

- `test_corehost2`
- `test_consoleapi`

生产 corehost2 中以下测试分支被编译掉：

- `server_session` 的 null writer 分支；
- `console_api` 的 null payload / input / processes fallback；
- `server_dispatcher` 的无 server 分支；
- session loop 中未启动 server/signal 的等待分支。

### 验证

```text
CoreIO.Reference     Passed
ConptyIO.Reference   Passed
CoreHost2.Reference  Passed
ConsoleAPI.Reference Passed
```

真实 WT：edit 初始 UI、鼠标菜单正常；PowerShell/cmd/pwsh 无回归。

### 0x80 分支优化撤销

按要求撤销了三个基于逐字节/逐字符 `0x80` 判断的 ASCII fast path：

```text
03a651b Revert "perf(vtparser): decode ascii runs without per-byte decoder calls"
91da3ad Revert "perf(vtserializer): serialize ascii text without decoder calls"
1b4155f Revert "perf(libscreen): fast-path ascii in write_chars"
```

后续优化不使用逐字节 `0x80` 判断。

本轮继续优化的两个函数：

- `byte_buffer::append_number`：去掉 16 字节 scratch buffer 的零初始化，直接让 `std::to_chars` 写入；
- `append_utf16`：使用 `value_or(0xFFFD)` 去掉逐码点的 optional 分支。

```text
35e8c09 perf(vtserializer): avoid zeroing number scratch buffer
1eb81a9 perf(vtserializer): remove optional branch for text codepoints
b0939ea perf(consoleapi): reuse GetConsoleInput event scratch buffer
```

`GetConsoleInput` 的 64 项 `input_event` scratch array 从每次调用构造的局部数组改为 `console_api` 成员 `read_events_`，避免每次 API 调用重新构造/清零 64 项事件。

同一条 `read_events_` 复用于 `ReadConsole`，去掉第二个局部 64 项数组：

```text
f7a29de perf(consoleapi): reuse ReadConsole event scratch buffer
```

### 可打印 ASCII 跳过 grapheme 分割

`write_text_row<text_measurement::graphemes>` 在执行 Unicode grapheme 分割前，用 64 位 SWAR 检查整段是否为可打印 ASCII；是则直接按单 cell 写入。混合文本仍走原 grapheme 路径。

`C:\1\bench_text.cpp`，4096 units，30 万次，Release `/O2`：

```text
ascii  old=26.7911 ns/unit new=0.2198 ns/unit ratio≈121.9x
mixed  old=26.4318 ns/unit new=26.0547 ns/unit ratio≈1.01x
```

新增 ASCII/控制/DEL/非 ASCII 检测测试。

```text
b33bbde perf(libscreen): skip grapheme segmentation for printable ascii
```

ASCII fast path 进入时只做一次 cursor 合法性检查，循环内只保留宽度检查，删除每 cell 的重复 bounds 检查：

```text
fb4d3ef perf(libscreen): remove per-cell bounds checks in ascii run
```

ASCII run 循环先计算本行剩余宽度，循环次数直接取 `min(text.size(), available)`，不再每字符检查 `cursor.x >= width`，`write_scalar` 的返回值也无需在已知合法范围内判断：

```text
839fd49 perf(libscreen): bound ascii run loop by row remainder
```

`text.empty()` 检查移到 `write_text_row` 最外层，避免只在 ASCII 分支内重复判断：

```text
b5ebe2f refactor(libscreen): check empty text before mode dispatch
```

### 命令展开缓冲复用

`read_console` 回车处理不再每次构造局部 `std::u16string expanded`，复用成员 `expanded_line_`：

```text
a00a71f perf(consoleapi): reuse expanded command buffer
```

### SetTitle 缓冲复用

`set_title` 不再每次构造局部 `std::u16string title` 和 `std::wstring wide`，复用成员 `title_buffer_` / `wide_title_buffer_`：

```text
346aeed perf(consoleapi): reuse set title buffers
```

### 移除 Alias 编码回退

`get_aliases` 已在调用 `append_alias_value` 前精确检查容量，因此移除容量不足时的临时字符串回退和 `encode_console_text`；容量不变量改为 `assert(units <= available)`。

```text
8236cb4 perf(consoleapi): remove alias encoding fallback
```

### 命令历史直接编码到输出

`get_command_history_length` 使用 `console_text_length`；`get_command_history` 直接写入输出，不再为每条 command 构造临时字符串。容量预检查也改为精确的编码后大小，去掉了第二重检查。

```text
766a11a perf(consoleapi): encode command history directly into output
```

### Alias 值直接写入输出

`get_aliases` 的长度计算使用 `console_text_length`；`append_alias_value` 在容量足够时直接调用 `WideCharToMultiByte` 写入输出，不再为 source/target 构造临时字符串：

```text
d88215c perf(consoleapi): append alias values without temp strings
```

### Alias Target 直接编码到输出

`get_alias` 的非 Unicode 分支直接写入输出，不再构造临时 `std::string`：

```text
b0779d6 perf(consoleapi): encode alias target directly into output
```

### Alias 长度查询去掉临时字符串

新增 `console_text_length`，直接调用 `WideCharToMultiByte(..., nullptr, 0)` 取长度；`get_aliases_length` 和 `get_alias_exes_length` 不再构造临时 `std::string`：

```text
c1090cc perf(consoleapi): size aliases without temporary strings
```

### Alias EXE 直接编码到输出

`get_alias_exes` 的非 Unicode 分支先取长度，确认容量后直接把 `WideCharToMultiByte` 结果写入输出，不再为每个 executable 分配临时 `std::string`：

```text
557e7d1 perf(consoleapi): encode alias exes directly into output
```

### Alias EXE 长度查询去掉临时字符串

`get_alias_exes_length` 的非 Unicode 分支直接调用 `WideCharToMultiByte(..., nullptr, 0)` 取长度，不再构造 `std::string` 再取 `.size()`：

```text
f4d1de1 perf(consoleapi): size alias exes without temp string
```

### Ctrl 事件目标缓冲复用

`generate_ctrl_event` 不再每次构造 `std::vector<std::uintptr_t> targets`，复用成员 `ctrl_targets_`：

```text
ad49bd7 perf(consoleapi): reuse ctrl event target buffer
```

### Alias EXE 去重缓冲复用

`get_alias_exes_length` / `get_alias_exes` 的局部 `seen` 向量改为复用成员 `alias_exes_seen_`，每次查询只 `clear()` 保留容量：

```text
3c78768 perf(consoleapi): reuse alias exe seen buffer
```

### 命令历史队列

`console_state::command_history` 从 `std::vector<std::u16string>` 改为 `bizwen::deque<std::u16string>`：

- 达到上限时 `pop_front()`；
- 设置上限时循环 `pop_front()`；
- 不再 `erase(begin)`。

```text
2c17d1b perf(console): use deque for command history
```

### 待处理 Console Input 队列

`server_session::queued_console_inputs_` 从 `std::vector<coreio::io_msg>` 改为 `bizwen::deque<coreio::io_msg>`，头部消费不再 `erase(begin)`：

```text
b6dcd71 perf(corehost2): use deque for queued console inputs
```

同类队列基准（4096 项，Release `/O2`）：

```text
vector erase(begin)=1817.503 ns/event
deque pop_front=1.527 ns/event
ratio≈1190x
```

### GetConsoleTitle ANSI 缓冲复用

`get_title` 的非 Unicode 分支不再每次构造 `std::vector<char>`，复用成员 `ansi_buffer_`：

```text
0f05cb0 perf(consoleapi): reuse title conversion buffer
```

### 历史选择缓冲复用

`read_console` 的上/下方向键历史选择不再每次构造局部 `std::u16string`，复用成员 `history_selection_`：

```text
cafc79f perf(consoleapi): reuse history selection buffer
```

### 单码点 scratch 去零初始化

`append_utf16` 单码点路径的 4 字节栈缓冲不再零初始化；encoder 会写入实际使用的字节：

```text
751fa27 perf(vtserializer): avoid zeroing single-unit scratch
```

### 未保留：resize_and_overwrite 转换

尝试将 `write_raw` 的：

```cpp
text_.resize(units);
MultiByteToWideChar(... text_.data() ...);
```

改为 `resize_and_overwrite`，避免 resize 零初始化。独立微基准（4096 字节，2 万次，Release `/O2`）：

```text
ascii old=0.2336-0.2544 ns/byte new=0.2349-0.2458 ns/byte ratio=0.95-1.07x
cjk   old=0.9885-1.3247 ns/byte new=0.9811-1.0390 ns/byte ratio=1.00-1.28x
```

收益不稳定，部分场景反而变慢，代码复杂度增加，因此不保留，恢复原实现。

### 单码点文本序列化

`append_utf16` 对 `text.size() == 1` 使用 4 字节栈缓冲，编码后 `append`，避免为一个码点 resize 3 倍再缩小：

```text
67cd7c0 perf(vtserializer): fast-path single-code-unit text
```

`C:\1\bench_serializer.cpp`，Release `/O2`，2000 万次：

```text
one-ascii   old=5.865-6.281 ns  new=4.511-4.554 ns  ratio=1.30-1.39x
one-cjk     old=7.005-7.749 ns  new=6.373-6.855 ns  ratio=1.06-1.18x
eight-ascii old=12.542-13.680 ns new=10.759-11.268 ns ratio=1.12-1.27x
```

### VT ground 文本扫描

`std::bitset<0x10000>` 和 128 项查表都不是有效优化，已撤销：

```text
e24eed5 Revert "perf(vtparser): use compact bitset for control lookup"
```

最终 `vt_parser::_direct_ground_text_run_length` 不使用查找表，改为 4 个 `char16_t` 一个 64 位字的 SWAR 扫描：

```cpp
const auto below_low = ~(((word | high) - low)) & high;
const auto is_del = word ^ del;
const auto at_del = ~((is_del | high) - one) & ~is_del & high;
const auto control = below_low | at_del;
```

- 每 4 个 `char16_t` 才产生一次控制字符判断；
- 无逐字符 `0x80` 判断；
- 无查找表；
- 尾部不足 4 个码点时保留标量循环。

新增测试覆盖 C0、DEL、lane 边界和非 ASCII 文本。

性能测试：`C:\1\bench_ground.cpp`，Release `/O2`，QPC 计时，每项重复 3 次；数值为 ns/char16：

```text
ascii-long           scalar=0.480-0.594  swar=0.161-0.222  ratio=2.9-3.7x
ascii-control-end    scalar=0.500-0.531  swar=0.150-0.223  ratio=2.2-3.3x
ascii-control-mid    scalar=0.126-0.132  swar=0.039-0.041  ratio=3.2-3.3x
non-ascii-long       scalar=0.483-0.565  swar=0.156-0.161  ratio=3.0-3.5x
short-8              scalar=0.592-0.663  swar=0.328-0.393  ratio=1.5-2.0x
```

结论：SWAR 在所有测试场景均快于标量扫描，保留该实现。

```text
db52434 perf(vtparser): scan four ground chars per branchless word
```

### Raw input 控制字节扫描

`server_session::read_raw` 原来逐字节判断 `\r`、`\n`、Ctrl-C、Ctrl-Z。改为 64 位字一次检查 8 字节，按是否启用 processed input 只编译出一种目标集合：

```text
no-control-64k   scalar=0.480-0.520 ns/byte  swar=0.078-0.117 ns/byte  ratio=4.4-6.4x
lf-at-end-64k    scalar=0.479-0.539 ns/byte  swar=0.077-0.113 ns/byte  ratio=4.2-6.7x
cr-at-mid-64k    scalar=0.242-0.266 ns/byte  swar=0.038-0.070 ns/byte  ratio=3.5-7.0x
ctrl-c-end-64k   scalar=0.751-0.772 ns/byte  swar=0.120-0.148 ns/byte  ratio=5.1-6.4x
short-16         scalar=0.492-0.734 ns/byte  swar=0.162-0.272 ns/byte  ratio=2.5-3.2x
```

数据来自 `C:\1\bench_raw.cpp`，Release `/O2`，QPC 计时，3 次运行。新增测试覆盖无控制字节、lane 中间控制字节、控制残留。

```text
707f509 perf(corehost2): scan raw input bytes in 64-bit words
1fed644 test(corehost2): cover raw word scan boundaries
```

CRLF/无归一化行增加直接 `memcpy` 快速路径，避免逐字节复制循环：

```text
68da681 perf(corehost2): memcpy normalized raw lines when possible
```

需要补 CR/LF 的行也不再逐字节循环，改为 `memcpy` 主体后补终止符：

```text
57c2b50 perf(corehost2): memcpy normalized raw line bodies
```

`C:\1\bench_copy.cpp`，64 KiB，20 万次，Release `/O2`：

```text
loop=0.2476-0.2540 ns/byte  memcpy=0.0170-0.0176 ns/byte  ratio≈14.3-14.6x
```

### 默认 legacy 样式缓存

`screen_buffer` 首次调用 `styles_.intern(text_style{})` 后缓存默认样式 ID，`style_for_legacy(0x0007)` 直接返回，不再对每个默认属性单元格做一次 `flat_map` 查找。

```text
b520c69 perf(libscreen): cache default legacy style id
```

### 文本序列化写入

`std::back_inserter` 不是有效优化，已撤销：

```text
363f7bc Revert "perf(vtserializer): append encoded text without resize zeroing"
```

保留的 `append_utf16` 优化只有：每次文本 run 复用一次 `unicode::decoder<char16_t>`/`encoder<char>`，并用 `value_or(0xFFFD)` 去掉 optional 分支：

```text
aeb8170 perf(vtserializer): reuse unicode codecs per text run
1eb81a9 perf(vtserializer): remove optional branch for text codepoints
```

### legacy 颜色索引（已撤销）

BGRI 转索引曾改为 16 项查找表以移除 4 个条件。微基准（`C:\1\bench_style.cpp`，2 亿次，Release `/O2`）：

```text
old=1.7836-1.8130 ns  table=1.7703-1.7721 ns  ratio≈1.01-1.02x
```

编译器已经把原条件优化成近似无分支指令，查找表收益只有 1-2%，不值得增加代码量和静态表，因此撤销：

```text
73833d5 Revert "perf(libscreen): replace bgri ternaries with compact table"
```

### INPUT_RECORD

`GetConsoleInput` 转换每条记录时不再整体执行 `record = INPUT_RECORD{}` 清零；三种事件分支都会填满各自被读取的字段：

```text
915c54b perf(consoleapi): avoid zeroing each INPUT_RECORD
```

### 输入队列 key 计数

按用户要求保留 `input_queue` 的 key 计数优化：

```text
c839ce5 Reapply "perf(consoleinput): track key events instead of scanning queue"
```

队列容器不再使用 `std::vector` + `head_` 偏移，改用 `bizwen::deque<input_event>`：

- `push_back` / `push_front` 直接入队；
- `read` 只 `pop_front()`，不再 `vector::erase(begin)`；
- 没有 `head_` 和尾部压缩逻辑；
- `byte_buffer::append` 使用 `append_range`。

```text
5cefccc perf(consoleinput): use bizwen deque for input queue
```

全部消费时直接 `clear()`，不再逐项 `pop_front`：

```text
314844b perf(consoleinput): clear deque when consuming all events
```

`C:\1\bench_queue.cpp`，4096 项、2 万次，Release `/O2`：

```text
pop=0.4081-0.4648 ns/event  clear=0.0041 ns/event  ratio≈99-115x
```

### 已撤销的样式微优化

```text
dbc2722 Revert "perf(libscreen): reuse style lookup across adjacent cells"
```

`write_char_info` 的相邻样式缓存没有性能测试数据，且给每个单元格增加分支，因此不保留。

### Console API 分派

`console_api::handle` 不再使用长串 `if (header->ApiNumber == ...)`。先取 `layer = ApiNumber >> 24`，再按 L1/L2/L3 分别 switch，和 libcorehost 的分派结构一致。未知 layer/API 仍返回 `status_not_implemented`。

### 测试稳定性

`cancellable_read` 的“in read”事件改为 manual-reset，避免 waiter 先消费 auto-reset 通知、`CancelSynchronousIo` 尚未执行时阻塞读线程造成偶发 join 超时。ConptyIO 连续 20 次运行通过。
