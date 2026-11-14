# 16-byte screen cell design

Status: reference implementation in `/libscreen`; not yet wired into `libcorehost`.

## Cell layout

```cpp
enum class cell_extent : std::int32_t {};
enum class cell_style : std::int32_t {};
enum class cell_hyperlink : std::int32_t {};

struct cell
{
    char16_t primary_;
    cell_info info_;
    cell_extent extent_;       // bits 0..23 offset, bits 24..31 length
    cell_style style_;
    cell_hyperlink hyperlink_;
};
```

`cell_info` is a 16-bit packed enum:

- bits 0-2: type (`scalar`, `complex`/`surrogate`/`grapheme`, `background`; zero blank);
- bits 3-5: width (`wide`, `tail`, `spacer_head`; zero narrow);
- bits 6-15: flags (`protected_cell`, `hyperlink`, `semantic_input`, `semantic_prompt`).

`cell_info_ops` owns the masks and `is_type_*`/`is_width_*`/`is_flags_*` predicates.
The struct is exactly 16 bytes:

- `primary_`: BMP UTF-16 code unit; `U+FFFD` for complex type.
- `info_`: type/width/flags.
- `extent_`: 24-bit screen-wide grapheme store offset and actual 1..255 length.
- `style_`: 32-bit screen-wide style ID.
- `hyperlink_`: 32-bit screen-wide hyperlink ID.

The complete cluster (including its real primary) lives in the screen-wide
`std::vector<char16_t>`.

## Ownership

- `screen_buffer` owns main page, optional alternate page, history, styles, hyperlinks and
  the grapheme store.
- `page` owns dense cells and row metadata.
- `row_state` owns used-column count and damage range.
- `grapheme_store` is append-only between rebuilds; callers check `can_append()`.
- `rebuild_graphemes()` rewrites live clusters and updates every cell extent.

## Operations

- `write_scalar`, `write_cluster`, `write_cell`
- `fill_rect`, `clear`
- `scroll_up`, `scroll_region_up`
- `resize`, `set_alternate`
- `clear_damage`, `damage_begin/end`
- `rebuild_graphemes`

## Invariants

- cell_info type=complex implies `primary_ == U+FFFD`.
- complex type implies a valid `[offset, offset+length)` in the store.
- cluster length is 1..255 and includes the real primary.
- wide heads are followed by a tail in the same row.
- styles/hyperlinks are screen-wide IDs; 0 means default/none.
- history is cleared on resize.
- alternate page never records history.
