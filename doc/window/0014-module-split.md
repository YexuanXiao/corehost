# libcorewindow 合理拆分

## 目标

`libcorewindow` 不再同时拥有窗口、渲染、IME 和 console view 状态。拆分不机械照搬计划，而是按现有代码依赖和职责划分。

## 当前结构

```text
libcoreui
  console.hpp           console view 状态、damage/scroll/selection 访问
  console_theme.hpp     主题/调色板
  render_worker.hpp     D3D12 quad 构建、atlas 使用、滚动条绘制

libime
  tsf_host.hpp          TSF thread manager、context、composition sink

libcorewindow
  window_host.hpp       Win32 窗口、消息循环、输入/选择/剪贴板、滚动条交互
  window_console_session.{hpp,cpp}
                        ConDrv console server 组装
```

## 依赖方向

```text
libcorewindow
  ├── libcoreui
  │     ├── libconsole
  │     ├── libscreen
  │     ├── libuitext
  │     ├── libd3d12ui
  │     └── libscrollbar
  ├── libime
  └── corehost/window/window_console_session
```

`libcoreui` 不依赖窗口；`libime` 不依赖渲染；`libcorewindow` 只做宿主和组装。

## 后续可继续拆

- `libuiinput`：把 `window_host` 的键盘/鼠标/选择/剪贴板状态拆出；
- `libuisixel`：Sixel 解码；
- `libsubwindow`：child 嵌入；
- `libcoreui` 可再拆为 `libuitext` 之外的 UI pass 组织。

## 验证

```text
100% tests passed, 0 tests failed out of 12
```
