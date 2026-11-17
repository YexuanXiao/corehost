# corehost2 vs OpenConsole：conpty_bench 性能对比

测试日期：2026-11-17  
测试机：当前 `C:\` 工作机，Windows Terminal 1.24 环境  
基准工具：`conpty_bench.exe`（SHA-256 `06298CC2226ACA114BCA64ECCA7C1A028321D927EADC54602D713C7A2B607D64`）

## 被测二进制

| | OpenConsole | corehost2 |
|---|---|---|
| 路径 | `C:\1\OpenConsole.exe` | `build-nolog\Release\corehost2.exe` |
| 版本 | 1.26.2611.09003 / 1.26.261109003-preview | 本地 Release 无日志构建 |
| 大小 | 1,091,424 B | 485,376 B |
| SHA-256 | `7E04EC7E4691ABF5308831D531CC2B7B8AC61E2B2F3EA52B51BDA810DDF7FF6A` | `3748B8C4B3DDA19B7BB052A21D7A21DB50245C37DBEBBEDE2AA5012FE25A94C7` |

> 后续为 libunicode 增加 `/Gw` 数据 COMDAT 折叠，并移除 libcorehost 的系统 ICU 依赖：corehost2 从 4,855,296 B 降到 485,376 B，corehost 为 570,368 B。以上性能数据来自同一代码在链接裁剪前的构建；折叠只移除未引用的 Unicode 名称表，不改变执行路径。

## 方法

- 24 个 `conpty_bench` 场景，每个场景各跑 3 次，取平均；同时保留 min/max。
- 使用 worker 模式，测试进程和场景环境一致：

```powershell
cd C:\1\bench-compare-ch2worker
.\conpty_bench.exe --worker corehost2 <scenario>

cd C:\1\bench-compare-ocworker
.\conpty_bench.exe --worker OpenConsole <scenario>
```

- 原始 144 条结果：`C:\1\bench-results\compare-workers-3x-final.txt`
- 机器可读汇总：[benchmark_openconsole_vs_corehost2.csv](benchmark_openconsole_vs_corehost2.csv)
- 绘图脚本：[benchmark_compare.py](benchmark_compare.py)

## 结论摘要

![corehost2 vs OpenConsole](benchmark_openconsole_vs_corehost2.png)

| 场景 | OpenConsole 平均（min-max） | corehost2 平均（min-max） | corehost2 / OpenConsole |
|---|---:|---:|---:|
| large-mixed | 324.7 ms（322.6-328.8） | 287.6 ms（285.1-292.2） | 0.886 |
| long-line | 938.5 ms（874.6-1,051.6） | 775.3 ms（769.5-786.7） | 0.826 |
| powershell-type | 1,858.9 ms（1,770.7-2,031.9） | 1,966.9 ms（1,878.8-2,063.5） | 1.058 |
| sgr-plain | 31.4 ms（31.3-31.7） | 33.0 ms（32.5-33.5） | 1.050 |
| cjk-terminal-input | 2,455.3 ms（2,446.6-2,471.9） | 160.0 ms（157.5-162.1） | 0.065 |

### 主要观察

1. **large-mixed：corehost2 快约 11.4%**  
   324.7 → 287.6 ms，ratio 0.886。

2. **long-line：corehost2 快约 17.4%**  
   938.5 → 775.3 ms，ratio 0.826。OpenConsole 三次波动较大（874.6-1051.6 ms）。

3. **powershell-type：corehost2 慢约 5.8%**  
   1,858.9 → 1,966.9 ms，ratio 1.058。  
   直接写入路径当前默认由宏关闭，因此输出为 2,301,364 B，比 OpenConsole 多 11.5%。

4. **sgr-plain：corehost2 慢约 5.0%**  
   31.4 → 33.0 ms，ratio 1.050。

5. **20 个 SGR 场景均值：corehost2 慢约 7.8%**  
   OpenConsole 33.14 ms，corehost2 35.72 ms，ratio 1.078。  
   SGR 场景 ratio 约 1.03-1.13。

6. **cjk-terminal-input：corehost2 快约 15.3 倍**  
   OpenConsole 2,455.3 ms，corehost2 160.0 ms。  
   仍是完整的 cooked line input / echo 语义，不是跳过输入处理。

7. **全部非 CJK 场景未加权均值：corehost2 略快约 1.1%**  
   OpenConsole 164.56 ms，corehost2 162.79 ms，ratio 0.989。  
   该均值被 large-mixed/long-line 主导，不能代替单场景结论。

## 输出字节数

| 场景 | OpenConsole | corehost2 | 差异 |
|---|---:|---:|---:|
| large-mixed | 25,228,948 | 25,228,948 | 0 |
| long-line | 16,777,377 | 16,777,376 | -1 B |
| powershell-type | 2,063,636 | 2,301,364 | +11.5% |
| sgr-plain | 4,256,878 | 4,256,879 | +1 B |
| cjk-terminal-input | 395,397 | 393,280 | -2,117 B（-0.54%） |

CJK 的输出字节数现在与 OpenConsole 基本一致，数据 marker 和读取结果正常。

## 输出字节变化分析

### 原理

CJK 场景的输入约 393,275 B。旧实现的 corehost2 输出 544,841 B，比 OpenConsole 多 149,443 B。

用 `COREHOST2_ECHO_STATS` 对一次 CJK 运行做了计数（51 次实时 echo）：

```text
live calls=51  units=324,615  bytes=544,130
```

典型一次 echo：

```text
units=6,760
期望新增 UTF-8 文本=8,190 B
实际 render_damage 输出=10,803 B
其中 ESC 序列仅 43 个（约 293 B）
```

### 根因

append-only 的实时 echo 也走通用 `vtserializer::render_damage()`。  
这个函数按 screen damaged cells 重绘整行，包含：

- 行首 CUP；
- 行尾空白 padding；
- CJK 宽字符经过 screen cell 布局后的空格填充；
- 最终 cursor CUP。

这些空白对“在行尾追加新字符”不是必要输出：终端本来就在行尾，新增文本直写即可。  
因此 +37.8% 不是有效显示内容，而是 renderer 重绘带来的冗余 padding，**不合理**。

### 修复

`echo_live` 的 append-only 路径改为：

```text
write_text -> 更新 screen
clear_damage
append 规范化后的新增 UTF-8 文本
```

编辑/覆盖/Backspace/方向键仍走 `redraw_line_echo()` + `render_damage()`，因为这时必须重绘已有 cells。

结果：

```text
旧：544,841 B，8.6 ms
新：393,988 B，6.4 ms
OpenConsole：395,398 B，2,426.7 ms
```

CJK 输出字节数从 +37.8% 恢复到 -0.36%，性能也恢复到约 6.4 ms。

## 注意事项

### PowerShell 场景：bench 修正 + scroll renderer 修复

之前的结果不成立，原因有两层：

1. **手工 `--worker` 时没有生成 fixture**  
   场景读取 `corehost-bench-realistic-type-vt.txt`。手工 worker 目录没有该文件，两边实际跑的是“文件不存在”错误路径，所以输出只有 720 B / 10,892 B。

2. **corehost2 的滚动重绘 bug**  
   fixture 正确后，旧 corehost2 输出 137,824,303 B，约为 OpenConsole 的 67 倍。原因是 `scroll_up` 把整屏标为 damaged，`session_loop` 每行调用 `render_damage()` 重绘整个 viewport。

已修正 benchmark：

- worker 在计时前生成固定的 2,063,574 B fixture；
- 两边执行同一 `Get-Content -TotalCount 12000` 命令。

已修正代码：

- `scroll_up` 不再 `mark_all_dirty()`，改为累计 pending scroll；
- renderer 先发 `CSI n S`，再只重绘新增/变化的行；
- `WriteConsole` 顺序文本改为直接输出实际消费的 UTF-8；
- 用 screen cursor / terminal cursor 跟踪，只在位置不一致时发 CUP 或 `\r`；
- `write_text` 仍负责更新 screen，cursor/scroll pending 同步消费；
- 不再为每个 WriteConsole 行输出 damage 行的 CUP/SGR/visibility。

结果：

```text
OpenConsole：2,063,636 B，1,748.5 ms
corehost2：  2,073,522 B，1,819.9 ms
```

输出只差 +0.48%，耗时慢约 4%。  
PowerShell prompt 的淡色重影在原版 OpenConsole 上也存在，不是本次 WriteConsole 路径引入的。

另外，`WriteConsole` 的零拷贝直接路径只允许默认 style；非默认颜色改回 renderer 输出，否则 PowerShell 错误文本会丢掉红色。实测该行为与真实 OpenConsole 一致（红色文字、黑色 console 背景）。

### 关闭 WriteConsole 直接路径的 A/B

PowerShell 12000 行 fixture，同一机器交替运行，每个 variant 各 10 次：

```text
直接路径: 2,073,521 B, mean 1,897.1 ms
纯 renderer: 2,301,364 B, mean 1,881.8 ms
```

关闭直接路径后耗时基本持平（差距在运行噪声内），但输出字节增加约 11%。  
直接路径的价值主要是减少 ConPTY 输出量，而不是稳定降低耗时。

直接路径内部又测试了每个 WriteConsole 后立即 flush vs 交给 session loop 批量 flush：

```text
immediate flush: mean 1,950.8 ms
deferred flush:  mean 1,935.1 ms
```

去掉每条 WriteConsole 的额外 flush 后约快 0.8%，因此保留 deferred flush。

### SGR 子集不是全面领先

20 个 SGR 场景平均 corehost2 慢约 7.8%，个别场景慢约 13%。  
差异只有约 1-3 ms，绝对量很小，但如果目标是“所有 SGR 场景都不慢于 OpenConsole”，当前还没有完全达到。

另外增加了 SGR style 结果缓存：以 `(style table, base style, base attributes, SGR payload)` 为键，缓存最近 4 个 `(style id, legacy attributes)` 结果。SGR 前缀/后缀交替重复时不再每次重建并 intern style。

A/B 对比（同一 SGR workload，旧 binary vs 新 binary，20 个场景各 3 次、交替 2 轮）：

```text
new / old mean ratio: 0.9495
```

端到端仍受 ConPTY 调度噪声影响，上表是旧一轮的 3 次均值；缓存本身主要降低重复 SGR 的 CPU 成本。

### CJK 输出字节数已修复

此前 corehost2 的 CJK 输出为 544,841 B，比 OpenConsole 多 149,443 B（+37.8%）。  
根因是 append-only 的实时 echo 也走通用 `render_damage()`，把整行 damaged cells 的空格 padding 一并输出。

修复后 append echo 只写新增文本；编辑/重绘仍走 renderer。本轮输出 393,280 B，与 OpenConsole 的 395,397 B 相差 -0.54%。  
**当前耗时 160.0 ms**，仍比 OpenConsole 的 2,455.3 ms 快约 15.3 倍，但相比当时修复后的 6.4 ms 有明显回退；该回退来自后续 raw buffer/输入事件重构，需要继续定位。

## 全部 24 个场景

| 场景 | OpenConsole ms | corehost2 ms | ratio | OpenConsole B | corehost2 B |
|---|---:|---:|---:|---:|---:|
| cjk-terminal-input | 2,455.3 | 160.0 | 0.065 | 395,397 | 393,280 |
| large-mixed-vt-cjk-output | 324.7 | 287.6 | 0.886 | 25,228,948 | 25,228,948 |
| long-line-three-vt-output | 938.5 | 775.3 | 0.826 | 16,777,377 | 16,777,376 |
| powershell-type-realistic-build-output | 1,858.9 | 1,966.9 | 1.058 | 2,063,636 | 2,301,364 |
| sgr-bg-16 | 33.2 | 36.2 | 1.092 | 4,254,994 | 4,254,994 |
| sgr-bg-256 | 33.2 | 35.1 | 1.056 | 4,258,829 | 4,258,828 |
| sgr-bg-bright | 33.5 | 36.2 | 1.080 | 4,258,440 | 4,258,438 |
| sgr-bg-rgb | 33.2 | 34.5 | 1.038 | 4,255,707 | 4,255,709 |
| sgr-blink | 33.7 | 35.9 | 1.066 | 4,258,828 | 4,258,829 |
| sgr-bold | 33.3 | 36.5 | 1.096 | 4,258,829 | 4,258,828 |
| sgr-combined | 33.2 | 34.5 | 1.041 | 4,251,809 | 4,251,809 |
| sgr-combined-bg | 32.5 | 36.2 | 1.113 | 4,255,058 | 4,255,060 |
| sgr-faint | 33.6 | 36.6 | 1.088 | 4,258,830 | 4,258,828 |
| sgr-fg-16 | 33.6 | 36.2 | 1.076 | 4,254,993 | 4,254,994 |
| sgr-fg-256 | 33.4 | 36.5 | 1.093 | 4,259,609 | 4,259,608 |
| sgr-fg-bright | 32.4 | 36.6 | 1.130 | 4,254,995 | 4,254,994 |
| sgr-fg-rgb | 31.9 | 35.0 | 1.095 | 4,259,218 | 4,259,220 |
| sgr-hidden | 33.0 | 35.4 | 1.072 | 4,258,829 | 4,258,830 |
| sgr-inverse | 34.2 | 36.8 | 1.074 | 4,258,829 | 4,258,828 |
| sgr-italic | 34.3 | 36.2 | 1.056 | 4,258,829 | 4,258,828 |
| sgr-plain | 31.4 | 33.0 | 1.050 | 4,256,878 | 4,256,879 |
| sgr-reset | 32.3 | 35.5 | 1.099 | 4,253,303 | 4,253,305 |
| sgr-strike | 33.5 | 35.7 | 1.067 | 4,258,829 | 4,258,830 |
| sgr-underline | 33.3 | 36.0 | 1.082 | 4,258,829 | 4,258,828 |

## 结论

- corehost2 在 **large-mixed** 上快约 11.4%，long-line 快约 17.4%，sgr-plain 慢约 5.0%。
- **PowerShell 慢约 5.8%**：直接路径默认关闭，输出字节多 11.5%。
- **SGR 子集整体慢约 7.8%**，没有做到全部场景领先。
- **cjk-terminal-input** 快约 15.3 倍，输出字节数与 OpenConsole 基本一致。
- 全部非 CJK 场景未加权均值 ratio 0.989；large-mixed/CJK 更快，SGR 子集仍有回退。
