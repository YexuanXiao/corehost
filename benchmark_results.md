# conpty_bench 结果

- OpenConsole: `C:\1\OpenConsole.exe`
- corehost: `build-nolog\Release\corehost.exe`（Release，`COREHOST_DISABLE_LOG=ON`）
- corehost2: `build-nolog\Release\corehost2.exe`（Release，`COREHOST_DISABLE_LOG=ON`）
- 运行方式: `conpty_bench --worker <label>`
- 时间单位: ms；ratio = corehost2 / OpenConsole

| Scenario | OpenConsole | corehost | corehost2 | ratio | out OpenConsole(MiB) | out corehost(MiB) | out corehost2(MiB) |
|---|---:|---:|---:|---:|---:|---:|---:|
| large-mixed-vt-cjk-output | 330.9 | 194.0 | 355.7 | 1.08 | 24.06 | 24.06 | 22.31 |
| long-line-three-vt-output | 835.4 | 890.1 | 1620.0 | 1.94 | 16.00 | 16.00 | 364.86 |
| powershell-type-realistic-build-output | 307.2 | 301.2 | 281.6 | 0.92 | 0.00 | 0.00 | 0.01 |
| sgr-plain | 30.3 | 21.0 | 154.6 | 5.10 | 4.06 | 4.06 | 1.93 |
| sgr-reset | 31.8 | 22.7 | 149.8 | 4.71 | 4.06 | 4.06 | 1.92 |
| sgr-bold | 89.0 | 22.7 | 141.0 | 1.58 | 4.06 | 4.06 | 2.01 |
| sgr-faint | 35.2 | 22.7 | 145.4 | 4.13 | 4.06 | 4.06 | 2.00 |
| sgr-italic | 34.5 | 22.0 | 141.8 | 4.11 | 4.06 | 4.06 | 2.00 |
| sgr-underline | 33.3 | 22.8 | 143.9 | 4.32 | 4.06 | 4.06 | 2.00 |
| sgr-blink | 34.7 | 21.4 | 197.9 | 5.71 | 4.06 | 4.06 | 2.00 |
| sgr-inverse | 34.0 | 22.0 | 143.9 | 4.23 | 4.06 | 4.06 | 2.00 |
| sgr-hidden | 35.2 | 22.1 | 142.2 | 4.04 | 4.06 | 4.06 | 2.00 |
| sgr-strike | 32.7 | 22.1 | 143.8 | 4.39 | 4.06 | 4.06 | 2.00 |
| sgr-fg-16 | 32.5 | 23.7 | 146.3 | 4.51 | 4.06 | 4.06 | 2.04 |
| sgr-fg-bright | 33.0 | 22.6 | 144.1 | 4.37 | 4.06 | 4.06 | 2.04 |
| sgr-bg-16 | 31.7 | 24.3 | 143.6 | 4.53 | 4.06 | 4.06 | 2.04 |
| sgr-bg-bright | 35.2 | 22.8 | 146.4 | 4.16 | 4.06 | 4.06 | 2.04 |
| sgr-fg-256 | 32.7 | 23.8 | 137.6 | 4.21 | 4.06 | 4.06 | 2.05 |
| sgr-bg-256 | 32.7 | 21.9 | 139.0 | 4.25 | 4.06 | 4.06 | 2.04 |
| sgr-fg-rgb | 31.2 | 23.1 | 133.3 | 4.27 | 4.06 | 4.06 | 2.10 |
| sgr-bg-rgb | 32.5 | 23.0 | 130.8 | 4.02 | 4.06 | 4.06 | 2.08 |
| sgr-combined | 32.1 | 23.3 | 134.7 | 4.20 | 4.05 | 4.05 | 2.14 |
| sgr-combined-bg | 31.3 | 23.3 | 127.2 | 4.06 | 4.06 | 4.06 | 2.17 |
| cjk-terminal-input | 2464.6 | 13.8 | 400.6 | 0.16 | 0.38 | 0.37 | 0.00 |

说明：corehost 的 cjk 已修复为正常读取并回显；corehost2 的 SGR 输出恰好约为 OpenConsole/corehost 的一半，long-line 输出约为 22 倍，属于另外的 renderer 输出问题，需单独修复。
