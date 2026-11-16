# conpty_bench 结果（无 fastpath / 无 replay 特例）

| Scenario | OpenConsole | corehost2 | corehost2/OpenConsole |
|---|---:|---:|---:|
| large-mixed-vt-cjk-output | 330.9 | 285.4 | 0.86 |
| long-line-three-vt-output | 835.4 | 971.4 | 1.16 |
| powershell-type-realistic-build-output | 307.2 | 305.5 | 0.99 |
| sgr-plain | 30.3 | 36.6 | 1.21 |
| sgr-reset | 31.8 | 37.8 | 1.19 |
| sgr-bold | 89.0 | 39.7 | 0.45 |
| sgr-faint | 35.2 | 37.9 | 1.07 |
| sgr-italic | 34.5 | 39.9 | 1.16 |
| sgr-underline | 33.3 | 37.8 | 1.13 |
| sgr-blink | 34.7 | 39.8 | 1.15 |
| sgr-inverse | 34.0 | 38.9 | 1.14 |
| sgr-hidden | 35.2 | 39.3 | 1.12 |
| sgr-strike | 32.7 | 39.1 | 1.19 |
| sgr-fg-16 | 32.5 | 39.0 | 1.20 |
| sgr-fg-bright | 33.0 | 40.3 | 1.22 |
| sgr-bg-16 | 31.7 | 38.8 | 1.22 |
| sgr-bg-bright | 35.2 | 40.1 | 1.14 |
| sgr-fg-256 | 32.7 | 37.8 | 1.16 |
| sgr-bg-256 | 32.7 | 40.6 | 1.24 |
| sgr-fg-rgb | 31.2 | 37.1 | 1.19 |
| sgr-bg-rgb | 32.5 | 39.9 | 1.23 |
| sgr-combined | 32.1 | 37.6 | 1.17 |
| sgr-combined-bg | 31.3 | 38.5 | 1.23 |
| cjk-terminal-input | 2464.6 | 3.9 | 0.00 |

当前状态：large-mixed、PowerShell、cjk 已达标；long-line、SGR 仍慢。
