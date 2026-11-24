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

## 复制语义

- `scroll_region_up/down` 在全宽滚动时直接移动 `row_block` 的 cells/store，不逐 cell 复制；
- 非全宽滚动按行复制，并把 cluster append 到目标 row store；
- 任意矩形 `copy_rect` 保留。跨行时按方向逐行处理，source row 不会被后续目标行覆盖；cluster 从 source row store 重映射到目标 row store；
- 不使用临时 `std::vector<copy_segment>`；
- `rebuild_graphemes` 按 row 使用普通循环重建，不使用 `cluster_remap`。
