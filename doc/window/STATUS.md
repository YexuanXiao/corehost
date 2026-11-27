# Window Mode 状态

> 当前完整缺口分析见 [0018-gap-analysis.md](0018-gap-analysis.md)。
> 配置项清单见 [0019-config-inventory.md](0019-config-inventory.md)。
> 本文只保留高层状态，不再重复每个功能的细节。

## 范围

`corehostw` 是单进程、无 ConPTY、无 defterm/libcorepty 的窗口终端：

- `window_entry` 创建 ConDrv `\Server`、进程内 console session、client 和窗口；
- `corehostw` 与 `corehost` 共享 `main.cpp`，用 `COREHOST_WINDOW` 切换入口；
- 只支持 Windows 11 22000+。

明确不做：tab、split pane、多 session、child HWND、HTML/RTF 剪贴板、搜索/查找、
非纯文本剪贴板。

## 已完成

- Win32 top-level 窗口、DPI、resize/reflow、焦点；
- D3D12 + DXGI composition swapchain + DirectComposition；
- DirectWrite fallback、glyph atlas、颜色 emoji、SGR 16/256/RGB、粗斜体/下划线/反显/删除线；
- 光标形状/闪烁；
- D3D12 device lost：检测、resource/swapchain 重建、0/500ms/2s/8s 重试，失败关闭窗口；
- 空闲渲染改为 stop/damage/wake 事件等待；失焦或光标不闪烁时 0 CPU，聚焦闪烁时允许 530ms 定时器；滚动条动画用合成器时钟按刷新率驱动；
- 垂直滚动条：hover、thumb 拖拽、轨道点击、滚轮、键盘滚动；
- 线性文本选择、Shift 扩展、双击选词、三击选行、边缘自动滚动；
- 纯文本剪贴板、右键粘贴、VT SGR mouse；剪贴板 read/write 默认 deny，需显式 allow；
- TSF composition 自绘、candidate 定位（`ITextStoreACP` 已决定不实现；不需要独立 message pump）；
- Sixel 解码、HSB/RGB 调色板、RGBA 渲染；屏幕内图像纹理缓存，内容不变不重传；
- hyperlink hover/Ctrl+click、visual bell；
- `window_config`（C++ 构造）：palette、字体、backdrop、opacity、dark mode、exit policy；
- 完整配置 schema `corehost_config` 已建立并由 `window_entry` 接收；窗口/渲染层始终传递完整 config 对象，未实现项仅存储；
- 配置决定/接入：scrollback 默认 3000、启动 120x30（console_config）、最小 36x2、candidate_offset_cells 默认 2、hyperlink modifier、字号/fallback/可变字体轴（完整 font_config，DirectWrite typographic collection）、光标闪烁、选择自动滚动、鼠标滚轮、右击、复制换行、光标样式覆盖、dark/light 调色板、窗口 shadow/opacity/topmost/launch（含 fullscreen）、title.force_corehost、reduce_motion、color emoji、ligatures、滚动条显示模式、UIA/TSF 开关（完整 accessibility/ime 分组）和剪贴板 deny/allow 策略已接入；bell 归 `corehost_config.bell`；Sixel 不设尺寸上限且只缓存屏幕内图像；
- Mica/Acrylic、layered opacity（实现完成，待用户手动验收）；
- UI Automation：Document/TextPattern/TextRange/ScrollPattern，文本变化和选择变化事件；自动化测试 `Window.UIA`（Document/TextPattern/ScrollPattern）；
- `ambiguous_is_wide` 默认 narrow；不提供 wide 反馈二进制；
- 14/14 自动化测试通过，新增 `Window.UIA` 和 `Config.Defaults`。

## 关键未完成

1. **TSF 范围决定**：不实现 `ITextStoreACP`；复杂 IME/TIP、日文/韩文未验收。
2. **UIA 屏幕阅读器验收**：自动化已覆盖 Document/TextPattern，屏幕阅读器仍未实测；Invoke 明确不做。
3. **CJK/combining/宽字符覆盖**：B6/B8/B9 仍未按用户要求验收。
4. **主题/配置**：无配置文件加载、无亮暗主题切换/高对比度。
5. **checklist 大量未验收**，不能等同于 WT/conhost 行为一致。

## 下一步

按 [0018-gap-analysis.md](0018-gap-analysis.md) 的顺序：

1. Mica/Acrylic/透明度手动验收；
2. 配置项对象；
3. UIA 屏幕阅读器验收；
4. 宽度/CJK/grapheme；
5. checklist。
