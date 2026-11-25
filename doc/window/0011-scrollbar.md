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
- console_state 只保存行数，UI 保存像素偏移，render 时换算；
- 右侧 16 DIP overlay scrollbar；
- thumb 只在上/下 26 DIP 箭头区域之间移动；
- 普通状态 2 DIP thumb，右侧 4 DIP；hover 展开为 8 DIP 圆角 thumb + 淡 track；
- 顶部/底部上下圆角箭头，点击滚动 1 行；箭头区域随 DPI 缩放；
- 箭头 mask 按实际物理尺寸生成并 1:1 绘制，避免缩放模糊；
- thumb 圆角用预生成 alpha mask 缓存在现有 R8 atlas 中，9-slice 绘制；
- thumb mask 提供 16/32/64、1x/4x 超采样编译变体测试；
- 使用真实 client 尺寸，track 紧贴窗口右边缘；
- 展开动画在约 8 帧内完成，按现有 compositor frame loop 推进；
- thumb 拖动按像素更新，避免行量化抖动；
- 内容按像素 offset 渲染，不是整行跳变。
- 轨道点击：上/下翻一页；
- Shift + 轨道点击：跳到点击位置；
- `Ctrl+Shift+S` / `ScrollLock`：切换 scroll lock；
- 回滚状态下：
  - `Home` / `End` / `PgUp` / `PgDn` / `Up` / `Down` 滚动；
  - 未 lock 时输入自动回到底部；
  - lock 时输入和输出都保持当前查看位置。

## Thumb mask 测试变体

编译期参数：

```text
COREHOST_SCROLLBAR_THUMB_MASK_SIZE       16 / 32 / 64
COREHOST_SCROLLBAR_THUMB_MASK_SAMPLES    1 / 4
```

已构建的测试二进制：

```text
C:\1\corehostbinary\thumb16_hard.exe   16x16, 1x
C:\1\corehostbinary\thumb16_ss.exe     16x16, 4x
C:\1\corehostbinary\thumb32_hard.exe   32x32, 1x
C:\1\corehostbinary\thumb64_ss.exe     64x64, 4x
C:\1\corehostbinary\corehostw.exe      32x32, 4x (默认)
```

## 当前限制

- hover 动画已实现，但还没有独立的动画 timer，依赖现有 compositor frame loop；
- 新输出保持位置的 anchor 调整在 render worker 中完成；
- history limit 只有 C++ API，不读取配置文件；
- 选择/复制和 mouse reporting 尚未接入。
