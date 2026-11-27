# 原生控制台客户端测试约束

## 禁止事项

原生客户端测试禁止使用 `CONIN$`：

- 禁止 `CreateFileW(L"CONIN$", ...)`；
- 禁止通过 `CONIN$` 重新打开控制台输入；
- 禁止用 `AttachConsole` 或任何方式绕过继承的 std handles。

测试客户端必须和普通 CUI 程序一样使用：

```cpp
HANDLE input = GetStdHandle(STD_INPUT_HANDLE);
HANDLE output = GetStdHandle(STD_OUTPUT_HANDLE);
```

`CONIN$` 会掩盖 corehostw 对继承句柄的缺陷，不能用于验收。

## 当前结论

原生测试客户端：

- 子系统：Windows CUI；
- `GetStdHandle(STD_INPUT_HANDLE)` 返回有效句柄；
- `GetFileType` 返回 `FILE_TYPE_CHAR`；
- `GetConsoleMode` 成功，mode 为 `0x1f7`；
- `ReadConsoleW` 失败，`GetLastError() == 233`（`ERROR_PIPE_NOT_CONNECTED`）；
- `ReadConsoleInputW` 同样失败，233。

PowerShell、cmd.exe、vim 在同一 corehostw 窗口中可以正常工作。因此当前不能把原生客户端失败归因于测试环境，也不能用 `CONIN$` 绕过。

## 复现

测试程序：`C:\1\native_diag.cpp`、`C:\1\native_diag.exe`

启动：

```powershell
Start-Process C:\1\corehostbinary\corehostw.exe -ArgumentList 'C:\1\native_diag.exe'
```

观察 `C:\1\native_diag.txt`。

期望：继承 stdin 的 `ReadConsoleW` / `ReadConsoleInputW` 正常返回。

实际：`ERROR_PIPE_NOT_CONNECTED`。

## 验收状态

- C8 多行粘贴：阻塞，原因见上；
- 禁止用 `CONIN$` 让测试“通过”；
- 需要修复 corehostw 的继承句柄路径，或确认客户端启动方式与 conhost 不一致的根因；
- 修复前 C8 不得标记通过。
