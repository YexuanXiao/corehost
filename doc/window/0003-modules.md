# Window Mode 模块边界

## 允许依赖

`corehost/window` 只负责组装，可以使用：

- `common`
- `libconsole`
- `libcoreio`
- `libinterop`
- `libscreen`
- `libvtparser`
- `libvtstate`

## 禁止依赖

`corehost/window` 不得包含或链接：

- `corehost/defterm`
- `libcorepty`
- `libconpty`
- ConPTY API

## 新增模块

### `libcorewindow`

当前实际职责：

- top-level Win32 宿主、窗口类、消息循环、DPI/焦点；
- 通过 `libd3d12ui` 创建交换链，调用 `libcoreui`；
- 键盘/鼠标/选择/剪贴板/IME 消息；
- UIA provider（`uia_provider.hpp`，TextPattern/ScrollPattern/事件）；
- 键盘、鼠标、剪贴板和选择由 `window_host` 直接处理，没有独立 `libuiinput` 模块。

不包含文字 shaping、Sixel 解码、console API、parser。

### `libconfig`

- `console_config`：scrollback 上限（默认 3000）、初始 grid、cursor 等；
- `window_config`：bell、candidate_offset_cells（默认 2）、backdrop、opacity、dark mode、exit policy；
- 当前只允许 C++ 构造，不读取配置文件。

### `libcoreui`

职责：

- 从 corehost console model 生成 D3D12 绘制命令；
- 组织渲染 pass；
- 不拥有窗口、不拥有 device、不实现 console 语义。

### `libd3d12ui`

职责：

- D3D12/DXGI 基础设施；
- device、queue、descriptor、fence、swapchain；
- 不负责文字、窗口或控制台。

### `libuitext`

职责：

- DirectWrite 字体集合、fallback、shaping、glyph raster；
- CPU glyph atlas；
- 不拥有 D3D12 资源。

### `libime`

当前职责：

- TSF thread manager、document/context、context owner/composition sink/text edit sink；
- composition、candidate 定位、finalized/composing 分离；
- 不绘制。

限制：没有 `ITextStoreACP`。

### `libuisixel`

职责：

- 解析 Sixel，生成 RGBA8 位图；
- 支持 RGB/HLS 调色板；
- 不读取图片文件，不使用 WIC。

### `libscrollbar`

职责：

- 垂直滚动条几何、状态、命中测试、动画；
- 生成 quad 数据。

## 控制台会话

窗口模式需要一个**不依赖 defterm/libcorepty** 的会话层，负责：

- ConDrv `\Server` 读取；
- `CONNECT`；
- console API dispatcher；
- `libconsole`/`libscreen` 状态；
- completion；
- 进程注册。

该层放在 `corehost/window/window_console_session.{hpp,cpp}`，与 defterm 的 `console_session` 同级；它是对现有模块的组装，不复制 parser 或 screen model。

## 依赖关系

```text
libcorewindow
    ├── libd3d12ui
    ├── libcoreui
    ├── libime
    └── corehost/window/window_console_session

libcoreui
    ├── libuitext
    ├── libuisixel
    ├── libscrollbar
    └── libconsole/libscreen model
```
