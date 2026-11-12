# WT 输入模式兼容

## 结论

真实 Windows Terminal 中，PowerShell 7.7 / PSReadLine 会先设置：

```text
ENABLE_ECHO_INPUT | ENABLE_EXTENDED_FLAGS | ENABLE_QUICK_EDIT_MODE |
ENABLE_INSERT_MODE | ENABLE_AUTO_POSITION
= 0x1E4
```

这个组合包含 `ENABLE_ECHO_INPUT` 但不含 `ENABLE_LINE_INPUT`。

corehost 的 `SetConsoleMode` 必须接受这个组合并完成设置。若返回失败，PSReadLine
的 `Console.ReadKey` 会把控制台输入误判为“已重定向”，随后 PowerShell 抛出：

```text
Cannot read keys when either application does not have a console or
when console input has been redirected.
```

最终 WT 标签页退出。

## 行为

- `0x1E4` 被保存，`GetConsoleMode` 能读回；
- 输入模式中的私有位仍按 `ENABLE_EXTENDED_FLAGS` 规则处理；
- 仍然拒绝未知位。

## 测试

真实 WT + PowerShell 7.7 验证：

- 输入 `e`、回车后命令执行并回到提示符；
- `echo FINAL_OK` 输出正确；
- `Ctrl+C` 后继续输入正常；
- WT 不再退出。

自动化回归在 `tests/test_console_state.cpp` 的
`SetConsoleMode validation` 中覆盖 ECHO-only 的设置结果。
