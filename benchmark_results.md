# conpty_bench 结果（无 fastpath / 无 replay 特例）

- OpenConsole: `C:\1\OpenConsole.exe`
- corehost2: `build-nolog\Release\corehost2.exe`（Release，`COREHOST_DISABLE_LOG=ON`）
- corehost2 使用完整 dispatcher/screen 状态，无 cursor-only/replay 特例

| Scenario | OpenConsole | corehost | corehost2 | corehost2/OpenConsole |
|---|---:|---:|---:|---:|
| large-mixed-vt-cjk-output | 330.9 | 194.0 | 285.0 | 0.86 |
| long-line-three-vt-output | 835.4 | 890.1 | 985.4 | 1.18 |
| powershell-type-realistic-build-output | 307.2 | 301.2 | 284.1 | 0.92 |
| sgr-plain | 30.3 | 21.0 | 44.9 | 1.48 |
| sgr-reset | 31.8 | 22.7 | 46.7 | 1.47 |
| sgr-bold | 89.0 | 22.7 | 46.2 | 0.52 |
| sgr-faint | 35.2 | 22.7 | 47.1 | 1.34 |
| sgr-italic | 34.5 | 22.0 | 47.7 | 1.38 |
| sgr-underline | 33.3 | 22.8 | 46.3 | 1.39 |
| sgr-blink | 34.7 | 21.4 | 45.7 | 1.32 |
| sgr-inverse | 34.0 | 22.0 | 46.6 | 1.37 |
| sgr-hidden | 35.2 | 22.1 | 47.7 | 1.35 |
| sgr-strike | 32.7 | 22.1 | 47.1 | 1.44 |
| sgr-fg-16 | 32.5 | 23.7 | 48.1 | 1.48 |
| sgr-fg-bright | 33.0 | 22.6 | 48.5 | 1.47 |
| sgr-bg-16 | 31.7 | 24.3 | 47.0 | 1.48 |
| sgr-bg-bright | 35.2 | 22.8 | 47.0 | 1.33 |
| sgr-fg-256 | 32.7 | 23.8 | 46.1 | 1.41 |
| sgr-bg-256 | 32.7 | 21.9 | 46.6 | 1.43 |
| sgr-fg-rgb | 31.2 | 23.1 | 45.0 | 1.44 |
| sgr-bg-rgb | 32.5 | 23.0 | 43.7 | 1.34 |
| sgr-combined | 32.1 | 23.3 | 43.7 | 1.36 |
| sgr-combined-bg | 31.3 | 23.3 | 43.6 | 1.39 |
| cjk-terminal-input | 2464.6 | 13.8 | 4.0 | 0.00 |

当前状态：large-mixed、PowerShell、cjk 已不慢于 OpenConsole；long-line 和 SGR 仍慢于 OpenConsole，继续优化中。
