# corehost2 vs OpenConsole：conpty_bench 性能对比

测试日期：2026-11-17  
测试机：当前 `C:\` 工作机，Windows Terminal 1.24 环境  
基准工具：`conpty_bench.exe`（SHA-256 `A32E43B1631F643B412652EB3CC3DF82D53F3BE31804AF51800373D4E4CF263F`）

## 被测二进制

| | OpenConsole | corehost2 |
|---|---|---|
| 路径 | `C:\1\OpenConsole.exe` | `build-nolog\Release\corehost2.exe` |
| 版本 | 1.26.2611.09003 / 1.26.261109003-preview | 本地 Release 无日志构建 |
| 大小 | 1,091,424 B | 4,855,296 B |
| SHA-256 | `7E04EC7E4691ABF5308831D531CC2B7B8AC61E2B2F3EA52B51BDA810DDF7FF6A` | `246301904C6E519DA45170EADC62DD098CB4406B7F5D6FFFAAAD4A43CCE0BFE9` |

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
| large-mixed | 337.2 ms（321.6-348.7） | **269.1 ms**（260.8-281.4） | **0.798** |
| long-line | 869.8 ms（806.2-993.9） | **855.8 ms**（833.6-875.2） | **0.984** |
| powershell-type | 290.7 ms（283.2-303.3） | **285.5 ms**（283.1-287.0） | **0.982*** |
| sgr-plain | 31.5 ms（31.2-31.9） | **31.4 ms**（30.6-31.9） | **0.996** |
| cjk-terminal-input | 2,426.7 ms（2,416.4-2,446.0） | **6.9 ms**（6.4-7.2） | **0.003** |

\* PowerShell 场景两边输出字节数不一致，见下方注意事项；该行只比较耗时。

### 主要观察

1. **large-mixed：corehost2 快约 20%**  
   337.2 → 269.1 ms，ratio 0.798。

2. **long-line：corehost2 略快约 1.6%**  
   869.8 → 855.8 ms，ratio 0.984。差距很小，接近噪声范围。

3. **sgr-plain：基本持平**  
   31.5 → 31.4 ms，ratio 0.996。

4. **20 个 SGR 场景均值：corehost2 慢约 3%**  
   OpenConsole 33.06 ms，corehost2 34.06 ms，ratio 1.030。  
   各 SGR 场景 ratio 约 0.95-1.08；不是所有 SGR 都慢，但整体略偏慢。

5. **cjk-terminal-input：corehost2 快约 353 倍**  
   OpenConsole 2,426.7 ms，corehost2 6.9 ms。  
   这仍然包含真实的 cooked line input / echo 语义，不是靠跳过输入处理得到的。

6. **全部非 CJK 场景的未加权均值：corehost2 快约 3.1%**  
   OpenConsole 93.87 ms，corehost2 90.94 ms，ratio 0.969。  
   这个均值被 large-mixed 等长场景主导，不能代替每个场景的结论。

## 输出字节数

| 场景 | OpenConsole | corehost2 | 差异 |
|---|---:|---:|---:|
| large-mixed | 25,228,947 | 25,228,949 | +2 B |
| long-line | 16,777,377 | 16,777,377 | 0 |
| powershell-type | 720 | 10,883 | 工作量不等价，见注意事项 |
| sgr-plain | 4,256,879 | 4,256,877 | -2 B |
| cjk-terminal-input | 395,398 | 393,988 | -1,410 B（-0.36%） |

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
新：393,988 B，6.9 ms
OpenConsole：395,398 B，2,426.7 ms
```

CJK 输出字节数从 +37.8% 恢复到 -0.36%，性能也恢复到约 6.9 ms。

## 注意事项

### PowerShell 场景不能直接比较“工作量”

worker 模式下：

- OpenConsole 输出 720 B；
- corehost2 输出 10,883 B。

两者耗时接近，但输出规模相差约 15 倍，说明该场景在当前 host 组合下没有产生等价的 PowerShell 输出。  
该行只能作为“该 worker 场景的耗时参考”，不能单独作为 PowerShell 综合性能结论。需要单独构造一个输出内容和 marker 都固定的 PowerShell 场景再复核。

### SGR 子集不是全面领先

20 个 SGR 场景平均 corehost2 慢约 3%，个别场景慢 5-8%。  
差异只有约 1-3 ms，绝对量很小，但如果目标是“所有 SGR 场景都不慢于 OpenConsole”，当前还没有完全达到。

### CJK 输出字节数已修复

此前 corehost2 的 CJK 输出为 544,841 B，比 OpenConsole 多 149,443 B（+37.8%）。  
根因是 append-only 的实时 echo 也走通用 `render_damage()`，把整行 damaged cells 的空格 padding 一并输出。

修复后 append echo 只写新增文本；编辑/重绘仍走 renderer。当前输出 393,988 B，与 OpenConsole 的 395,398 B 相差 -0.36%。

## 全部 24 个场景

| 场景 | OpenConsole ms | corehost2 ms | ratio | OpenConsole B | corehost2 B |
|---|---:|---:|---:|---:|---:|
| cjk-terminal-input | 2,426.7 | 6.9 | 0.003 | 395,398 | 393,988 |
| large-mixed-vt-cjk-output | 337.2 | 269.1 | 0.798 | 25,228,947 | 25,228,949 |
| long-line-three-vt-output | 869.8 | 855.8 | 0.984 | 16,777,377 | 16,777,377 |
| powershell-type-realistic-build-output | 290.7 | 285.5 | 0.982 | 720 | 10,883 |
| sgr-bg-16 | 33.1 | 35.3 | 1.067 | 4,254,994 | 4,254,994 |
| sgr-bg-256 | 33.0 | 33.4 | 1.014 | 4,258,829 | 4,258,829 |
| sgr-bg-bright | 33.5 | 34.7 | 1.034 | 4,258,439 | 4,258,439 |
| sgr-bg-rgb | 32.7 | 33.8 | 1.032 | 4,255,709 | 4,255,707 |
| sgr-blink | 32.8 | 34.2 | 1.041 | 4,258,829 | 4,258,829 |
| sgr-bold | 32.5 | 35.2 | 1.081 | 4,258,829 | 4,258,829 |
| sgr-combined | 32.3 | 34.0 | 1.052 | 4,251,809 | 4,251,809 |
| sgr-combined-bg | 34.9 | 33.3 | 0.952 | 4,255,057 | 4,255,057 |
| sgr-faint | 33.8 | 35.9 | 1.062 | 4,258,829 | 4,258,827 |
| sgr-fg-16 | 32.7 | 35.0 | 1.073 | 4,254,994 | 4,254,994 |
| sgr-fg-256 | 32.6 | 34.0 | 1.045 | 4,259,609 | 4,259,609 |
| sgr-fg-bright | 32.9 | 33.9 | 1.033 | 4,254,994 | 4,254,994 |
| sgr-fg-rgb | 32.3 | 33.6 | 1.039 | 4,259,219 | 4,259,219 |
| sgr-hidden | 34.1 | 34.9 | 1.021 | 4,258,829 | 4,258,827 |
| sgr-inverse | 33.6 | 34.4 | 1.022 | 4,258,829 | 4,258,831 |
| sgr-italic | 32.8 | 34.3 | 1.044 | 4,258,829 | 4,258,827 |
| sgr-plain | 31.5 | 31.4 | 0.996 | 4,256,879 | 4,256,877 |
| sgr-reset | 31.7 | 32.6 | 1.027 | 4,253,304 | 4,253,304 |
| sgr-strike | 34.2 | 33.8 | 0.988 | 4,258,829 | 4,258,829 |
| sgr-underline | 34.0 | 33.7 | 0.991 | 4,258,829 | 4,258,827 |

## 结论

- corehost2 在 **large-mixed** 上明显领先（约 20%）。
- **long-line / sgr-plain / PowerShell 耗时** 接近，属于同一水平；long-line 和 PowerShell 仅小幅领先。
- **SGR 子集整体略慢约 3%**，没有做到全部场景领先。
- **cjk-terminal-input** 有数量级优势（约 353 倍），输出字节数与 OpenConsole 基本一致。
- 因此，更准确的说法是：**核心渲染/输入场景整体不慢于 OpenConsole，且 large-mixed/CJK 明显更快；SGR 子集仍有小幅回退。**
