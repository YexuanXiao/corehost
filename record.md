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
