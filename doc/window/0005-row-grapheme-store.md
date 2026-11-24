# Screen Buffer Per-Row Grapheme Store

## 目标

每个 row 拥有自己的 `grapheme_store`。`screen::cell` 的 `offset/length`
只在该 row 内解释，不再指向 screen-wide store。

## 新数据结构

```cpp
struct row_block final
{
    cell_vector cells;
    row_state state;
    grapheme_store graphemes;
};

struct history_row final
{
    cell_vector cells;
    history_state state;
    grapheme_store graphemes;
};
```

`page` 仍持有 `bizwen::deque<row_block>`；row scroll 时整个 `row_block`
移动，store 跟着移动。

`screen_buffer` 删除全局 `graphemes_`，访问变为 row-aware：

```cpp
std::span<const char16_t> cluster_of(std::size_t y, const cell &value) const;
std::span<const char16_t> history_cluster_of(std::size_t index, const cell &value) const;
```

写入：

```cpp
write_cluster(x, y, cluster)
    -> rows[y].graphemes.append(cluster)
```

## 为什么还存在跨行复制

纯按行复制不足以覆盖现有语义：

- `ConsolepCopyConsole`：任意矩形复制，源/目标可以跨行；
- `copy_rect`：VT insert/delete characters 目前是同一行，但 API 本身允许矩形；
- `scroll_region_up/down`：当前通过 `copy_rect` 移动多行。

可以做的简化：

1. scroll/insert/delete lines 改为移动整个 `row_block`，避免逐 cell 跨行复制；
2. insert/delete characters 只保留 `copy_row_span`，同一 row store 内 offset 可直接复用；
3. 任意矩形 `copy_rect` 仍需要跨行时，把源 cluster append 到目标 row store 并重映射 offset，或者在 API 层拒绝跨行。

## 待确认

- 是否保留任意矩形 `copy_rect` 的跨行复制，并对 cluster 做目标 row store 重映射；
- 或者删除通用 `copy_rect`，只保留 `copy_row_span` / `move_row_block`，由 Console copy API 自行逐行处理。
