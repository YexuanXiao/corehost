# 16-byte screen cell design

Status: header-only reference implementation in `/libscreen`; not yet wired into
`libcorehost`. All declarations and definitions live in `.hpp`; `libscreen` is a CMake
INTERFACE target.

## Cell layout

```cpp
enum class cell_extent : std::int32_t {};
enum class cell_style : std::int32_t {};
enum class cell_hyperlink : std::int32_t {};

struct cell
{
    char16_t primary_;
    cell_type type_ : 4;
    cell_width width_ : 4;
    cell_flags flags_ : 8;
    cell_extent extent_;       // bits 0..23 offset, bits 24..31 length
    cell_style style_;
    cell_hyperlink hyperlink_;
};
```

- `primary_`: BMP UTF-16 code unit; `U+FFFD` when `type_=2` (complex) or image.
- `type_`: blank=0, scalar=1, complex/surrogate/grapheme=2, background=3, image=4.
- `width_`: narrow, wide, tail, spacer head.
- `flags_`: protected/hyperlink/semantic/reserved.
- `extent_`: for complex: 24-bit grapheme store offset + 8-bit length; for image:
  image fragment ID.
- `cell_type_ops`/`cell_width_ops`/`cell_flags_ops` provide field masks and
  `is_*`/`has` predicates; bitfields are accessed through these ops, not raw
  bitfield layout.
- `style_`: 32-bit screen-wide style ID.
- `hyperlink_`: 32-bit screen-wide hyperlink ID.

The complete cluster (including its real primary) lives in the screen-wide
`std::vector<char16_t>`. `cell` is trivially copyable and exactly 16 bytes.

## Ownership

- `screen_buffer` owns main page, optional alternate page, history, styles, hyperlinks,
  image table and the grapheme store.
- `page` owns dense cells and row metadata.
- `row_state` owns used-column count, line flags, command exit code and damage range.
- `console_state` owns cursor, tabs, scroll region, selection and search overlays.
- `grapheme_store` is append-only between rebuilds; callers check `can_append()`.
- `rebuild_graphemes()` rewrites live clusters and updates every cell extent.

## Operations

- `write_scalar`, `write_cluster`, `write_image`, `write_cell`
- `fill_rect`, `clear`
- `scroll_up`, `scroll_region_up`
- `resize`, `set_alternate`
- `clear_damage`, `damage_begin/end`
- `rebuild_graphemes`
- line flags / line rendition / command exit code
- `console_state`: cursor, tabs, scroll region, selection, search

## Invariants

- `type_=2` implies `primary_==U+FFFD`.
- `type_=2` implies a valid `[offset, offset+length)` in the store.
- `type_=image` implies a valid image-fragment ID; primary is `U+FFFD`.
- cluster length is 1..255 and includes the real primary.
- wide heads are followed by a tail in the same row.
- styles/hyperlinks are screen-wide IDs; 0 means default/none.
- history is cleared on resize.
- alternate page never records history.
