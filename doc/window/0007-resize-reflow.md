# Window Resize / Grid Reflow

## 触发

渲染线程每帧读取 client size，并按当前 text metrics 计算：

```cpp
columns = max(1, width / cell_width);
rows = max(1, height / cell_height);
```

与当前 grid 不同则锁定 `console_mutex` 调用 `console::resize(columns, rows)`。

## screen_buffer::resize

- 输入可选的 `coord *cursor`；
- 从 history rows + active rows 构造逻辑行；
- `line_flag::wrapped` 标记作为换行连接依据；
- 每个逻辑行重新按新宽度折行；
- 行尾 blank cell 会裁剪，避免空白行占满视口；
- active viewport 取 reflow 后最后 `height` 行；
- 多余行进入 history，受 `history_limit` 限制；
- history row 也按相同规则 reflow；
- cursor 按原始 cell 在 reflow 中的落点映射；
- alternate page 重置。

## 写入端

`write_text_row` 在每次自动换行时给新行设置：

```cpp
line_flag::wrapped
```

这样 resize 时可以把旧行重新拼回逻辑行。

## 验证

新增测试覆盖：

- 写入 `ABCDEF` 到 4 列，第二行 wrapped；
- resize 到 3 列后得到 `ABC` / `DEF`；
- 历史与 active 的 viewport 语义。

```text
100% tests passed, 0 tests failed out of 11
```

## 限制

- reflow 目前不特殊处理 image、double-height、prompt 边界；
- 宽字符在宽度 1 的网格中会被替换为 blank；
- cursor 映射在 cursor 落入 history 时退回 clamp。
