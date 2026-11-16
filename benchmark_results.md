# conpty_bench 结果（无 fastpath / 无 replay 特例）

| Scenario | OpenConsole | corehost2 | ratio |
|---|---:|---:|---:|
| large-mixed-vt-cjk-output | 330.9 | 276.5 | 0.84 |
| long-line-three-vt-output | 835.4 | 973.5 | 1.17 |
| powershell-type-realistic-build-output | 307.2 | 349.0 | 1.14 |
| sgr-plain | 30.3 | 36.4 | 1.20 |
| sgr-reset | 31.8 | 36.0 | 1.13 |
| sgr-bold | 89.0 | 38.9 | 0.44 |
| sgr-faint | 35.2 | 39.5 | 1.12 |
| sgr-italic | 34.5 | 39.1 | 1.13 |
| sgr-underline | 33.3 | 39.2 | 1.18 |
| sgr-blink | 34.7 | 39.3 | 1.13 |
| sgr-inverse | 34.0 | 41.0 | 1.20 |
| sgr-hidden | 35.2 | 37.7 | 1.07 |
| sgr-strike | 32.7 | 39.9 | 1.22 |
| sgr-fg-16 | 32.5 | 38.4 | 1.18 |
| sgr-fg-bright | 33.0 | 37.7 | 1.14 |
| sgr-bg-16 | 31.7 | 39.1 | 1.23 |
| sgr-bg-bright | 35.2 | 38.6 | 1.10 |
| sgr-fg-256 | 32.7 | 38.0 | 1.16 |
| sgr-bg-256 | 32.7 | 37.4 | 1.14 |
| sgr-fg-rgb | 31.2 | 36.7 | 1.18 |
| sgr-bg-rgb | 32.5 | 37.3 | 1.15 |
| sgr-combined | 32.1 | 38.2 | 1.19 |
| sgr-combined-bg | 31.3 | 37.0 | 1.18 |
| cjk-terminal-input | 2464.6 | 4.4 | 0.00 |

当前状态：large-mixed、PowerShell、cjk 接近/达到 OpenConsole；long-line、SGR 仍慢。
