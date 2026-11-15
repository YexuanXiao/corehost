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
| edit.exe 启动 | alternate screen 已进入，但菜单/编辑区不绘制，进程停在初始化 | edit 发送 `OSC 4/10/11` 颜色查询，宿主未向终端转发查询，也未把终端应答回送给 edit |
| PowerShell/pwsh 大小写与连字符 | 自动输入 `A-` 只显示 `a`；`Write-Host` 变成 `writehost` | 原始 Win32 input 序列包含 `uc=65` / `uc=45`；需核对 ReadConsoleInput 的扫描码/控制状态转换，cmd 的 cooked ReadConsole 路径正常 |

## 复现命令

```powershell
Copy-Item build\Release\corehost2.exe 'C:\1\terminal-1.24.12741.0\OpenConsole.exe' -Force
Start-Process 'C:\1\terminal-1.24.12741.0\WindowsTerminal.exe'
```

## 备注

- 自动化输入使用 SendKeys/SendInput；PowerShell 的 PSReadLine 会重绘输入行，截图中可能出现淡色重复文本。
- 左侧边缘的竖排字符来自 WT 透明边框后的桌面内容，不属于控制台输出。
- `openconsole.exe` 当前仍是 corehost2；恢复原始宿主请复制 `OpenConsole.exe.orig`。
