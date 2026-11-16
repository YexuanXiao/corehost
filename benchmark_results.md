# conpty_bench 结果

- OpenConsole: `C:\1\OpenConsole.exe`
- corehost: `build-nolog\Release\corehost.exe`（Release，`COREHOST_DISABLE_LOG=ON`）
- corehost2: `build-nolog\Release\corehost2.exe`（Release，`COREHOST_DISABLE_LOG=ON`）
- 运行方式: `conpty_bench --worker <label>`（绕过 parent worker 临时目录 bug）
- 时间单位: ms；ratio = corehost2 / OpenConsole

| Scenario | OpenConsole | corehost | corehost2 | ratio |
|---|---:|---:|---:|---:|
| large-mixed-vt-cjk-output | 304.9 | 183.3 | 340.7 | 1.12 |
| long-line-three-vt-output | 837.1 | 906.8 | 1636.4 | 1.95 |
| powershell-type-realistic-build-output | 275.0 | 262.7 | 339.9 | 1.24 |
| sgr-plain | 28.2 | 19.1 | 146.7 | 5.21 |
| sgr-reset | 30.9 | 21.3 | 145.1 | 4.69 |
| sgr-bold | 30.7 | 22.1 | 144.6 | 4.71 |
| sgr-faint | 31.7 | 22.2 | 141.8 | 4.47 |
| sgr-italic | 30.2 | 21.0 | 144.1 | 4.77 |
| sgr-underline | 31.7 | 22.4 | 141.8 | 4.48 |
| sgr-blink | 32.0 | 21.9 | 140.9 | 4.40 |
| sgr-inverse | 31.4 | 22.5 | 141.1 | 4.49 |
| sgr-hidden | 30.1 | 20.8 | 142.3 | 4.72 |
| sgr-strike | 31.3 | 22.3 | 142.4 | 4.55 |
| sgr-fg-16 | 30.5 | 21.9 | 141.4 | 4.63 |
| sgr-fg-bright | 31.3 | 23.2 | 140.8 | 4.50 |
| sgr-bg-16 | 29.8 | 21.7 | 145.0 | 4.87 |
| sgr-bg-bright | 31.4 | 22.4 | 141.4 | 4.50 |
| sgr-fg-256 | 30.6 | 23.6 | 134.4 | 4.40 |
| sgr-bg-256 | 30.4 | 23.5 | 137.7 | 4.52 |
| sgr-fg-rgb | 29.4 | 21.9 | 129.3 | 4.40 |
| sgr-bg-rgb | 30.5 | 22.1 | 131.0 | 4.30 |
| sgr-combined | 31.9 | 23.0 | 125.9 | 3.95 |
| sgr-combined-bg | 28.4 | 22.8 | 125.6 | 4.43 |
| cjk-terminal-input | 2333.9 | 4.7 | 394.8 | 0.17 |

说明: corehost/corehost2 的 SGR 输出字节数约为 OpenConsole 的一半，主要受终端渲染策略影响；cjk-terminal-input 的 corehost 输出只有 4096 字节（无完整回显），corehost2 修复后输出 914 字节并正常完成。
