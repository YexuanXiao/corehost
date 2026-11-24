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
scroll_offset   # 距离底部的行数，0 = bottom
scroll_locked    # 新输出/输入不改变 scroll offset
```

`console` C++ API：

```text
history_size()
history_limit()
set_history_limit(limit)   # 默认 1000
scroll_offset()
scroll_by(lines)
scroll_to_top()
scroll_to_bottom()
toggle_scroll_lock()
snap_to_bottom_if_unlocked()
```

## 交互

- 滚轮：3 行/notch；
- 右侧 16 DIP overlay scrollbar；
- 普通状态 2 DIP thumb，右侧 4 DIP；hover 展开为 8 DIP thumb + 淡 track；
- 顶部/底部上下箭头；
- 280ms Fluent 风格展开动画，按帧推进；
- thumb 拖动；
- 轨道点击：上/下翻一页；
- Shift + 轨道点击：跳到点击位置；
- `Ctrl+Shift+S` / `ScrollLock`：切换 scroll lock；
- 回滚状态下：
  - `Home` / `End` / `PgUp` / `PgDn` / `Up` / `Down` 滚动；
  - 未 lock 时输入自动回到底部；
  - lock 时输入和输出都保持当前查看位置。

## 当前限制

- hover 动画已实现，但还没有独立的动画 timer，依赖现有 compositor frame loop；
- 新输出保持位置的 anchor 调整在 render worker 中完成；
- history limit 只有 C++ API，不读取配置文件；
- 选择/复制和 mouse reporting 尚未接入。
