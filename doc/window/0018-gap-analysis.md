# Window Mode 功能缺口与目标状态

基线提交：`793deef`（2026-11-27）；空闲渲染修复：`3f3bc24`；动画合成器等待：`4e09ee0`。

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

1. UIA 自动化测试已加入；屏幕阅读器实测仍未做，Invoke 仍不实现。
2. `wt_test_checklist.md` 仍大量未验收，不能把“功能存在”等同于“与 conhost/WT 行为一致”。

TSF 不实现 `ITextStoreACP`，原因见 `0015-ime-tsf.md`；这不是本轮阻塞项。
## 已完成

| 计划项 | 状态 | 证据 / 说明 |
|---|---|---|
| W0 文档与启动契约 | 已完成，文档需同步 | `0001`、`0002`、`0003`；本文修正过期状态 |
| W1 `corehostw` / `window_entry` | 已完成 | 单进程 ConDrv console，不使用 ConPTY/libcorepty/defterm |
| W2.1–W2.4 窗口/DPI/焦点/空闲 | 已完成 | `window_host`、`window_ui`；render 线程改为 damage/wake/stop 事件等待 |
| W3.1–W3.5 D3D12/DXGI/DComp | 已完成 | `libd3d12ui`、composition swapchain、transparent path |
| W3.6 device lost | 实现完成，待故障注入验证 | 检测 `DXGI_ERROR_DEVICE_REMOVED`/`RESET`/`HUNG`/`DRIVER_INTERNAL_ERROR`；0/500/2000/8000ms 重建，失败关闭窗口 |
| W4 基础 console 会话 | 已完成基础路径 | `window_console_session`，CONNECT/RAW_WRITE/RAW_READ/console API |
| W5 文本显示 | 已完成基础路径 | DirectWrite fallback、glyph atlas、SGR 16/256/RGB、bold/italic/underline/strike、emoji、box drawing |
| W6 键盘/鼠标/选择/剪贴板 | 已完成基础路径 | 键盘、SGR mouse、文本线性选择、双击/三击、复制/粘贴 |
| W6.4 TSF composition/candidate | 基础完成，范围已决定 | TSF context、composition 自绘、candidate 定位；`ITextStoreACP` 不实现 |
| W7 damage/scrollback | 已完成基础路径 | damage row、history、selection virtual rows |
| W8.1 滚动条 | 已完成 | hover/拖拽/轨道点击/滚轮/键盘滚动（用户确认当前行为可接受） |
| W8.2–W8.3 Sixel | 完成 | 解码、HLS/RGB、image fragment、独立纹理缓存；屏幕外释放 |
| W8.4 透明/Mica/Acrylic | 实现完成，待用户手动验收 | `window_config` + DWM |
| 新需求：UIA TextPattern | 已完成基础路径 | Document、TextPattern、TextRange、selection、find |
| 新需求：UIA ScrollPattern | 已完成 | 纵向滚动、百分比、view size |
| 新需求：UIA 文本/选择事件 | 已完成接线 | render worker 文本变化投递；选择/自动滚动触发 SelectionChanged |
| UIA 自动化 | 已完成基础验证 | `Window.UIA` 启动 corehostw，验证 Document/TextPattern 和文本内容 |
| 选择边缘自动滚动 | 已完成 | timer + edge test；100 行输出选择得到跨历史文本 |
| hyperlink / bell | 已完成 | hover 下划线、Ctrl+click；`FlashWindowEx` visual bell |

Sixel 验证：屏幕内图像仅首次上传纹理，后续光标闪烁/文字帧不重传；图像滚出屏幕后缓存
纹理数为 0。颜色 emoji 仍走原有 color atlas，不与图像纹理混用。

## 部分完成或未完成

### 已修复：空闲渲染（验收口径已确认）

`render_worker` 已改为等待 stop、damage、render wake 三个事件；首个循环强制采样一次。
光标闪烁使用 530ms 定时器；滚动条动画使用 `DCompositionWaitForCompositorClock`，按显示刷新率运行，支持高刷新率。

验收口径（用户确认）：

- **失焦**，或**光标不闪烁**时，GUI 线程应阻塞在事件上，CPU 为 0；
- **有焦点且光标闪烁**时，530ms 光标定时器必然唤醒线程，不要求 CPU 为 0；
- 有滚动条动画时由合成器时钟按显示刷新率驱动，动画结束回到事件阻塞；

本地 10 秒采样中，焦点内/失焦/失焦 resize 后的进程 CPU 增量均为 0（Get-Process
秒级精度），文本和 resize 后内容仍正常显示。

### P0：剩余

#### 1. UIA 验收与事件

已有：

- Document/TextPattern；
- ScrollPattern；
- TextChanged / TextSelectionChanged 事件投递；
- 自动化 `Window.UIA` 验证 Document/TextPattern。

仍缺：

- UIA 树/ScrollPattern/事件扩展测试；
- Narrator / Accessibility Insights / NVDA 实测；
- 文本变化事件是否需要 payload/notification 的产品决定；
- Invoke 未实现（终端窗口没有有意义的默认调用动作）。

### 已决定的 TSF 范围

TSF `ITextStoreACP` 已决定不实现；TSF message pump 也决定不需要。

当前窗口是 TUI 宿主，文本编辑仍属于 client；corehost 只负责 console input 和
composition 自绘。`ITfContextOwner` + `ITfContextOwnerCompositionSink` + `ITfTextEditSink`
已覆盖 Microsoft Pinyin 的基础路径，引入 `ITextStoreACP` 会把 console input、selection 和
TSF 文本存储耦合起来。

因此本轮不实现；只有需要第三方 TIP 直接编辑、复杂重组、日文/韩文 candidate 时才重新评估。
详见 `0015-ime-tsf.md`。

### P1：功能/一致性

#### 1. 宽度策略与 CJK/grapheme

- `ambiguous_is_wide` 默认 narrow；不再提供 wide 反馈二进制；
- WT 对 U+2500–259F 使用 terminal convention narrow，Unicode 官方 EAW 为 Ambiguous；
- 该差异记录在 https://github.com/microsoft/terminal/issues/20980；
- B6/B8/B9（CJK、combining、宽字符覆盖）仍未按用户要求验收。

#### 2. 主题与配置

- `scrollback_lines` 默认 3000、`initial/min grid`（默认 120x30 / 36x2）、`candidate_offset_cells` 默认 2、`hyperlink modifier`、字号和字体 fallback 列表、光标闪烁、选择自动滚动、鼠标滚轮、右击、复制换行、滚动条显示模式和 UIA/TSF 开关已接入；`bell_mode` 归 window；Sixel 不设上限且只缓存屏幕内图像；
- `window_config` 只有 C++ 构造，明确未实现配置文件加载；
- 只有 Campbell 调色板和深色标题；没有亮/暗主题切换、高对比度、系统主题跟随；
- `0001-plan.md` W8.5/W8.6 仍未完成。

#### 3. 模块边界

计划中的：

- `libuiinput`：不存在，键盘/鼠标/剪贴板仍在 `window_host`；
- `libsubwindow`：不存在，child 嵌入明确不做；
- `libuicolor`：没有独立模块，主题/颜色仍在 `libcoreui`。

`libcorewindow` 仍承担窗口、输入、选择、TSF 消息和 UIA，职责比 `0003-modules.md`
描述的更重。

#### 4. Checklist

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
- UIA 树/ScrollPattern/事件扩展；
- Sixel 滚动/resize 边界；
- 大图/长行压力测试。

## 建议顺序

1. Mica/Acrylic/透明度手动验收；
2. 配置项对象（见 `0019-config-inventory.md`）；
3. UIA 屏幕阅读器验收；
4. 宽度策略/CJK/grapheme；
5. checklist 剩余项。

## 相关文件

- `terminal-window-code-plan.md`
- `terminal-window-plan.md`
- `0001-plan.md`
- `0003-modules.md`
- `0012-selection.md`
- `0015-ime-tsf.md`
- `0017-current-state-plan.md`
- `0019-config-inventory.md`
- `STATUS.md`
- `wt_test_checklist.md`
