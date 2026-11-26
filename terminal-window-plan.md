# CoreHost 终端图形界面实施计划

> 实现状态、已知缺口和优先级见 [doc/window/0018-gap-analysis.md](doc/window/0018-gap-analysis.md)。
> 配置项清单见 [doc/window/0019-config-inventory.md](doc/window/0019-config-inventory.md)。
> TSF 决策：当前不实现 `ITextStoreACP`；下文提到它的内容是初始选项，不是当前目标。

## 1. 范围与约束

本计划描述 **CoreHost 终端窗口的图形界面实现**。

硬约束：

1. 直接使用 corehost 现有代码；
2. 不使用 WinUI、XAML、WPF、MFC、Qt、Dear ImGui、Direct2D 等 UI 框架；
3. 只使用 Windows 内置核心组件：Win32、D3D12、DXGI、DirectWrite、DirectComposition、TSF；
4. 不使用 WIC；图像只通过 sixel 协议绘制，不解码图片文件；
5. IME 只使用 TSF，不使用 IMM32，也不存在 IMM32 fallback；
6. 空闲状态不轮询：没有新消息、没有待处理事件、没有动画时，GUI 线程完全停止，CPU 占用为 0；
7. 不内置 tab、split pane 或多 session UI。
8. 只需要支持Windows11（22000起）。

当前实现状态：见 [doc/window/0018-gap-analysis.md](doc/window/0018-gap-analysis.md)。
配置项清单：见 [doc/window/0019-config-inventory.md](doc/window/0019-config-inventory.md)。

明确不做：

- tab、split pane、多 session；
- child HWND / `libsubwindow` 嵌入；
- HTML/RTF 剪贴板；
- 搜索/查找；
- 非纯文本剪贴板。
本文不设计 C++ 公共 API，只说明实现方法、Win32/Windows API 清单，以及这些 API 之间的组合关系。

---

## 2. 模块划分与顶级文件夹

每个模块只承担一个内聚职责；模块之间可以交互，但不把多个领域塞进同一个模块。

每一个模块下可以有多个类。

```text
corehost/          # 项目根目录，不是 corehost 二进制目录
├── libcoreui/     # 终端渲染合成：background、selection、glyph、cursor、Sixel 合成
├── libcorewindow/ # top-level Win32 窗口程序
├── libd3d12ui/    # D3D12 device/queue/swapchain/descriptor/fence
├── libuitext/     # DWrite 字体、fallback、shaping、glyph raster
├── libime/        # TSF IME，仅 TSF
├── libuisixel/    # sixel 协议解码与 RGBA 位图生成
├── libscrollbar/  # 垂直滚动条几何、状态、命中测试
└── libconfig/     # console/window 配置对象
```

### `libcoreui`

只负责“把 corehost model 变成一帧 D3D12 绘制命令”：

- 读取 corehost 现有 cell/style/damage；
- 组织 background/selection/glyph/decoration/cursor/sixel overlay/scrollbar 的 pass；
- 调用 `libuitext`、`libuisixel`、`libscrollbar` 提供的数据/几何。

`libcoreui` 可以把绘制命令录制到调用方已有的 D3D12 command list 和目标纹理，因此可以嵌入任何已有 DX12 窗口。

### `libcorewindow`

只负责 top-level Win32 宿主：

- 注册、创建 top-level 窗口；
- 消息循环；
- 通过 `libd3d12ui` 建立并持有本窗口的 device/queue/swapchain；
- 调用 `libcoreui` 渲染；
- 支持云母/亚克力；
- 处理 DPI、显示器和焦点；
- 处理键盘、鼠标、滚轮、剪贴板消息和选择，不再拆出 `libuiinput` 分类。

### `libsubwindow`（明确不做）

child HWND 嵌入不在当前范围；不建立 `libsubwindow`。窗口只做 top-level。

### `libd3d12ui`

只负责 D3D12/DXGI 基础设施：

- device、adapter、queue；
- command allocator/list；
- descriptor heap；
- swapchain（owned 模式）；
- fence、frame slot、上传 ring；
- resize、颜色空间设置、frame latency。

### `libuitext`

只负责 DirectWrite 文本：

- 字体集合和显式 fallback 列表；
- DWrite fallback builder；
- shaping；
- glyph 栅格化；
- CPU glyph atlas。

### `libime`

只负责 TSF：

- thread manager、document/context；
- composition（`ITextStoreACP` 已决定不实现）；
- candidate 定位。

### `libuisixel`

只负责 sixel：

- 解码 corehost 提供的 sixel 数据；
- 解析颜色寄存器、六像素竖带、透明色；
- 生成 RGBA8 位图。

### `libscrollbar`

只负责滚动条：

- 使用 DX12 实现
- 可作为一个库给任何其他软件使用；
- 命中测试；
- hover、拖拽、轨道点击；
- 生成轨道和 thumb 的 quad 几何；
- 动画；
- 使用 WinUI 的样式：窗口失焦时完全隐藏；窗口有焦点时显示为无背景小细条；焦点在滚动条区域时显示为完整的滚动条，并带有背景。

键盘快捷键：

Home：滚动到顶部
End：滚动到底部
PgUp：向上翻页
PgDn：向下翻页
↑：向上滚动一行
↓：向下滚动一行
Shift + 单击滚动条：滑块直接跳到单击的位置

### `libconfig`

只负责配置对象：

- `console_config`：scrollback 上限（默认 3000）、初始 grid、cursor 等；
- `window_config`：窗口、显示、主题、输入相关配置；`bell` 和 `candidate_offset_cells`（默认 2）都在这里；
- 配置清单见 [doc/window/0019-config-inventory.md](doc/window/0019-config-inventory.md)；
- 输入仍由 `libcorewindow` 处理，不再单独建立 `libuiinput` 模块。

---

## 3. 使用的 Windows API 清单

以下 Windows API 可以完成窗口、D3D12、DirectWrite、DirectComposition、TSF、Sixel 和滚动条功能。所有 API 都是 Windows 核心组件，不使用 UI 框架。

### 3.1 窗口、消息、DPI

| API | 用途 |
|---|---|
| `RegisterClassExW` | 注册 top-level/child window class |
| `CreateWindowExW` | 创建 top-level 或 child HWND |
| `DestroyWindow` | 销毁窗口 |
| `DefWindowProcW` | 默认消息处理 |
| `ShowWindow` | 显示窗口 |
| `SetWindowPos` | 移动、缩放、更新 Z 序 |
| `GetClientRect` | 获取客户区尺寸 |
| `GetWindowRect` | 获取屏幕矩形，用于 DComp visual 对齐 |
| `ClientToScreen` / `ScreenToClient` | 鼠标、IME、DComp 坐标转换 |
| `MapWindowPoints` | 父子窗口坐标转换 |
| `GetAncestor` | 找到 child 的 top-level HWND |
| `SetWindowSubclass` / `DefSubclassProc` | child 消息钩子 |
| `SetProcessDpiAwarenessContext` | Per-Monitor V2 DPI |
| `GetDpiForWindow` | 当前窗口 DPI |
| `AdjustWindowRectExForDpi` | 按 DPI 计算客户区 |
| `GetSystemMetricsForDpi` | 边框/标题栏尺寸 |
| `GetMessageW` | 阻塞等待消息；空闲时不轮询 |
| `TranslateMessage` | 生成字符消息 |
| `DispatchMessageW` | 分发消息 |
| `WaitMessage` | 没有消息时阻塞 |
| `MsgWaitForMultipleObjectsEx` | 同时等待窗口消息和 GPU/COM 事件，使用 `INFINITE` |
| `PostQuitMessage` | 退出消息循环 |
| `SendMessageW` | 同步窗口操作 |
| `BeginPaint` / `EndPaint` | `WM_PAINT` |
| `ValidateRect` | 标记客户区已绘制 |
| `SetCapture` / `ReleaseCapture` | 鼠标拖选、滚动条拖拽 |
| `TrackMouseEvent` | hover / `WM_MOUSELEAVE` |
| `OpenClipboard` / `GetClipboardData` / `SetClipboardData` / `EmptyClipboard` | 复制/粘贴 |
| `AddClipboardFormatListener` | 监听剪贴板变化 |

关键消息：

- `WM_SIZE`、`WM_MOVE`、`WM_DPICHANGED`、`WM_DISPLAYCHANGE`；
- `WM_THEMECHANGED`、`WM_SETTINGCHANGE`；
- `WM_ACTIVATE`、`WM_SETFOCUS`、`WM_KILLFOCUS`；
- `WM_MOUSEMOVE`、`WM_LBUTTONDOWN/UP`、`WM_RBUTTONDOWN/UP`、`WM_MBUTTONDOWN/UP`；
- `WM_MOUSEWHEEL`、`WM_MOUSEHWHEEL`；
- `WM_KEYDOWN/UP`、`WM_CHAR`、`WM_UNICHAR`；
- `WM_PAINT`、`WM_ERASEBKGND`、`WM_SETCURSOR`、`WM_CAPTURECHANGED`。

### 3.2 Direct3D 12

| API | 用途 |
|---|---|
| `D3D12CreateDevice` | 创建 D3D12 device |
| `D3D12GetDebugInterface` | Debug/Validation/DRED |
| `ID3D12Device::CreateCommandQueue` | direct queue |
| `ID3D12Device::CreateCommandAllocator` | 每帧 allocator |
| `ID3D12Device::CreateCommandList` | 录制命令 |
| `ID3D12Device::CreateRootSignature` | 根签名 |
| `ID3D12Device::CreateGraphicsPipelineState` | background/glyph/decoration/cursor/sixel/scrollbar PSO |
| `ID3D12Device::CreateDescriptorHeap` | RTV、SRV、sampler |
| `ID3D12Device::CreateCommittedResource` | backbuffer/atlas/RGBA texture/instance buffer |
| `ID3D12Device::CreateRenderTargetView` | backbuffer RTV |
| `ID3D12Device::CreateShaderResourceView` | glyph/color/sixel texture SRV |
| `ID3D12Device::CreateFence` | 帧同步 |
| `ID3D12GraphicsCommandList::SetDescriptorHeaps` | 绑定 descriptor |
| `ID3D12GraphicsCommandList::SetGraphicsRootSignature` | 设置根签名 |
| `ID3D12GraphicsCommandList::SetGraphicsRoot32BitConstants` | 颜色矩阵、viewport、透明度 |
| `ID3D12GraphicsCommandList::SetGraphicsRootDescriptorTable` | atlas/Sixel 纹理 |
| `ID3D12GraphicsCommandList::OMSetRenderTargets` | 设置目标 |
| `ID3D12GraphicsCommandList::RSSetViewports` | 视口 |
| `ID3D12GraphicsCommandList::RSSetScissorRects` | damage 裁剪 |
| `ID3D12GraphicsCommandList::IASetVertexBuffers` | glyph/quad/Sixel instance |
| `ID3D12GraphicsCommandList::IASetIndexBuffer` | quad 索引 |
| `ID3D12GraphicsCommandList::ResourceBarrier` | 纹理状态转换 |
| `ID3D12GraphicsCommandList::DrawInstanced` | 背景、选择、光标 |
| `ID3D12GraphicsCommandList::DrawIndexedInstanced` | glyph、装饰、Sixel、滚动条 |
| `ID3D12CommandQueue::ExecuteCommandLists` | 提交 |
| `ID3D12CommandQueue::Signal` | fence |
| `ID3D12Fence::SetEventOnCompletion` | GPU 等待 |
| `ID3D12Resource::Map` / `Unmap` | upload ring |
| `ID3D12Resource::GetGPUVirtualAddress` | 常量/实例 buffer |

### 3.3 DXGI

| API | 用途 |
|---|---|
| `CreateDXGIFactory2` | DXGI factory |
| `IDXGIFactory6::EnumAdapterByGpuPreference` | GPU 选择 |
| `IDXGIAdapter::EnumOutputs` | 显示器枚举 |
| `IDXGIFactory2::CreateSwapChainForComposition` | 透明/DComp 交换链 |
| `IDXGIFactory2::CreateSwapChainForHwnd` | 不透明 child/top-level 交换链 |
| `IDXGISwapChain::GetBuffer` | backbuffer |
| `IDXGISwapChain::Present` | 提交帧 |
| `IDXGISwapChain::ResizeBuffers` | 窗口缩放 |
| `IDXGISwapChain2::SetMaximumFrameLatency` | 限制排队帧 |
| `IDXGISwapChain2::GetFrameLatencyWaitableObject` | 帧同步 |

### 3.4 DirectComposition 与 DWM

| API | 用途 |
|---|---|
| `DCompositionCreateDevice3` | DComp device |
| `IDCompositionDevice::CreateTargetForHwnd` | 绑定 top-level HWND |
| `IDCompositionDevice::CreateVisual` | visual |
| `IDCompositionDevice::CreateRectangleClip` | child 裁剪 |
| `IDCompositionVisual::SetContent` | 设置 composition swapchain |
| `IDCompositionVisual2::SetOffsetX` / `SetOffsetY` | child 位置 |
| `IDCompositionVisual2::SetClip` | child 裁剪 |
| `IDCompositionVisual2::SetOpacity` | 整体透明度 |
| `IDCompositionDevice::Commit` | 提交合成变更 |
| `DCompositionWaitForCompositorClock` | 仅主动动画时等待；空闲不用 |
| `DwmSetWindowAttribute` | 暗色标题栏、圆角、边框、backdrop |
| `DwmExtendFrameIntoClientArea` | 可选 Mica/扩展边框 |

DWM 属性：

- `DWMWA_USE_IMMERSIVE_DARK_MODE`
- `DWMWA_WINDOW_CORNER_PREFERENCE`
- `DWMWA_BORDER_COLOR`
- `DWMWA_CAPTION_COLOR`
- `DWMWA_SYSTEMBACKDROP_TYPE`

### 3.5 DirectWrite

| API | 用途 |
|---|---|
| `DWriteCreateFactory` | 创建 `IDWriteFactory5` |
| `IDWriteFactory::GetSystemFontCollection` | 系统字体 |
| `IDWriteFontSetBuilder1::AddFontFile` | 私有字体 |
| `IDWriteFactory5::CreateFontCollectionFromFontSet` | 显式字体集合 |
| `IDWriteFontCollection::FindFamilyName` | 查找 family |
| `IDWriteFontFamily::GetFirstMatchingFont` | weight/style |
| `IDWriteFont::CreateFontFace` | font face |
| `IDWriteFontFace::GetGlyphIndices` | glyph indices |
| `IDWriteFontFace::GetDesignGlyphMetrics` | 度量 |
| `IDWriteTextAnalyzer::AnalyzeScript` | script itemization |
| `IDWriteTextAnalyzer::GetGlyphs` | GSUB/shaping |
| `IDWriteTextAnalyzer::GetGlyphPlacements` | GPOS/位置 |
| `IDWriteGlyphRunAnalysis::CreateAlphaTexture` | glyph alpha |
| `IDWriteFactory4::TranslateColorGlyphRun` | COLR/CPAL 彩色 glyph |
| `IDWriteColorGlyphRunEnumerator1::GetCurrentRun` | 彩色层 |
| `IDWriteTypography` / `DWRITE_FONT_FEATURE` | ligature/feature |

**Fallback 专用 API**：

| API | 用途 |
|---|---|
| `IDWriteFactory2::CreateFontFallbackBuilder` | 构建 fallback |
| `IDWriteFactory2::GetSystemFontFallback` | 系统自动 fallback |
| `IDWriteFontFallbackBuilder::AddMapping` | 把显式 family 列表加入 fallback |
| `IDWriteFontFallbackBuilder::AddMappings` | 追加系统 fallback 映射 |
| `IDWriteFontFallbackBuilder::CreateFontFallback` | 生成最终 fallback 对象 |
| `IDWriteFontFallback::MapCharacters` | 让 DWrite 决定 run 的字体 |

### 3.6 显示器配置

| API | 用途 |
|---|---|
| `GetDisplayConfigBufferSizes` | DisplayConfig 路径数量 |
| `QueryDisplayConfig` | 显示路径 |
| `DisplayConfigGetDeviceInfo` | advanced color / SDR white |
| `MonitorFromWindow` | 当前显示器 |
| `GetMonitorInfoW` | 显示器矩形 |
| `EnumDisplayMonitors` | 多显示器枚举 |

### 3.7 TSF：IME 唯一路径

| API | 用途 |
|---|---|
| `CoInitializeEx(COINIT_APARTMENTTHREADED)` | 初始化 UI 线程 COM/TSF |
| `CoCreateInstance(CLSID_TF_ThreadMgr, IID_ITfThreadMgr)` | TSF thread manager |
| `ITfThreadMgr::Activate` | 激活 TSF |
| `ITfThreadMgr::CreateDocumentMgr` | document manager |
| `ITfThreadMgr::AssociateFocus` | HWND 与 context 关联 |
| `ITfDocumentMgr::CreateContext` | 输入 context |
| `ITfDocumentMgr::Push` / `Pop` | 焦点文档栈 |
| `ITfContext::RequestEditSession` | 请求编辑 |
| `ITfEditSession::DoEditSession` | composition 编辑 |
| `ITfContextOwner` / `ITfContextOwnerCompositionSink` | 当前 composition 路径；不实现 ITextStoreACP |
| `ITfCompositionSink::OnCompositionTerminated` | composition 结束 |
| `ITfThreadMgrEventSink::OnSetFocus` | 焦点变化 |
| `ITfContextView::GetTextExt` / `GetScreenExt` | 预编辑/候选窗口定位 |
| `ITfUIElementSink` | IME UI 元素事件 |
| `ITfThreadMgr::AdviseSink(IID_ITfUIElementSink, ...)` | 订阅 IME UI 事件 |
| `CoUninitialize` | 清理 COM |

### 3.8 滚动条和 Sixel

滚动条没有专用 Win32 控件；使用以下消息和模块自己的几何：

- `WM_MOUSEMOVE`；
- `WM_LBUTTONDOWN` / `WM_LBUTTONUP`；
- `WM_MOUSEWHEEL`；
- `SetCapture` / `ReleaseCapture`；
- `TrackMouseEvent`。

`libuisixel` 解析 sixel 字节并生成 RGBA8 位图，`libd3d12ui` 负责上传纹理，`libcoreui` 负责绘制四边形。

---

## 4. API 组合关系

### 4.1 创建 top-level 透明窗口

```text
SetProcessDpiAwarenessContext
    ↓
RegisterClassExW
    ↓
CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP)
    ↓
CreateDXGIFactory2
    ↓
D3D12CreateDevice
    ↓
CreateCommandQueue / CreateSwapChainForComposition
    ↓
DCompositionCreateDevice3
    ↓
CreateTargetForHwnd(hwnd)
    ↓
CreateVisual / SetContent(swapchain)
    ↓
Commit
```

`WS_EX_NOREDIRECTIONBITMAP` 去掉 GDI 重定向表面；`CreateSwapChainForComposition` 提供 DComp 可用的交换链；`SetContent` 把它挂到窗口。

### 4.2 每帧渲染

```text
WM_PAINT / corehost model damage
    ↓
CommandAllocator::Reset
    ↓
CommandList::Reset
    ↓
ResourceBarrier(backbuffer -> RENDER_TARGET)
    ↓
OMSetRenderTargets
    ↓
background
    ↓
selection/search
    ↓
glyph
    ↓
decoration/cursor
    ↓
Sixel overlay
    ↓
scrollbar
    ↓
final color pass
    ↓
ResourceBarrier(backbuffer -> PRESENT)
    ↓
ExecuteCommandLists
    ↓
Signal(fence)
    ↓
Present
```

只有出现 damage、尺寸变化或颜色配置变化时才执行。

### 4.3 DirectWrite 到 D3D12

```text
corehost console model
    ↓
IDWriteFontFallback::MapCharacters
    ↓
IDWriteTextAnalyzer::GetGlyphs
    ↓
IDWriteTextAnalyzer::GetGlyphPlacements
    ↓
IDWriteGlyphRunAnalysis::CreateAlphaTexture
    ↓
CPU atlas / upload ring
    ↓
D3D12 texture
    ↓
glyph instance / DrawIndexedInstanced
```

DirectWrite 只负责 fallback、shaping 和 raster；D3D12 负责上传和最终绘制。

### 4.4 字体 fallback：先列表，后自动

```text
显式 family 列表
    ↓
IDWriteFontFallbackBuilder::AddMapping(
    full Unicode range,
    targetFamilyNames = 显式列表,
    custom font collection)
    ↓
IDWriteFactory2::GetSystemFontFallback
    ↓
IDWriteFontFallbackBuilder::AddMappings(systemFallback)
    ↓
CreateFontFallback
    ↓
IDWriteFontFallback::MapCharacters
```

顺序：

1. `AddMapping` 的显式 family 列表优先；
2. `AddMappings` 把 DWrite 系统自动 fallback 追加到后面；
3. `MapCharacters` 由 DWrite 选择字体。

### 4.5 子窗口嵌入（明确不做）

child HWND / `libsubwindow` 不在当前范围；只实现 top-level 窗口。原计划中的 child DComp、
焦点转发和宿主 device 复用不进入本轮实现，也不再作为验收项。
### 4.6 TSF IME

```text
CoInitializeEx
    ↓
CoCreateInstance(CLSID_TF_ThreadMgr)
    ↓
ITfThreadMgr::Activate
    ↓
CreateDocumentMgr / CreateContext
    ↓
ITfContextOwner + composition sink 提供 composition 文本
    ↓
ITfThreadMgr::AssociateFocus(hwnd, context)
    ↓
IME composition
    ↓
ITfContextView::GetTextExt 计算候选窗口位置
    ↓
libcoreui 绘制 composition overlay
```

不使用 `ImmGetContext`、`ImmSetCompositionWindow`、`ImmSetCandidateWindow`。TSF 初始化失败时禁用 IME，不切换 IMM32。

### 4.7 空闲状态

```text
没有 damage、没有输入、没有动画
    ↓
GetMessageW(..., INFINITE)
    ↓
线程完全阻塞，CPU = 0
```

需要同时等待 GPU/COM 事件时：

```text
MsgWaitForMultipleObjectsEx(
    handles, INFINITE, QS_ALLINPUT, MWMO_INPUTAVAILABLE)
```

规则：

- 不使用 `PeekMessage` 轮询；
- 不使用 `Sleep(0)`/`Sleep(1)` 渲染循环；
- 没有 damage 不调用 `Present`；
- 没有待提交帧时不等待 swapchain frame-latency waitable，避免被 DWM vsync 周期唤醒；
- `WM_PAINT` 用 `ValidateRect`/`BeginPaint`/`EndPaint` 结束，不持续 Invalidate；
- 仅在需要光标闪烁或动画时创建定时器；
- 失焦或关闭闪烁时取消定时器，回到完全阻塞。

严格满足“没有新消息就完全停止”：

- 无光标闪烁、无动画时，没有任何 timer；
- 有光标闪烁时，timer 本身就是一条新消息；关闭闪烁或失焦后立即停止。

### 4.8 DPI 与窗口尺寸

```text
WM_DPICHANGED
    ↓
GetDpiForWindow
    ↓
重新计算 cell metrics / glyph atlas
    ↓
AdjustWindowRectExForDpi
    ↓
ResizeBuffers / 重建 RTV / DComp visual offset
```

`WM_SIZE`、`WM_MOVE`、`WM_DISPLAYCHANGE` 触发相同的 swapchain/visual 更新。

当前实现状态：`render_worker` 仍使用 `DCompositionWaitForCompositorClock`，上述空闲目标
尚未完成，列为 P0；实现后必须用 CPU 测量和 Present 计数验收。
### 4.9 能力覆盖检查

| 功能 | API 组合 | 结论 |
|---|---|---|
| top-level 窗口 | `RegisterClassExW` + `CreateWindowExW` + `GetMessageW` | 足够 |
| 透明背景 | `WS_EX_NOREDIRECTIONBITMAP` + `CreateSwapChainForComposition` + DComp | 足够 |
| 子窗口嵌入 | `CreateWindowExW(WS_CHILD)` + child swapchain 或宿主 DComp target | 明确不做 |
| 文字 | DWrite fallback/shaping + glyph raster + D3D12 | 足够 |
| 字体 fallback | `IDWriteFontFallbackBuilder` + `MapCharacters` | 足够，列表优先、系统自动兜底 |
| IME | TSF + `ITfContextOwner` + `ITfCompositionSink` | 足够，无 IMM32；ITextStoreACP 不做 |
| Sixel | `libuisixel` 解析 + D3D12 texture | 足够，无 WIC |
| 滚动条 | Win32 鼠标消息 + `SetCapture` + quad draw | 足够 |
| 多显示器/DPI | `WM_DPICHANGED` + `GetDpiForWindow` + `IsCurrent` | 足够 |
| 空闲零 CPU | `GetMessageW(INFINITE)` / `MsgWaitForMultipleObjectsEx(INFINITE)` | 足够，无轮询 |
| 帧同步 | frame-latency waitable + fence | 足够 |
| 透明 child | 宿主顶层 DComp target + `SetClip`/`SetOffset` | 明确不做 |

---

### 4.10 D3D12 设备丢失

```text
Present/ExecuteCommandLists 返回 device removed
    ↓
GetDeviceRemovedReason
    ↓
停止 render worker / 释放 swapchain 与尺寸相关资源
    ↓
重新创建 device、queue、swapchain、RTV、DComp target
    ↓
重建 glyph/color atlas 和 Sixel 缓存
    ↓
强制全量 redraw
```

要求：

- 检测 `DXGI_ERROR_DEVICE_REMOVED` / `DXGI_ERROR_DEVICE_RESET`；
- 重建失败不能死循环，退化为错误窗口/退出；
- 与 resize、DPI、透明度路径共用资源重建函数；
- 自动化测试至少能注入 fake device-removed 结果。

---
## 5. 图形实现细节

### 5.1 窗口样式

Top-level：

- `WS_OVERLAPPEDWINDOW`；
- `WS_EX_NOREDIRECTIONBITMAP` 用于透明/DComp 路径；
- DWM 暗色标题栏、圆角、边框颜色；
- 标准非客户区，不实现自定义 caption。

Child：

- `WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN`；
- `WM_ERASEBKGND` 返回 1；
- `WM_PAINT` 不绘制 GDI，只验证区域；
- 父窗口负责 `TranslateMessage`/`DispatchMessage`；
- `WM_SETFOCUS`/`WM_KILLFOCUS` 管理焦点。

### 5.2 D3D12 资源

- SDR backbuffer：`R8G8B8A8_UNORM`；
- glyph atlas：`R8_UNORM`；
- color glyph atlas：`B8G8R8A8_UNORM`；
- Sixel：RGBA8 纹理；
- frame slot：3；
- descriptor heap：RTV、SRV、sampler 分离；
- 资源屏障按 pass 精确切换。

### 5.3 渲染 pass

1. 背景纯色/透明；
2. 选择高亮；
3. glyph；
4. underline/undercurl/strikethrough/overline；
5. cursor；
6. Sixel overlay；
7. 滚动条；
8. IME composition；
9. final color conversion/PQ。

颜色混合在线性空间，alpha 使用 premultiplied。

### 5.4 glyph atlas

- 每个 `(font face, pixel size, AA mode)` 一个 atlas；
- R8 atlas 2048×2048；
- 彩色 glyph 使用 BGRA atlas；
- shelf packing + LRU；
- `CreateAlphaTexture` 生成 alpha；
- `TranslateColorGlyphRun` 生成 COLR/CPAL 彩色层；
- 新 glyph 通过 upload ring 上传。

### 5.5 字体 fallback

- 显式 family 列表先加入 `IDWriteFontFallbackBuilder`；
- 系统自动 fallback 通过 `AddMappings` 追加；
- `MapCharacters` 由 DWrite 完成实际选择；
- 不手写 codepoint 循环；
- 无字体时由 DWrite 返回 missing glyph。

### 5.6 透明

- top-level DComp visual；
- swapchain `AlphaMode = PREMULTIPLIED`；

- 可选 DWM backdrop；
- 背景、文字、光标全部输出 premultiplied alpha；
- 不透明模式可走 `CreateSwapChainForHwnd`。

### 5.7 IME

- TSF only；
- `ITfContextOwner` + composition sink 管理 composition；
- `ITfContextView::GetTextExt` 计算候选窗口位置；
- 候选位置偏移来自 `window_config::candidate_offset_cells`，默认 2，按 cell 计算；
- composition overlay 由 D3D12 绘制；

- TSF 失败时禁用 IME，不 fallback IMM32。

### 5.8 Sixel

- `libuisixel` 解析 sixel：
  - 颜色寄存器；
  - 六像素竖带；
  - 透明色；
  - 重复/压缩；
- 生成 RGBA8 位图；
- `libd3d12ui` 上传纹理；
- `libcoreui` 按 corehost model 提供的位置/尺寸绘制四边形；
- 当前屏幕内容中的 Sixel 图像区域未变化时，不重新绘制、不重新上传 RGBA 纹理；
- 缓存键为 image id + pixel generation + source size + placement；
- 仅图像新增、像素变化、位置/尺寸变化、滚动导致可见区域变化时失效；
- 只缓存当前屏幕内可见图像，屏幕外图像不保留；
- 不设 Sixel 最大尺寸/像素数上限。

### 5.9 滚动条

`libscrollbar`：

- 根据 scrollback 总行数、可见行数、当前顶部行计算：
  - track；
  - thumb 位置；
  - thumb 长度；
- 鼠标：
  - `WM_LBUTTONDOWN` 开始拖拽；
  - `SetCapture`；
  - `WM_MOUSEMOVE` 更新；
  - `WM_LBUTTONUP` 结束；
  - `WM_MOUSEWHEEL` 行滚动；
  - `WM_MOUSELEAVE` 清除 hover；
- 生成 thumb/track 的 quad 几何；
- `libcoreui` 负责 draw。

### 5.10 配置文件（主题）切换

当前状态：只支持 C++ 构造；尚未实现配置文件加载。

计划：

- 使用私有窗口消息切换（不受 Windows 影响）；
- 支持亮/暗、高对比度黑/高对比度白；
- 动态切换；
- 配置字段见 [doc/window/0019-config-inventory.md](doc/window/0019-config-inventory.md)。

### 5.11 UI Automation

- 窗口根 provider：`IRawElementProviderSimple` / Fragment / FragmentRoot；
- `ITextProvider` + `ITextRangeProvider` 暴露终端文本、selection、find；
- `IScrollProvider` 只暴露纵向 scrollback；
- 文本变化和 selection 变化发 UIA 事件；
- 不实现 Invoke：终端窗口没有有意义的默认调用动作；
- provider/range 用 `winrt::implements` + `winrt::make`；
- UIA 调用可能来自其他线程，provider 读取 console 时必须持 `console_mutex`。

---

## 6. 实现顺序

### 阶段 0：窗口与清屏

- `RegisterClassExW`/`CreateWindowExW`；
- D3D12 device/queue/swapchain；
- DComp；
- `GetMessageW` 消息循环；
- 纯色清屏；
- 空闲无 GPU 提交。

验收：窗口打开，无输入时 GUI 线程 CPU 为 0。

### 阶段 1：DirectWrite 文本

- `IDWriteFactory5`；
- 字体集合；
- glyph shaping；
- alpha atlas；
- glyph pass；
- 字形缓存；
- DPI/cell metrics。

验收：ASCII/CJK 正确显示。

### 阶段 2：绑定 corehost model

- 直接读取 corehost 已有 cell/style/damage；
- 不复制 parser/model；
- damage 驱动局部重绘。

验收：corehost 输出实时显示，无重复模型。

### 阶段 3：TSF IME

- TSF thread manager；
- `ITfContextOwner` + composition sink（不实现 ITextStoreACP）；
- composition overlay；
- candidate 定位。

验收：Microsoft Pinyin/Japanese IME 可输入。

### 阶段 4：fallback 与 Sixel

- `IDWriteFontFallbackBuilder` 显式列表 + 系统 fallback；
- `libuisixel` 解码；
- D3D12 上传/绘制。

验收：缺失字体按列表优先，再由 DWrite 自动 fallback；sixel 正确显示。

### 阶段 5：透明与配置

- top-level DComp；
- premultiplied alpha；
- DWM backdrop；
- `window_config` 构造和配置项接入。

验收：透明背景正确，无黑边；配置可改变字体/palette/backdrop/opacity。

### 阶段 6：滚动条

- `libscrollbar`；
- 拖动/轨道点击/滚轮；
- resize/焦点。

验收：滚动条拖动/滚轮正常。

### 阶段 7：性能、设备恢复与 UIA

- damage 局部重绘；
- Sixel 图像缓存；
- atlas LRU；
- frame latency waitable；
- 移除所有轮询；
- device lost 检测/重建；
- UIA TextPattern/ScrollPattern/事件自动化；
- 60 秒空闲 CPU 测量。

验收：无输入时 CPU 0%，无持续 GPU 提交；device lost 可恢复。

---

## 7. 验收标准

1. `libcoreui` 直接读取 corehost console model，不复制 parser/model；
2. 不依赖任何 UI 框架；
3. 透明 top-level 正确；
4. 字体 fallback 先使用显式列表，再使用 DWrite 自动 fallback，不手写 codepoint fallback；
5. TSF IME 工作；
6. 垂直滚动条支持拖拽、轨道点击、滚轮；
7. 空闲时 GUI 线程完全阻塞，无轮询、无持续 Present；
8. 只有新消息、damage 或显式动画 timer 才唤醒；
9. WARP golden、DComp、IME 硬件测试通过；
10. UIA TextPattern/ScrollPattern/事件可自动化验证；
11. 亮暗主题/配置文件切换（如启用）；
12. device lost 可恢复。
