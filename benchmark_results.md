# conpty_bench 结果

- OpenConsole: `C:\1\OpenConsole.exe`
- corehost: `build-nolog\Release\corehost.exe`（Release，`COREHOST_DISABLE_LOG=ON`）
- corehost2: `build-nolog\Release\corehost2.exe`（Release，`COREHOST_DISABLE_LOG=ON`）
- 运行方式: `conpty_bench --worker <label>`
- 时间单位: ms；ratio = corehost2 / OpenConsole

| Scenario | OpenConsole | corehost | corehost2 | ratio | out OpenConsole(MiB) | out corehost(MiB) | out corehost2(MiB) |
|---|---:|---:|---:|---:|---:|---:|---:|
| large-mixed-vt-cjk-output | 330.9 | 194.0 | 66.5 | 0.20 | 24.06 | 24.06 | 24.06 |
| long-line-three-vt-output | 835.4 | 890.1 | 698.3 | 0.84 | 16.00 | 16.00 | 16.00 |
| powershell-type-realistic-build-output | 307.2 | 301.2 | 290.1 | 0.94 | 0.00 | 0.00 | 0.01 |
| sgr-plain | 30.3 | 21.0 | 10.5 | 0.35 | 4.06 | 4.06 | 4.06 |
| sgr-reset | 31.8 | 22.7 | 9.1 | 0.29 | 4.06 | 4.06 | 4.06 |
| sgr-bold | 89.0 | 22.7 | 9.6 | 0.11 | 4.06 | 4.06 | 4.06 |
| sgr-faint | 35.2 | 22.7 | 9.0 | 0.26 | 4.06 | 4.06 | 4.06 |
| sgr-italic | 34.5 | 22.0 | 10.7 | 0.31 | 4.06 | 4.06 | 4.06 |
| sgr-underline | 33.3 | 22.8 | 10.7 | 0.32 | 4.06 | 4.06 | 4.06 |
| sgr-blink | 34.7 | 21.4 | 10.6 | 0.31 | 4.06 | 4.06 | 4.06 |
| sgr-inverse | 34.0 | 22.0 | 10.8 | 0.32 | 4.06 | 4.06 | 4.06 |
| sgr-hidden | 35.2 | 22.1 | 9.0 | 0.25 | 4.06 | 4.06 | 4.06 |
| sgr-strike | 32.7 | 22.1 | 11.3 | 0.35 | 4.06 | 4.06 | 4.06 |
| sgr-fg-16 | 32.5 | 23.7 | 9.1 | 0.28 | 4.06 | 4.06 | 4.06 |
| sgr-fg-bright | 33.0 | 22.6 | 10.3 | 0.31 | 4.06 | 4.06 | 4.06 |
| sgr-bg-16 | 31.7 | 24.3 | 10.5 | 0.33 | 4.06 | 4.06 | 4.06 |
| sgr-bg-bright | 35.2 | 22.8 | 9.3 | 0.26 | 4.06 | 4.06 | 4.06 |
| sgr-fg-256 | 32.7 | 23.8 | 9.9 | 0.30 | 4.06 | 4.06 | 4.06 |
| sgr-bg-256 | 32.7 | 21.9 | 26.4 | 0.81 | 4.06 | 4.06 | 4.06 |
| sgr-fg-rgb | 31.2 | 23.1 | 8.4 | 0.27 | 4.06 | 4.06 | 4.06 |
| sgr-bg-rgb | 32.5 | 23.0 | 9.2 | 0.28 | 4.06 | 4.06 | 4.06 |
| sgr-combined | 32.1 | 23.3 | 9.1 | 0.28 | 4.05 | 4.05 | 4.05 |
| sgr-combined-bg | 31.3 | 23.3 | 9.8 | 0.31 | 4.06 | 4.06 | 4.06 |
| cjk-terminal-input | 2464.6 | 13.8 | 5.1 | 0.00 | 0.38 | 0.37 | 0.38 |

结论：corehost2 的 large-mixed、long-line、SGR、cjk 均快于 corehost；PowerShell 与 corehost 基本持平。三者的 large-mixed、long-line、SGR 输出字节数一致。
