# Window Mode 进度审查（2026-11-26）

> 后续更新：`libcoreui` 与 `libime` 已从 `libcorewindow` 拆出，见 [0014-module-split.md](0014-module-split.md)。本文“模块拆分未完成”部分反映审查当时状态。

## 结论

窗口终端已经达到可用的显示 MVP，但目标中仍有未完成项：

- TSF IME 尚未完整；
- `libcorewindow` 职责过重；
- 透明、Sixel、child 嵌入、标签/分屏、配置主题未实现；
- checklist 中 CJK/emoji/combining 和部分输入/压力项未验收。

## 已完成

- `corehostw` / `window_entry` / 进程内 ConDrv console server；
- 不依赖 ConPTY、defterm、libcorepty；
- D3D12 + DXGI composition swapchain + DirectComposition；
- DirectWrite 文本、glyph atlas、颜色 emoji、显式字体 fallback + 系统 fallback；
- DPI 缩放、resize/reflow、光标；
- Campbell 主题、SGR 16/256/RGB、粗斜体/下划线/反显/删除线；
- 滚动条 thumb/箭头/hover/拖动/轨道点击/滚轮；
- grapheme cluster 选择、Shift 单击扩展、双击选词、三击选行；
- scrollback 选择/复制；
- `Ctrl+Shift+C/V` 纯文本剪贴板；
- Win32 鼠标 reporting，Vim `set mouse=a` 可点击定位；
- ConDrv input handle wait event 修复；
- DEC line drawing 单 cell 修复；
- IME 候选窗位置修复，用户确认位置正确。

## 部分完成

### IME

- 默认 IMM32 路径可以输入中文；
- `ImmSetCompositionWindow` / `ImmSetCandidateWindow` 已定位到 client caret；
- TSF 仍是 opt-in scaffold：
  - 有 thread manager/document/context/owner/composition sink；
  - 没有 `ITextStoreACP`；
  - 没有 composition inline 绘制；
  - TSF pump 在无 composition 时会吞键，目前禁用；
  - `COREHOST_TSF` 默认关闭。

### 字体显示

- fallback 已实现；
- B6 CJK、B7 emoji、B8 combining、B9 宽字符覆盖尚未验收；
- 涉及 grapheme cluster，需用户确认后再测/改。

### 透明

- swapchain 为 premultiplied alpha；
- clear alpha 仍为 1.0；
- 无 DWM backdrop / 透明背景配置。

### 空闲与性能

- 渲染由 `DCompositionWaitForCompositorClock` 驱动；
- 无 damage 时不重建/提交；
- 但线程仍每帧唤醒，未达到“完全 idle 阻塞”。

## 未完成

### 模块拆分

当前实际：

```text
libcorewindow
  window_host.hpp       窗口、输入、选择、剪贴板、IME 消息、scrollbar 交互
  render_worker.hpp     渲染、quad、atlas 使用
  tsf_host.hpp          TSF
  console.hpp           console view 状态
```

计划中的模块没有建立：

- `libcoreui`；
- `libime`；
- `libuiinput`；
- `libuisixel`；
- `libsubwindow` / child 嵌入。

`libcorewindow` 同时拥有窗口、渲染、输入、IME、选择，违反单一职责。

### 功能

- child 窗口嵌入；
- Sixel；
- 透明 / backdrop；
- 配置文件、亮暗主题、高对比度切换；
- 标签页/分屏；
- 选择拖动边缘自动滚动；
- HTML/RTF 剪贴板；
- 完整 VT mouse 模式验收；
- `ITextStoreACP` TSF 路径；
- TSF composition inline 显示。

### Checklist

未验收/未完成：

- A4 分屏、A6 多标签、A7 启动目录；
- B6–B9 CJK/emoji/combining/宽字符覆盖；
- B17/B18 标题/hyperlink 完整验收；
- C1–C8、C10–C12 完整输入/粘贴/window event；
- D4–D9 长行、alternate、多标签、嵌套、退出码、ReadConsoleOutput；
- E7–E9；
- F1–F6 压力/回归；
- WARP golden / DComp / IME hardware 自动化测试。

### 文档

- `STATUS.md` 严重过期；
- 模块边界文档与代码不一致；
- TSF/IME 限制没有独立文档；
- 本次审查即本文件。

## 约束问题

- 计划禁止新增 lambda，TSF 接入使用了 lambda / `std::function`；
- 计划要求 TSF 使用 `ITextStoreACP`，当前没有；
- `libcorewindow` 职责过重；
- 共享代码中不能针对 window/defterm/libcorepty 做特判，目前未发现明显违规。

## 优先级

### P0

1. 继续 IME：确定默认 IMM32 路径是否继续保留，并补 TSF `ITextStoreACP` / composition；
2. 记录并验证候选窗/组合窗行为；
3. 合理拆分 `libcorewindow`。

### P1

4. 透明、配置主题；
5. Sixel；
6. child 嵌入；
7. checklist 剩余项。

### P2

8. 标签/分屏、压力测试、自动化 UI 测试。

## 测试状态

```text
100% tests passed, 0 tests failed out of 12
```

## 相关文件

- `terminal-window-code-plan.md`
- `terminal-window-plan.md`
- `wt_test_checklist.md`
- `doc/window/STATUS.md`
- `doc/window/0003-modules.md`
