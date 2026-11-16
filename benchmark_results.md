# conpty_bench 结果（无 fastpath）

- OpenConsole: `C:\1\OpenConsole.exe`
- corehost: `build-nolog\Release\corehost.exe`（Release，`COREHOST_DISABLE_LOG=ON`）
- corehost2: `build-nolog\Release\corehost2.exe`（Release，`COREHOST_DISABLE_LOG=ON`）
- corehost2 使用 UTF-8 VT replay：完整解析/状态更新，文本只推进 cursor；无跳过 dispatcher 的 fastpath
- 时间单位: ms；ratio = corehost2 / OpenConsole

| Scenario | OpenConsole | corehost | corehost2 | ratio | out corehost2(MiB) |
|---|---:|---:|---:|---:|---:|
| large-mixed-vt-cjk-output | 330.9 | 194.0 | 78.3 | 0.24 | 24.06 |
| long-line-three-vt-output | 835.4 | 890.1 | 747.8 | 0.90 | 16.00 |
| powershell-type-realistic-build-output | 307.2 | 301.2 | 289.5 | 0.94 | 0.01 |
| sgr-plain | 30.3 | 21.0 | 16.2 | 0.54 | 4.06 |
| sgr-reset | 31.8 | 22.7 | 16.3 | 0.51 | 4.06 |
| sgr-bold | 89.0 | 22.7 | 16.3 | 0.18 | 4.06 |
| sgr-faint | 35.2 | 22.7 | 17.2 | 0.49 | 4.06 |
| sgr-italic | 34.5 | 22.0 | 15.5 | 0.45 | 4.06 |
| sgr-underline | 33.3 | 22.8 | 16.2 | 0.49 | 4.06 |
| sgr-blink | 34.7 | 21.4 | 15.4 | 0.44 | 4.06 |
| sgr-inverse | 34.0 | 22.0 | 15.5 | 0.46 | 4.06 |
| sgr-hidden | 35.2 | 22.1 | 15.7 | 0.45 | 4.06 |
| sgr-strike | 32.7 | 22.1 | 15.7 | 0.48 | 4.06 |
| sgr-fg-16 | 32.5 | 23.7 | 15.9 | 0.49 | 4.06 |
| sgr-fg-bright | 33.0 | 22.6 | 17.2 | 0.52 | 4.06 |
| sgr-bg-16 | 31.7 | 24.3 | 17.0 | 0.54 | 4.06 |
| sgr-bg-bright | 35.2 | 22.8 | 18.2 | 0.52 | 4.06 |
| sgr-fg-256 | 32.7 | 23.8 | 15.9 | 0.49 | 4.06 |
| sgr-bg-256 | 32.7 | 21.9 | 17.3 | 0.53 | 4.06 |
| sgr-fg-rgb | 31.2 | 23.1 | 17.0 | 0.54 | 4.06 |
| sgr-bg-rgb | 32.5 | 23.0 | 17.0 | 0.52 | 4.06 |
| sgr-combined | 32.1 | 23.3 | 16.4 | 0.51 | 4.05 |
| sgr-combined-bg | 31.3 | 23.3 | 16.6 | 0.53 | 4.06 |
| cjk-terminal-input | 2464.6 | 13.8 | 4.9 | 0.00 | 0.38 |

结论：无 fastpath 情况下，corehost2 的 large-mixed、long-line、SGR、cjk 均快于 corehost，PowerShell 与 corehost 基本持平；输出字节数与修改前一致。
