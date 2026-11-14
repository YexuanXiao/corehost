# 16-byte screen cell design

Status: reference implementation in `/libscreen`; not yet wired into `libcorehost`.

## Cell layout

```
word 0: primary 16 | content 2 | width 2 | offset 24 | length 8 | flags 12
word 1: style 32 | hyperlink 32
```

- `primary`: BMP UTF-16 code unit; `U+FFFD` when `content=2`.
- `content`: blank=0, scalar=1, complex/surrogate/grapheme=2, background=3.
- `width`: narrow, wide, tail, spacer head.
- `offset`: 24-bit screen-wide grapheme store offset.
- `length`: actual 1..255 UTF-16 units, no minus-one encoding.
- `style`: 32-bit screen-wide style ID.
- `hyperlink`: 32-bit screen-wide hyperlink ID.
- `flags`: 12 bits; low four are protected/hyperlink/semantic input/semantic prompt.

The complete cluster (including its real primary) lives in the screen-wide
`std::vector<char16_t>`. `cell` is trivially copyable and exactly 16 bytes.

## Ownership

- `screen_buffer` owns main page, optional alternate page, history, styles, hyperlinks and
  the grapheme store.
- `page` owns dense cells and row metadata.
- `row_state` owns used-column count and damage range.
- `grapheme_store` is append-only between rebuilds; callers check `can_append()`.
- `rebuild_graphemes()` rewrites live clusters and updates every cell offset/length.

## Operations

- `write_scalar`, `write_cluster`, `write_cell`
- `fill_rect`, `clear`
- `scroll_up`, `scroll_region_up`
- `resize`, `set_alternate`
- `clear_damage`, `damage_begin/end`
- `rebuild_graphemes`

## Invariants

- `content=2` implies `primary == U+FFFD`.
- `content=2` implies a valid `[offset, offset+length)` in the store.
- cluster length is 1..255 and includes the real primary.
- wide heads are followed by a tail in the same row.
- styles/hyperlinks are screen-wide IDs; 0 means default/none.
- history is cleared on resize.
- alternate page never records history.
