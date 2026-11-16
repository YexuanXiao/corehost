# conpty_bench 结果

- OpenConsole: `C:\1\OpenConsole.exe`
- corehost: `build-nolog\Release\corehost.exe`（Release，`COREHOST_DISABLE_LOG=ON`）
- corehost2: `build-nolog\Release\corehost2.exe`（Release，`COREHOST_DISABLE_LOG=ON`）
- 运行方式: `conpty_bench --worker <label>`
- 时间单位: ms；ratio = corehost2 / OpenConsole

| Scenario | OpenConsole | corehost | corehost2 | ratio | out OpenConsole(MiB) | out corehost(MiB) | out corehost2(MiB) |
|---|---:|---:|---:|---:|---:|---:|---:|
| large-mixed-vt-cjk-output | 330.9 | 194.0 | 53.6 | 0.16 | 24.06 | 24.06 | 24.06 |
| long-line-three-vt-output | 835.4 | 890.1 | 709.3 | 0.85 | 16.00 | 16.00 | 16.00 |
| powershell-type-realistic-build-output | 307.2 | 301.2 | 297.9 | 0.97 | 0.00 | 0.00 | 0.01 |
| sgr-plain | 30.3 | 21.0 | 6.6 | 0.22 | 4.06 | 4.06 | 4.06 |
| sgr-reset | 31.8 | 22.7 | 6.6 | 0.21 | 4.06 | 4.06 | 4.06 |
| sgr-bold | 89.0 | 22.7 | 6.2 | 0.07 | 4.06 | 4.06 | 4.06 |
| sgr-faint | 35.2 | 22.7 | 7.1 | 0.20 | 4.06 | 4.06 | 4.06 |
| sgr-italic | 34.5 | 22.0 | 7.8 | 0.23 | 4.06 | 4.06 | 4.06 |
| sgr-underline | 33.3 | 22.8 | 7.7 | 0.23 | 4.06 | 4.06 | 4.06 |
| sgr-blink | 34.7 | 21.4 | 8.8 | 0.25 | 4.06 | 4.06 | 4.06 |
| sgr-inverse | 34.0 | 22.0 | 8.2 | 0.24 | 4.06 | 4.06 | 4.06 |
| sgr-hidden | 35.2 | 22.1 | 9.2 | 0.26 | 4.06 | 4.06 | 4.06 |
| sgr-strike | 32.7 | 22.1 | 8.1 | 0.25 | 4.06 | 4.06 | 4.06 |
| sgr-fg-16 | 32.5 | 23.7 | 6.9 | 0.21 | 4.06 | 4.06 | 4.06 |
| sgr-fg-bright | 33.0 | 22.6 | 6.7 | 0.20 | 4.06 | 4.06 | 4.06 |
| sgr-bg-16 | 31.7 | 24.3 | 6.9 | 0.22 | 4.06 | 4.06 | 4.06 |
| sgr-bg-bright | 35.2 | 22.8 | 8.0 | 0.23 | 4.06 | 4.06 | 4.06 |
| sgr-fg-256 | 32.7 | 23.8 | 11.0 | 0.34 | 4.06 | 4.06 | 4.06 |
| sgr-bg-256 | 32.7 | 21.9 | 9.7 | 0.30 | 4.06 | 4.06 | 4.06 |
| sgr-fg-rgb | 31.2 | 23.1 | 12.4 | 0.40 | 4.06 | 4.06 | 4.06 |
| sgr-bg-rgb | 32.5 | 23.0 | 10.1 | 0.31 | 4.06 | 4.06 | 4.06 |
| sgr-combined | 32.1 | 23.3 | 12.6 | 0.39 | 4.05 | 4.05 | 4.05 |
| sgr-combined-bg | 31.3 | 23.3 | 13.1 | 0.42 | 4.06 | 4.06 | 4.06 |
| cjk-terminal-input | 2464.6 | 13.8 | 6.6 | 0.00 | 0.38 | 0.37 | 0.38 |

结论：corehost2 的 large-mixed、long-line、SGR、cjk 均不慢于 corehost；PowerShell 与 corehost 基本持平。输出字节数三者一致（cjk 有几 KiB 回显边界差异）。
