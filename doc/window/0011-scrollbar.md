# Scrollbar

## 结构

`libscrollbar` 只负责几何和命中测试：

- track / thumb 计算；
- minimum thumb height；
- `above_thumb` / `thumb` / `below_thumb` 命中；
- pointer -> top row 映射；
- 无 Win32 依赖。

`libcorewindow` 负责：

- 从 console history + 当前页生成 scrolled view；
- scroll offset / scroll lock；
- 窗口鼠标消息、拖拽、滚轮；
- D3D12 overlay quad。

## 状态

`console_state`：

```text
scroll_offset   # console_state 中的行数，std::size_t，0 = bottom
scroll_locked    # 新输出/输入不改变 scroll offset
scroll_pixels    # UI 侧像素偏移，double；由 window/render 层维护
```

`console` C++ API：

```text
history_size()
history_limit()
set_history_limit(limit)   # 默认 1000
scroll_offset()
scroll_pixels()
scroll_by_pixels(pixels)
scroll_by(lines)
scroll_to_top()
scroll_to_bottom()
toggle_scroll_lock()
snap_to_bottom_if_unlocked()
```

## 交互

- 滚轮：每 120 delta 滚 3 行，保留高精度滚轮的小数增量；
- 主屏滚轮始终滚动本地 scrollback，即使应用启用了 Win32 ENABLE_MOUSE_INPUT；
- 只有 alternate screen + VT mouse tracking 的应用才接收滚轮事件；
- 指针位于 scrollbar 区域时始终本地滚动；
- console_state 只保存行数，UI 保存像素偏移，render 时换算；
- 右侧 16 DIP overlay scrollbar；
- thumb 只在上/下 26 DIP 箭头区域之间移动；
- 普通状态 2 DIP thumb，右侧 4 DIP；hover 展开为 8 DIP 圆角 thumb + 淡 track；
- 顶部/底部上下圆角箭头，点击滚动 1 行；箭头区域随 DPI 缩放；
- 箭头 mask 固定 32×32，绘制时缩放到 10×9 DIP，圆角半径为短边的 20%；
- 箭头上下 margin 为 8 DIP，thumb track inset 为 24 DIP；箭头到 thumb 的间距为 2 DIP；
- thumb 圆角用预生成 alpha mask 缓存在现有 R8 atlas 中，9-slice 绘制；
- 使用真实 client 尺寸，track 紧贴窗口右边缘；
- 展开动画在约 8 帧内完成，由 `DCompositionWaitForCompositorClock` 按显示刷新率推进；
- thumb 拖动按像素更新，避免行量化抖动；
- 内容按像素 offset 渲染，不是整行跳变。
- 轨道点击：上/下翻一页；
- Shift + 轨道点击：跳到点击位置；
- `Ctrl+Shift+S` / `ScrollLock`：切换 scroll lock；
- 回滚状态下：
  - `Home` / `End` / `PgUp` / `PgDn` / `Up` / `Down` 滚动；
  - 未 lock 时输入自动回到底部；
  - lock 时输入和输出都保持当前查看位置。

## 常量位置

所有滚动条几何、mask、绘制参数都集中在：

```text
libscrollbar/scrollbar.hpp
```

包括 width、arrow margin/size、track inset、mask size/samples、圆角比例、thumb 宽度/右边距、透明度和动画帧数。

## Fixed mask 决策

滚动条相关的两个 mask 固定为 **32×32，无 supersampling**：

- thumb：32×32，9-slice；
- 箭头：32×32，绘制时缩放到 10×9 DIP，圆角 20%；
- 不再按 DPI 重新生成，也不提供运行时/编译期变体；
- 选择原因：生成开销低、atlas 占用稳定、高 DPI 下仍能接受；
- 代价：箭头从 32×32 缩到 10×9 时边缘会有轻微缩放模糊，这是有意接受的质量/性能权衡。

之前的 16/32/64 与 1x/4x 变体仅用于测试，当前源码不再提供这些配置项。
## 当前限制

- hover 动画已实现，不使用固定 timer；渲染线程在动画期间使用合成器时钟等待；
- 新输出保持位置的 anchor 调整在 render worker 中完成；
- history limit 只有 C++ API，不读取配置文件；
- 选择/复制和 mouse reporting 尚未接入。
