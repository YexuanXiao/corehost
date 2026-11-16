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
| SHA-256 | `7E04EC7E4691ABF5308831D531CC2B7B8AC61E2B2F3EA52B51BDA810DDF7FF6A` | `B97A72D9AE25B212EAEC31B442A6C8C82797F0F1A5EF813F391EF6CAFE271754` |

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
| large-mixed | 337.2 ms（321.6-348.7） | **280.0 ms**（256.8-316.9） | **0.830** |
| long-line | 869.8 ms（806.2-993.9） | **826.6 ms**（799.2-866.3） | **0.950** |
| powershell-type | 290.7 ms（283.2-303.3） | 296.1 ms（279.8-323.9） | 1.018* |
| sgr-plain | 31.5 ms（31.2-31.9） | **30.9 ms**（30.3-31.8） | **0.980** |
| cjk-terminal-input | 2,426.7 ms（2,416.4-2,446.0） | **6.4 ms**（6.4-6.5） | **0.003** |

\* PowerShell 场景两边输出字节数不一致，见下方注意事项；该行只比较耗时。

### 主要观察

1. **large-mixed：corehost2 快约 17%**  
   337.2 → 280.0 ms，ratio 0.830。

2. **long-line：corehost2 快约 5%**  
   869.8 → 826.6 ms，ratio 0.950。三次波动较大（799-866 ms）。

3. **sgr-plain：corehost2 快约 2%**  
   31.5 → 30.9 ms，ratio 0.980。

4. **20 个 SGR 场景均值：corehost2 慢约 2.3%**  
   OpenConsole 33.06 ms，corehost2 33.83 ms，ratio 1.023。  
   各 SGR 场景 ratio 约 0.95-1.07；不是所有 SGR 都慢，但整体略偏慢。

5. **cjk-terminal-input：corehost2 快约 378 倍**  
   OpenConsole 2,426.7 ms，corehost2 6.4 ms。  
   这仍然包含真实的 cooked line input / echo 语义，不是靠跳过输入处理得到的。

6. **全部非 CJK 场景的未加权均值：corehost2 快约 3.7%**  
   OpenConsole 93.87 ms，corehost2 90.40 ms，ratio 0.963。  
   这个均值被 large-mixed 等长场景主导，不能代替每个场景的结论。

## 输出字节数

| 场景 | OpenConsole | corehost2 | 差异 |
|---|---:|---:|---:|
| large-mixed | 25,228,948 | 25,228,949 | +1 B |
| long-line | 16,777,377 | 16,777,377 | 0 |
| powershell-type | 720 | 10,892 | 工作量不等价，见注意事项 |
| sgr-plain | 4,256,879 | 4,256,878 | -1 B |
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
新：393,988 B，6.4 ms
OpenConsole：395,398 B，2,426.7 ms
```

CJK 输出字节数从 +37.8% 恢复到 -0.36%，性能也恢复到约 6.4 ms。

## 注意事项

### PowerShell 场景不能直接比较“工作量”

worker 模式下：

- OpenConsole 输出 720 B；
- corehost2 输出 10,892 B。

两者耗时接近，但输出规模相差约 15 倍，说明该场景在当前 host 组合下没有产生等价的 PowerShell 输出。  
该行只能作为“该 worker 场景的耗时参考”，不能单独作为 PowerShell 综合性能结论。需要单独构造一个输出内容和 marker 都固定的 PowerShell 场景再复核。

### SGR 子集不是全面领先

20 个 SGR 场景平均 corehost2 慢约 2.3%，个别场景慢约 7%。  
差异只有约 1-3 ms，绝对量很小，但如果目标是“所有 SGR 场景都不慢于 OpenConsole”，当前还没有完全达到。

### CJK 输出字节数已修复

此前 corehost2 的 CJK 输出为 544,841 B，比 OpenConsole 多 149,443 B（+37.8%）。  
根因是 append-only 的实时 echo 也走通用 `render_damage()`，把整行 damaged cells 的空格 padding 一并输出。

修复后 append echo 只写新增文本；编辑/重绘仍走 renderer。当前输出 393,988 B，与 OpenConsole 的 395,398 B 相差 -0.36%。

## 全部 24 个场景

| 场景 | OpenConsole ms | corehost2 ms | ratio | OpenConsole B | corehost2 B |
|---|---:|---:|---:|---:|---:|
| cjk-terminal-input | 2,426.7 | 6.4 | 0.003 | 395,398 | 393,988 |
| large-mixed-vt-cjk-output | 337.2 | 280.0 | 0.830 | 25,228,948 | 25,228,949 |
| long-line-three-vt-output | 869.8 | 826.6 | 0.950 | 16,777,377 | 16,777,377 |
| powershell-type-realistic-build-output | 290.7 | 296.1 | 1.018 | 720 | 10,892 |
| sgr-bg-16 | 33.1 | 33.5 | 1.011 | 4,254,994 | 4,254,993 |
| sgr-bg-256 | 33.0 | 34.5 | 1.048 | 4,258,829 | 4,258,828 |
| sgr-bg-bright | 33.5 | 33.8 | 1.008 | 4,258,439 | 4,258,440 |
| sgr-bg-rgb | 32.7 | 34.6 | 1.058 | 4,255,708 | 4,255,709 |
| sgr-blink | 32.8 | 33.3 | 1.015 | 4,258,829 | 4,258,830 |
| sgr-bold | 32.5 | 34.4 | 1.058 | 4,258,829 | 4,258,830 |
| sgr-combined | 32.3 | 34.5 | 1.067 | 4,251,809 | 4,251,809 |
| sgr-combined-bg | 34.9 | 34.1 | 0.976 | 4,255,059 | 4,255,059 |
| sgr-faint | 33.8 | 35.2 | 1.042 | 4,258,829 | 4,258,829 |
| sgr-fg-16 | 32.7 | 34.8 | 1.065 | 4,254,994 | 4,254,993 |
| sgr-fg-256 | 32.6 | 33.7 | 1.034 | 4,259,609 | 4,259,608 |
| sgr-fg-bright | 32.9 | 35.1 | 1.070 | 4,254,993 | 4,254,994 |
| sgr-fg-rgb | 32.3 | 33.3 | 1.029 | 4,259,219 | 4,259,218 |
| sgr-hidden | 34.1 | 33.7 | 0.988 | 4,258,829 | 4,258,829 |
| sgr-inverse | 33.6 | 33.9 | 1.009 | 4,258,828 | 4,258,829 |
| sgr-italic | 32.8 | 33.8 | 1.030 | 4,258,829 | 4,258,829 |
| sgr-plain | 31.5 | 30.9 | 0.980 | 4,256,879 | 4,256,878 |
| sgr-reset | 31.7 | 32.3 | 1.017 | 4,253,303 | 4,253,305 |
| sgr-strike | 34.2 | 33.5 | 0.979 | 4,258,829 | 4,258,829 |
| sgr-underline | 34.0 | 33.6 | 0.987 | 4,258,829 | 4,258,828 |

## 结论

- corehost2 在 **large-mixed** 上明显领先（约 17%）。
- **long-line / sgr-plain / PowerShell 耗时** 接近，属于同一水平；long-line 和 PowerShell 仅小幅领先。
- **SGR 子集整体慢约 2.3%**，没有做到全部场景领先。
- **cjk-terminal-input** 有数量级优势（约 378 倍），输出字节数与 OpenConsole 基本一致。
- 因此，更准确的说法是：**核心渲染/输入场景整体不慢于 OpenConsole，且 large-mixed/CJK 明显更快；SGR 子集仍有小幅回退。**
