# Window Mode 功能缺口与目标状态

基线提交：`793deef`（2026-11-27）。

本文按 `terminal-window-code-plan.md`、`terminal-window-plan.md`、`0001-plan.md` 的 W0–W9
阶段和 `wt_test_checklist.md` 对照当前代码，区分：

- 已完成；
- 部分完成；
- 明确不做；
- 目标未完成。

## 结论

窗口终端已经具备可用的显示、输入、选择、滚动、Sixel、IME 和 UIA 文本接口，但
**还不能认为 `0001-plan.md` 的完成定义已经满足**。

最关键的未完成项：

1. W2.4/W9.1：空闲时渲染线程仍由 `DCompositionWaitForCompositorClock` 唤醒，没有达到
   “无消息时 GUI 线程完全停止、CPU 0%”的硬要求。
2. W6.4：TSF 仍不是完整的 `ITextStoreACP` 实现，只有 context owner/read-only property
   加 composition sink；输入法和复杂编辑行为仍需硬件验收。
3. W3.6：没有 D3D12 device removed/lost 的检测和重建路径。
4. Sixel 图像每次绘制都会重新写入 color atlas 并上传纹理，静态大图性能不合格。
5. UIA 已提供 TextPattern/ScrollPattern/事件，但还没有自动化 UIA 测试和屏幕阅读器验收；
   没有 Invoke（终端窗口没有有意义的默认调用动作）。
6. `wt_test_checklist.md` 仍大量未验收，不能把“功能存在”等同于“与 conhost/WT 行为一致”。

## 已完成

| 计划项 | 状态 | 证据 / 说明 |
|---|---|---|
| W0 文档与启动契约 | 已完成，文档需同步 | `0001`、`0002`、`0003`；本文修正过期状态 |
| W1 `corehostw` / `window_entry` | 已完成 | 单进程 ConDrv console，不使用 ConPTY/libcorepty/defterm |
| W2.1–W2.3 窗口、DPI、尺寸、焦点 | 已完成 | `window_host`、`window_ui`；PerMonitorV2 |
| W3.1–W3.5 D3D12/DXGI/DComp | 已完成 | `libd3d12ui`、composition swapchain、transparent path |
| W4 基础 console 会话 | 已完成基础路径 | `window_console_session`，CONNECT/RAW_WRITE/RAW_READ/console API |
| W5 文本显示 | 已完成基础路径 | DirectWrite fallback、glyph atlas、SGR 16/256/RGB、bold/italic/underline/strike、emoji、box drawing |
| W6 键盘/鼠标/选择/剪贴板 | 已完成基础路径 | 键盘、SGR mouse、文本线性选择、双击/三击、复制/粘贴 |
| W6.4 TSF composition/candidate | 部分完成 | TSF context、composition 自绘、candidate 定位；无 `ITextStoreACP` |
| W7 damage/scrollback | 已完成基础路径 | damage row、history、selection virtual rows |
| W8.1 滚动条 | 已完成 | hover/拖拽/轨道点击/滚轮/键盘滚动 |
| W8.2–W8.3 Sixel | 已完成功能，性能待修 | 解码、HLS/RGB、image fragment、RGBA quad |
| W8.4 透明/Mica/Acrylic | 已完成 | `window_config` + DWM |
| 新需求：UIA TextPattern | 已完成基础路径 | Document、TextPattern、TextRange、selection、find |
| 新需求：UIA ScrollPattern | 已完成 | 纵向滚动、百分比、view size |
| 新需求：UIA 文本/选择事件 | 已完成接线 | render worker 文本变化投递；选择/自动滚动触发 SelectionChanged |
| 选择边缘自动滚动 | 已完成 | timer + edge test；100 行输出选择得到跨历史文本 |
| hyperlink / bell | 已完成 | hover 下划线、Ctrl+click；`FlashWindowEx` visual bell |

## 部分完成或未完成

### P0：与硬要求直接冲突

#### 1. 空闲渲染

代码现状：

```cpp
HANDLE waits[] = {stop_event_.get()};
DCompositionWaitForCompositorClock(1, waits, INFINITE);
```

`render_worker` 仍按合成器时钟唤醒。虽然只有 `changed` 时才构建 quad/present，但线程并非
阻塞在 damage/stop 事件上，不满足 W2.4、W9.1 的零 CPU 目标。

需要：

- 增加 render wake event；
- 有 damage 时唤醒；
- 光标闪烁/滚动条动画才使用定时器；
- 无 damage、无动画时阻塞在事件上。

#### 2. TSF `ITextStoreACP`

当前 `libime/tsf_host.hpp` 只实现：

- `ITfContextOwner`
- `ITfContextOwnerCompositionSink`
- `ITfTextEditSink`
- context read-only property 枚举

没有完整的 `ITextStoreACP`，也没有把 selection/文本存储暴露给 TSF。微软拼音基础输入可用，
但复杂候选、重组、第三方 TIP、日文/韩文行为没有验收。

#### 3. D3D12 设备丢失

`libd3d12ui` 没有 `GetDeviceRemovedReason`、device removed 检测、资源重建或 swapchain
重建路径。驱动更新、TDR、显示器切换后窗口可能直接失效。

#### 4. Sixel 图像缓存

当前 image fragment 每帧重新执行 `color_atlas_.add_image`，重新上传 RGBA 纹理。静态图像
没有持久缓存，大图会持续消耗 CPU/GPU 带宽。

#### 5. UIA 验收与事件

已有：

- Document/TextPattern；
- ScrollPattern；
- TextChanged / TextSelectionChanged 事件投递；
- 手动 PowerShell UIA 验证。

仍缺：

- UIA 自动化测试（树、TextPattern、ScrollPattern、事件）；
- Narrator / Accessibility Insights / NVDA 实测；
- 文本变化事件是否需要 payload/notification 的产品决定；
- Invoke 未实现（终端窗口没有有意义的默认调用动作）。

### P1：功能/一致性

#### 6. 宽度策略与 CJK/grapheme

- `ambiguous_is_wide` 默认 narrow；
- `corehostw_wide.exe` 是临时反馈构建；
- WT 对 U+2500–259F 使用 terminal convention narrow，Unicode 官方 EAW 为 Ambiguous；
- 该差异记录在 https://github.com/microsoft/terminal/issues/20980；
- B6/B8/B9（CJK、combining、宽字符覆盖）仍未按用户要求验收。

#### 7. 主题与配置

- `window_config` 只有 C++ 构造，明确未实现配置文件加载；
- 只有 Campbell 调色板和深色标题；没有亮/暗主题切换、高对比度、系统主题跟随；
- `0001-plan.md` W8.5/W8.6 仍未完成。

#### 8. 模块边界

计划中的：

- `libuiinput`：不存在，键盘/鼠标/剪贴板仍在 `window_host`；
- `libsubwindow`：不存在，child 嵌入明确不做；
- `libuicolor`：没有独立模块，主题/颜色仍在 `libcoreui`。

`libcorewindow` 仍承担窗口、输入、选择、TSF 消息和 UIA，职责比 `0003-modules.md`
描述的更重。

#### 9. Checklist

`wt_test_checklist.md` 大部分仍未验收，尤其是：

- C3–C7 组合键、F1–F12、Tab；
- C9 mouse 全模式；
- C11 window size event；
- D2/D3/D5/D7/D8/D9；
- E1/E2/E4/E8/E9；
- F1–F6 压力；
- B6/B8/B9 CJK/grapheme；
- A5 关闭语义（用户已经确认 `:wq` 和 Ctrl+Q 路径，仍需形成验收记录）。

`wt-display-check.md`、`STATUS.md`、`0017` 中的部分描述已过期，本文为准。

#### 9. 滚动条键盘快捷键

代码只有已滚动或按下 Shift 时才把 Home/End/PgUp/PgDn/方向键用于滚动：

```cpp
if (lines != 0 && (offset != 0 || shift))
```

这与 `terminal-window-plan.md` 中“Home/End/PgUp/PgDn/方向键滚动”的完整快捷键描述
不完全一致。当前行为优先保证未滚动时按键仍交给 client，方向键不会被终端吞掉；需要
用户确认目标快捷键语义后再调整。
### P2：非目标 / 后续

明确不做：

- tab、split pane、多 session UI；
- child HWND 嵌入；
- HTML/RTF 剪贴板；
- 搜索/查找；
- 非纯文本剪贴板；
- 当前阶段完整 console API 覆盖验证。

后续可做：

- WARP golden / D3D12 自动化截图；
- UIA 自动化；
- Sixel 滚动/resize 边界；
- 大图/长行压力测试。

## 建议顺序

1. W2.4/W9.1 空闲渲染（硬要求）；
2. W3.6 device lost；
3. W6.4 `ITextStoreACP` 与 IME 硬件验收；
4. Sixel 图像缓存；
5. UIA 自动化与屏幕阅读器验收；
6. 宽度策略/CJK/grapheme；
7. 主题/配置；
8. checklist 剩余项。

## 相关文件

- `terminal-window-code-plan.md`
- `terminal-window-plan.md`
- `0001-plan.md`
- `0003-modules.md`
- `0012-selection.md`
- `0015-ime-tsf.md`
- `0017-current-state-plan.md`
- `STATUS.md`
- `wt_test_checklist.md`
