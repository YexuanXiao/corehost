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
- 垂直滚动条：hover、thumb 拖拽、轨道点击、滚轮、键盘滚动；
- 线性文本选择、Shift 扩展、双击选词、三击选行、边缘自动滚动；
- 纯文本剪贴板、右键粘贴、VT SGR mouse；
- TSF composition 自绘、candidate 定位（`ITextStoreACP` 已决定不实现）；
- Sixel 解码、HSB/RGB 调色板、RGBA 渲染；
- hyperlink hover/Ctrl+click、visual bell；
- `window_config`（C++ 构造）：palette、字体、backdrop、opacity、dark mode、exit policy；
- Mica/Acrylic、layered opacity（实现完成，待用户手动验收）；
- UI Automation：Document/TextPattern/TextRange/ScrollPattern，文本变化和选择变化事件；
- `ambiguous_is_wide` 默认 narrow；wide 反馈二进制 `corehostw_wide.exe`；
- 12/12 自动化测试通过。

## 关键未完成

1. **空闲渲染不满足硬要求**：`render_worker` 仍使用
   `DCompositionWaitForCompositorClock`，不是零 CPU 的 damage/stop 事件阻塞。
2. **TSF 范围决定**：不实现 `ITextStoreACP`；复杂 IME/TIP、日文/韩文未验收。
3. **D3D12 device lost 未处理**：无 device removed 检测和资源重建。
4. **Sixel 性能**：图像每次绘制重新写 color atlas/上传纹理。
5. **UIA 自动化/屏幕阅读器验收**：仅手动 PowerShell UIA 验证；Invoke 明确不做。
6. **CJK/combining/宽字符覆盖**：B6/B8/B9 仍未按用户要求验收。
7. **主题/配置**：无配置文件加载、无亮暗主题切换/高对比度。
8. **checklist 大量未验收**，不能等同于 WT/conhost 行为一致。

## 下一步

按 [0018-gap-analysis.md](0018-gap-analysis.md) 的顺序：

1. 空闲渲染；
2. device lost；
3. Sixel 缓存；
4. Mica/Acrylic/透明度手动验收；
5. 配置项对象；
6. UIA 自动化；
7. 宽度/CJK/grapheme；
8. checklist。
