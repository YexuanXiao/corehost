# conpty_bench 结果（3 次平均，无 fastpath / 无 replay 特例）

> 最新完整对比见 [benchmark_openconsole_vs_corehost2.md](benchmark_openconsole_vs_corehost2.md)。  
> 本文保留的是更早一轮的结果，cjk 输出字节与当前实现不同。

| Scenario | OpenConsole | corehost2 | ratio |
|---|---:|---:|---:|
| large-mixed-vt-cjk-output | 355.1 | 267.5 | 0.75 |
| long-line-three-vt-output | 947.9 | 871.7 | 0.92 |
| powershell-type-realistic | 292.4 | 289.5 | 0.99 |
| sgr-plain | 31.3 | 30.1 | 0.96 |
| cjk-terminal-input | 2706.7 | 13-20 | ~0.006 |

cjk-terminal-input 在输入语义修复后从约 4 ms 变为 13-20 ms；输出约 394239 B，接近 OpenConsole 的 395398 B。OpenConsole 仍慢约 2.4 s，主要耗时在逐字符 cooked line input/echo 路径。

其余场景仍不慢于 OpenConsole。
