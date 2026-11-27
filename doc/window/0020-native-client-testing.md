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

最终验证使用 `native_echo.exe`：

- 子系统：Windows CUI；
- 只用 `GetStdHandle(STD_INPUT_HANDLE)` / `ReadConsoleW`；
- 通过正常 `WM_CHAR` 和 Ctrl+Shift+V 输入；
- OpenConsole 和 corehostw 都能得到相同 `NATIVE-LINE` 输出。

早期 `native_diag` 的 `ERROR_PIPE_NOT_CONNECTED` 是测试终止/输入注入方式导致的诊断噪音，不能作为 corehost 缺陷证据；禁止用 `CONIN$` 绕过。

## 复现

测试程序：`C:\1
ative_diag.cpp`、`C:\1
ative_diag.exe`

启动：

```powershell
Start-Process C:\1\corehostbinary\corehostw.exe -ArgumentList 'C:\1
ative_diag.exe'
```

观察 `C:\1
ative_diag.txt`。

期望：继承 stdin 的 `ReadConsoleW` / `ReadConsoleInputW` 正常返回。

实际：`ERROR_PIPE_NOT_CONNECTED`。

## 验收状态

- C8 多行粘贴：已验证通过；原生 `native_echo.exe` 粘贴 3 行（末尾带换行）得到 3 条 `NATIVE-LINE`；
- 禁止用 `CONIN$` 让测试“通过”；
- 需要修复 corehostw 的继承句柄路径，或确认客户端启动方式与 conhost 不一致的根因；
- 证据：`C:\1
ative_corehost_paste3.png`。

## conhost / OpenConsole 对比

同一个原生客户端 `native_echo.exe`，相同输入序列（A/Enter/B/Enter/C/Enter）：

- OpenConsole：`C:\1
ative_openconsole_after.png`
- corehostw：`C:\1
ative_corehost_after.png`

两者输出一致：

```text
NATIVE-READY
A
NATIVE-LINE:A
B
NATIVE-LINE:B
C
NATIVE-LINE:C
NATIVE-DONE
```

说明：使用继承的 std handles、通过正常 `WM_CHAR` 输入时，原生客户端在 corehostw 和 OpenConsole 下都能工作；禁止用 `CONIN$` 绕过。

![OpenConsole native client](C:/1/native_openconsole_after.png)

![corehostw native client](C:/1/native_corehost_after.png)