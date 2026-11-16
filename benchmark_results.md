# conpty_bench 结果

- OpenConsole: `C:\1\OpenConsole.exe`
- corehost: `build-nolog\Release\corehost.exe`（Release，`COREHOST_DISABLE_LOG=ON`）
- corehost2: `build-nolog\Release\corehost2.exe`（Release，`COREHOST_DISABLE_LOG=ON`）
- 运行方式: `conpty_bench --worker <label>`
- 时间单位: ms；ratio = corehost2 / OpenConsole

| Scenario | OpenConsole | corehost | corehost2 | ratio | out OpenConsole(MiB) | out corehost(MiB) | out corehost2(MiB) |
|---|---:|---:|---:|---:|---:|---:|---:|
| large-mixed-vt-cjk-output | 330.9 | 194.0 | 291.9 | 0.88 | 24.06 | 24.06 | 24.06 |
| long-line-three-vt-output | 835.4 | 890.1 | 1235.5 | 1.48 | 16.00 | 16.00 | 16.00 |
| powershell-type-realistic-build-output | 307.2 | 301.2 | 285.8 | 0.93 | 0.00 | 0.00 | 0.01 |
| sgr-plain | 30.3 | 21.0 | 147.6 | 4.87 | 4.06 | 4.06 | 4.06 |
| sgr-reset | 31.8 | 22.7 | 141.6 | 4.45 | 4.06 | 4.06 | 4.06 |
| sgr-bold | 89.0 | 22.7 | 138.6 | 1.56 | 4.06 | 4.06 | 4.06 |
| sgr-faint | 35.2 | 22.7 | 139.8 | 3.97 | 4.06 | 4.06 | 4.06 |
| sgr-italic | 34.5 | 22.0 | 144.2 | 4.18 | 4.06 | 4.06 | 4.06 |
| sgr-underline | 33.3 | 22.8 | 145.9 | 4.38 | 4.06 | 4.06 | 4.06 |
| sgr-blink | 34.7 | 21.4 | 139.4 | 4.02 | 4.06 | 4.06 | 4.06 |
| sgr-inverse | 34.0 | 22.0 | 145.2 | 4.27 | 4.06 | 4.06 | 4.06 |
| sgr-hidden | 35.2 | 22.1 | 140.9 | 4.00 | 4.06 | 4.06 | 4.06 |
| sgr-strike | 32.7 | 22.1 | 143.0 | 4.37 | 4.06 | 4.06 | 4.06 |
| sgr-fg-16 | 32.5 | 23.7 | 137.2 | 4.22 | 4.06 | 4.06 | 4.06 |
| sgr-fg-bright | 33.0 | 22.6 | 140.1 | 4.25 | 4.06 | 4.06 | 4.06 |
| sgr-bg-16 | 31.7 | 24.3 | 138.6 | 4.37 | 4.06 | 4.06 | 4.06 |
| sgr-bg-bright | 35.2 | 22.8 | 140.5 | 3.99 | 4.06 | 4.06 | 4.06 |
| sgr-fg-256 | 32.7 | 23.8 | 137.9 | 4.22 | 4.06 | 4.06 | 4.06 |
| sgr-bg-256 | 32.7 | 21.9 | 135.9 | 4.16 | 4.06 | 4.06 | 4.06 |
| sgr-fg-rgb | 31.2 | 23.1 | 128.1 | 4.10 | 4.06 | 4.06 | 4.06 |
| sgr-bg-rgb | 32.5 | 23.0 | 131.4 | 4.04 | 4.06 | 4.06 | 4.06 |
| sgr-combined | 32.1 | 23.3 | 128.4 | 4.01 | 4.05 | 4.05 | 4.05 |
| sgr-combined-bg | 31.3 | 23.3 | 123.6 | 3.95 | 4.06 | 4.06 | 4.06 |
| cjk-terminal-input | 2464.6 | 13.8 | 396.4 | 0.16 | 0.38 | 0.37 | 0.38 |

结论：修复后三者的 large-mixed、long-line、SGR 输出字节数一致；cjk 仅有几 KiB 的回显边界差异。
