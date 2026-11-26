# Window Mode 当前状态与实施计划（2026-11-26）

## 范围修正

明确不做：

- 分屏、多窗格；
- 多标签页；
- child HWND 嵌入；
- HTML/RTF 剪贴板；
- 搜索/查找；
- 非纯文本剪贴板；
- 当前阶段不验证完整 console API 覆盖；
- 不集中补测试，测试跟随功能点；
- CJK/Emoji/grapheme 验收保持低优先级。

> 2026-11-27 更新：当前完整缺口以 [0018-gap-analysis.md](0018-gap-analysis.md) 为准。
> 当前状态：
> `render_worker` 已改为 stop/damage/wake 事件等待；失焦或光标不闪烁时 0 CPU，聚焦闪烁允许 530ms 定时器；滚动条动画用合成器时钟。
> UIA TextPattern/ScrollPattern/事件、线性选择、UIA provider 已加入；Sixel 性能问题仍存在。

## 当前状态


已完成：

- `window_ui` 独立 UI 线程；
- D3D12 + DirectComposition 渲染；
- DirectWrite 字体 fallback、glyph atlas、颜色 emoji；
- DPI、resize/reflow、光标形状/闪烁；
- Campbell 主题、SGR 16/256/RGB、粗斜体/下划线/反显/删除线；
- 滚动条拖动、轨道点击、滚轮、hover；
- grapheme cluster 选择、Shift 单击、双击选词、三击选行；
- scrollback 选择、复制、右键粘贴；
- TSF IME：composition 自绘、candidate、finalized/composing 分离；
- IMM32 `WM_IME_REQUEST` 兼容；
- 窗口图标、标题；
- 字体选项 C++ 接口。

已知缺口（2026-11-27）：

- TSF 不实现 `ITextStoreACP`（决定记录在 0015），复杂 IME/TIP 未验收；
- TSF 不实现 `ITextStoreACP`（决定记录在 0015），复杂 IME/TIP 未验收；
- D3D12 device lost 无恢复；
- Sixel 图像没有持久缓存；
- UIA 只有手动验证，没有自动化/屏幕阅读器验收；
- 主题只有 Campbell，没有配置文件加载、亮暗切换、高对比度；
- CJK/combining/宽字符覆盖与 checklist 大量项仍未验收；
- `window_host` 仍包含输入/选择/IME/UIA，`libuiinput` 等计划模块未建立。

## 计划状态

- 配置项候选清单见 [0019-config-inventory.md](0019-config-inventory.md)；

- `window_config`、DWM 外观、选择/自动滚动、hyperlink、bell、Sixel 已完成基础路径；
- 渲染空闲已完成（验收口径见 0018）；
- IME 复审仅完成 TSF 基础路径；
- 配置文件和主题切换仍不做；
- UIA 是计划外新增能力，见 [0018-gap-analysis.md](0018-gap-analysis.md)。

## 原计划（历史）

### 1. window_config（已完成）

新增顶层 `libconfig/`，分为 `console_config` 和 `window_config`：

```cpp
struct console_config
{
    std::size_t scrollback_lines{3000};
};

struct window_config
{
    console_palette palette = console_palette::campbell();
    bell_mode bell = bell_mode::visual;
    uitext::text_options text_options{};
    backdrop_mode backdrop = backdrop_mode::none;
    std::uint8_t opacity = 255;
    bool dark_mode = true;
    client_exit_policy exit_policy = client_exit_policy::always_exit;
    std::int32_t candidate_offset_cells{2};
};
```

- 先只由 C++ 构造，不读取配置文件；
- `window_entry(title, command_line, const console_config &, const window_config &)`；
- `window_ui`、`window_host`、`render_worker` 均保存配置引用；
- `window_ui` 和 `window_host` 接收同一份配置；
- 默认值保持当前行为。

验收：修改配置可改变字体、主题、backdrop、透明度，不需要改窗口代码。

### 2. DWM 外观（已完成）

- `DwmSetWindowAttribute(DWMWA_SYSTEMBACKDROP_TYPE)`；
- Mica：`DWMSBT_MAINWINDOW`；
- 亚克力：`DWMSBT_TRANSIENTWINDOW`；
- `DWMWA_USE_IMMERSIVE_DARK_MODE`；
- 自定义透明度：`WS_EX_LAYERED` + `SetLayeredWindowAttributes`；
- Mica/亚克力时默认背景 cell 透明，保留 ANSI 背景色。

验收：三种模式可切换，无黑边，窗口仍可交互。

### 3. 文本选择修复与边缘自动滚动（已完成）

- 复现并修复当前选择回归；
- 拖动到 client 上/下边缘时按行自动滚动；
- 水平越界时 clamp 到首/末列；
- 自动滚动只在拖动期间运行，停止后释放 timer。

验收：backwards/forwards、多行、scrollback、边缘拖动均可选择。

### 4. Hyperlink（已完成）

- 从 cell hyperlink 元数据解析 URL；
- hover 时记录鼠标下 cell 的 hyperlink；
- Ctrl+click 使用 `ShellExecuteW` 打开；
- hover 时在 cell 下方绘制下划线；
- 不改变选择/鼠标 reporting 行为。

### 5. Bell（已完成）

- 解析 BEL 后在 UI 线程触发；
- 默认 visual bell：窗口边框/标题短暂提示或闪烁；
- 不做音频前先确认行为；
- 限频，避免高频 BEL 卡顿。

### 6. Sixel（基础完成，性能未完成）

- 新增 `libuisixel`：Sixel 解码为 RGBA8；
- palette 解码、raster attributes、repeat；
- `libscreen::image_fragment` 保存像素数据或句柄；
- render_worker 创建/更新图片纹理；
- 支持 image_fragment 的 below/replace/above 层。

验收：`\x1bPq...` sixel 图片显示，滚动/resize/销毁正确。

### 7. 渲染空闲（已完成）

- 渲染线程不再固定等 `DCompositionWaitForCompositorClock`；
- 增加 wake event；
- 无 damage、无动画、无光标闪烁时阻塞在 wake event；
- 有 damage 时主动唤醒并 present；
- 光标闪烁使用 timer，只在需要时唤醒；
- 目标：失焦或光标不闪烁时 GPU 不提交、线程 CPU 0%；聚焦闪烁时允许 530ms 定时器。

### 8. IME 复审（部分完成）

- 基于当前 TSF 实现重新核对：
  - candidate 位置；
  - composition 生命周期；
  - `IMR_QUERYCHARPOSITION`/`IMR_RECONVERTSTRING`；
  - InputScope；
  - 日文/韩文行为。
- 只补实际缺口，不为了“完整”添加无用代码。

## 顺序

1. window_config；
2. DWM 外观；
3. 选择修复 + 自动滚动；
4. hyperlink；
5. bell；
6. 渲染空闲；
7. Sixel；
8. IME 复审。

## 进展记录（2026-11-26）

- `window_config` 已接入 `window_entry`、`window_ui`、`window_host`、`render_worker`。
- DWM Mica/亚克力/自定义 layered opacity 已实现并实测 Mica、opacity。
- 选择边缘自动滚动已实现。
- hyperlink hover 下划线、Ctrl+click 打开已实现；hover 诊断日志确认命中。
- BEL 解析、state 计数、`FlashWindowEx` visual bell 已实现。
- 渲染已改为 stop/damage/wake 事件等待；失焦或光标不闪烁时 0 CPU，聚焦闪烁允许 530ms 定时器；滚动条动画用合成器时钟。
- 当前测试：`build-ninja` 12/12 通过。
- Sixel 解码、image fragment、color atlas RGBA quad 已完成；64x64 彩色 Sixel 实测显示。
- 已知性能问题：image 每次 render 都会重新写入 color atlas/上传纹理，后续需缓存。
- selection 已改为线性 text 模式，支持边缘自动滚动，复制按行裁剪。