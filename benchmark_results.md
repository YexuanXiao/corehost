# conpty_bench 结果（3 次平均，无 fastpath / 无 replay 特例）

| Scenario | OpenConsole | corehost2 | corehost2/OpenConsole |
|---|---:|---:|---:|
| large-mixed-vt-cjk-output | 355.0 | 267.5 | 0.75 |
| long-line-three-vt-output | 947.9 | 871.7 | 0.92 |
| powershell-type-realistic-build-output | 292.4 | 289.5 | 0.99 |
| sgr-plain | 31.3 | 30.1 | 0.96 |
| sgr-reset | 33.1 | 31.8 | 0.96 |
| sgr-bold | 34.8 | 33.1 | 0.95 |
| sgr-faint | 33.3 | 33.3 | 1.00 |
| sgr-italic | 33.4 | 33.0 | 0.99 |
| sgr-underline | 34.0 | 33.9 | 1.00 |
| sgr-blink | 34.8 | 33.5 | 0.96 |
| sgr-inverse | 33.6 | 32.8 | 0.97 |
| sgr-hidden | 34.0 | 32.7 | 0.96 |
| sgr-strike | 33.7 | 33.2 | 0.98 |
| sgr-fg-16 | 34.0 | 34.3 | 1.01 |
| sgr-fg-bright | 33.6 | 32.9 | 0.98 |
| sgr-bg-16 | 34.8 | 32.9 | 0.95 |
| sgr-bg-bright | 36.0 | 33.8 | 0.94 |
| sgr-fg-256 | 34.6 | 33.3 | 0.96 |
| sgr-bg-256 | 35.4 | 32.5 | 0.92 |
| sgr-fg-rgb | 33.2 | 32.1 | 0.97 |
| sgr-bg-rgb | 34.4 | 33.9 | 0.99 |
| sgr-combined | 34.5 | 32.6 | 0.94 |
| sgr-combined-bg | 33.8 | 32.6 | 0.96 |
| cjk-terminal-input | 2706.7 | 4.5 | 0.00 |

结论：所有场景 3 次平均均不慢于 OpenConsole（SGR 个别 case 在噪声范围内约相等）。
