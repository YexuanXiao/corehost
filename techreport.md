# corehost 16 字节 cell 屏幕缓冲区技术报告

## 摘要

本报告重新设计 corehost 的屏幕缓冲区，采用 **16 字节 cell**：

- 16 位 UTF-16 primary；
- 4 位 `cell_type`；
- 4 位 `cell_width`；
- 8 位 `cell_flags`；
- 32 位 `cell_extent`（实际打包 24 位 offset + 8 位 length）；
- 32 位 `cell_style`；
- 32 位 `cell_hyperlink`。

`complex`/`surrogate`/`grapheme` 都是 `cell_type=2` 的别名，统一表示“这个 cell 需要多个
UTF-16 code unit”。当 `cell_type=2` 时，cell 的 primary 固定为 `U+FFFD`；真实 primary 和
完整 cluster 保存在 screen-wide 的 `std::vector<char16_t>` 中，由 `cell_extent` 内部的
24 位 offset 和 8 位 length 定位。

实现位于 [libscreen](libscreen)，包括：

- `cell`、style/hyperlink table；
- 24 位 offset/8 位 length 的 grapheme store；
- dense page、row damage、main/alternate buffer、scrollback history；
- 自动化测试 `LibScreen.Reference`。

四类实现的对比保留在报告第 4 节：

1. 原版 conhost / Windows Terminal；
2. Contour；
3. Ghostty；
4. 本报告的 corehost 16-byte cell。

## 1. 目标与约束

### 1.1 功能目标

- 控制台 API 的 cell/列语义与 Windows Console 一致；
- 内部保存完整 Unicode cluster，不把 non-BMP surrogate 拆成两个独立 cell；
- 普通 BMP 文本不分配 cluster 存储；
- 支持 wide/tail、空白、背景、保护、超链接等 cell 状态；
- 支持主缓冲区、备用缓冲区和 scrollback；
- 支持行级 damage，供无头 VT 序列化和未来 DWrite renderer 共用；
- 屏幕缓冲区与 Console API/DWrite 解耦。

### 1.2 8 字节 cell 的局限

之前的 8 字节方案只能容纳：

- 16 位 primary；
- 16 位 offset；
- 6 位 length；
- 16 位 style；
- 2 位 type + 2 位 width + 5 位 flags。

它的问题：

- 16 位 offset 只有 65,535 个 UTF-16 unit；
- 6 位 length 只能到 64；
- 16 位 style ID 是 page-local，跨 page 需要重映射；
- 没有空间保存 hyperlink ID；
- 依赖 `length - 1` 与保留位等隐式编码。

16 字节 cell 的目标不是追求最小内存，而是让 **24 位 offset、8 位 length、32 位 style、
32 位 hyperlink、8 位 flags** 同时成立，并把隐式编码全部去掉。

## 2. 参考实现

### 2.1 原版 conhost / Windows Terminal

conhost 的内部 cell 是 `OutputCell`：

- `std::wstring _text` 保存完整 UTF-16 文本；
- `TextBuffer` 保存行、行属性、cursor、line rendition 等；
- 文本可以超过一个 UTF-16 code unit，因而能保存 surrogate pair、组合序列和多码点
  grapheme；
- `CHAR_INFO` 只是一个有损的 API 投影。

读取路径：

- `CONSOLE_INFORMATION::AsCharInfo` 调用 `Utf16ToUcs2(cell.Chars())`；
- cell 文本长度大于 1 时返回 `U+FFFD`；
- `ReadOutputStringW`（`ReadConsoleOutputCharacter` 的底层）也把多 unit cell 替换为
  `U+FFFD`，并跳过 trailing cell。

写入路径：

- `WriteConsoleOutput` 的每个 `CHAR_INFO` 被解释为一个 cell、一个 `WCHAR`，不跨格合并
  surrogate pair；
- `WriteConsoleW` 的 legacy 路径通过 `TextBuffer`/`ROW::WriteHelper` 的 `GraphemeNext`
  写入，能保存完整 grapheme；
- `WriteConsoleOutputCharacterW` 通过 `OutputCellIterator` 的 `utf16_next` 按 codepoint
  推进，能保存单个 surrogate pair，但不负责完整 grapheme 合并；
- `VtIo::Writer::WriteInfos` 接受 `CHAR_INFO`，对 surrogate 调用 `SanitizeUCS2`，旧 API
  的 VT 输出也会降级。

Windows Terminal 的渲染路径不使用 `CHAR_INFO`；它使用 `TextBuffer`/`OutputCell` 的完整
UTF-16 文本，交给 DWrite/AtlasEngine。因此 conhost 的分层是：

```text
内部 TextBuffer/OutputCell（完整 UTF-16）
        ├── Console API CHAR_INFO 投影（有损，U+FFFD）
        └── DWrite/VT 渲染（完整文本）
```

### 2.2 Contour

Contour 的 `LineSoA` 是一行一组 SoA 数组：

- `codepoints`：每 cell 的 `char32_t` primary；
- `widths`：每 cell 的 1/2 列宽；
- `sgr`：每 cell 的图形属性；
- `clusterSize`/`clusterPoolIndex`/`clusterPool`：多码点 grapheme 的 side pool；
- `LineShape`：`Uniform`/`Mixed`/`Spanning` 快路径；
- `MaxGraphemeClusterSize = 16`，真实 RGI emoji 已有 10 个 codepoint 的案例。

Contour 的 `DirectWriteShaper` 仍以 `char32_t` 为输入，在 shaper 边界转为 UTF-16，并把
UTF-16 glyph cluster 映射回 cell cluster；fallback 不拆 grapheme/ligature。

Contour 适合跨平台、SoA 扫描和复杂文本渲染，但没有 Windows `CHAR_INFO` 的兼容语义。

### 2.3 Ghostty

Ghostty 的 `Cell` 是 `packed struct(u64)`，8 字节：

- 21 位 codepoint；
- content tag；
- 16 位 page-local style ID；
- wide/spacer 标志；
- protected/hyperlink/semantic 等标志；
- 多码点 grapheme 由 page 的 grapheme allocator/map 保存；
- page 使用页对齐分配和 page list，支持 scrollback；
- style set 使用 page-local 引用计数。

Ghostty 证明了 8 字节 packed cell + page-local side table 的高性能路径，但 21 位 primary、
16 位 style 和 page-local grapheme map 是为跨平台终端设计的，不直接满足 Windows
`CHAR_INFO`/DWrite 的 16 位单元和 corehost 的 24 位 offset 需求。

### 2.4 corehost 16-byte cell

本报告的设计：

- 16 字节 cell；两个 64 位 word；
- UTF-16 primary，BMP 直接保存，complex 时为 `U+FFFD`；
- 24 位 cluster offset；
- 8 位 cluster length；
- 32 位 screen-wide style ID；
- 32 位 screen-wide hyperlink ID；
- 8 位 flags；
- screen-wide `std::vector<char16_t>` cluster store；
- dense page + row metadata + main/alternate + scrollback history；
- Console API 与 DWrite 只在边界投影/映射。

## 3. 四种实现的全面对比

### 3.1 对比表

| 维度 | 原版 conhost / Windows Terminal | Contour | Ghostty | corehost 16-byte cell |
| --- | --- | --- | --- | --- |
| 内部 cell 大小 | `OutputCell` 含 `std::wstring` + 属性，通常明显大于 16B | SoA 多数组，每 cell 约多个字节的 codepoint/width/属性 | 8B packed cell | **16B** |
| primary 编码 | UTF-16 `wstring` 文本 | `char32_t` codepoint | 21 位 codepoint | **16 位 UTF-16 code unit** |
| 非 BMP | cell 文本可含 surrogate pair | `char32_t` 完整标量 | 21 位 codepoint + grapheme map | primary=`U+FFFD`，完整 pair 在 store |
| grapheme cluster | 多 unit `wstring` | per-line cluster pool + LineShape | per-page grapheme allocator/map | **screen-wide char16 store + 24 位 offset/8 位 length** |
| style | `TextAttribute`/legacy 投影 | 每 cell GraphicsAttributes | page-local style ID | **32 位 screen-wide style ID** |
| hyperlink | 外部/属性路径 | 每 cell HyperlinkId | 每 cell hyperlink + page map | **32 位 screen-wide hyperlink ID** |
| 网格/分页 | TextBuffer/行 + 旧 API 缓冲 | dense/ring line、reflow | page allocator + page list | dense page + row damage + main/alt/history |
| Console API | `CHAR_INFO` 单 WCHAR；多 unit 有损 | 不直接提供 Windows Console API | 不直接提供 Windows Console API | 与 conhost 相同的 `U+FFFD` 投影 |
| DWrite | 内部完整文本，DWrite 渲染 | char32→UTF-16 shaper + cluster map | 平台 renderer | 完整 cluster 直接生成 UTF-16 run |
| 8/16B 取舍 | 语义丰富但 cell 重 | SoA 扫描快但字段多 | 内存紧凑但字段/offset 受限 | 用 2 倍于 8B 的内存换 24 位 offset、8 位 length、32 位 ID 和 hyperlink |

### 3.2 Cell 大小与用途

- conhost 优先兼容旧 API 和完整 TextBuffer，cell 内有变长 `wstring`，不是为 16B 设计的；
- Contour 优先 SoA 扫描、批量 ASCII 和跨平台渲染，字段按热/温/冷分层；
- Ghostty 优先紧凑和 page 生命周期，8B cell 足以支持其 21 位 codepoint 模型；
- corehost 16B 优先 Windows 兼容 + 24 位 cluster 地址 + 32 位共享 ID，接受 cell 内存翻倍。

corehost 的 16B 不是“最大的 cell”，而是**在 Windows 语义下把关键字段一次放够**：

```text
primary 16 | cell_type 4 | cell_width 4 | cell_flags 8
cell_extent 32 (24-bit offset + 8-bit length) | cell_style 32 | cell_hyperlink 32
```

### 3.3 Cluster 存放

- conhost 把 cluster 放在 cell 的 `std::wstring` 中，读取 API 时再降级为单个 WCHAR；
- Contour 把首个 codepoint 放 cell，额外 codepoint 放 per-line cluster pool；
- Ghostty 把 primary 放 8B cell，完整 grapheme 放 page grapheme map；
- corehost 把 actual primary 和全部 cluster 放 screen-wide char16 store，cell 只保留
  offset/length，primary 固定 `U+FFFD`。

corehost 的选择适合 Windows API：`CHAR_INFO` 读取本来就是 `U+FFFD`，内部不需要为了
API 保留一个假的 high surrogate；DWrite/无头路径直接读取 store 的完整 cluster。

### 3.4 Style、hyperlink 与 page 生命周期

- conhost style 是 per-cell 属性/legacy 投影；
- Contour style 是 per-cell 完整属性；
- Ghostty style 是 page-local ID；
- corehost style/hyperlink 是 **screen-wide 32 位 ID**。

32 位 ID 的代价是 cell 16B，但收益是：

- 跨 page/scrollback 复制 cell 不需要重映射 style；
- hyperlink 可以直接保存在 cell 中；
- 未来 DWrite 的 color/underline/hyperlink 可以共享同一套表；
- 空白/背景 cell 仍只使用 style ID，不内联颜色。

## 4. 16 字节 cell 设计

### 4.1 字段布局

| 字段 | 位数 | 含义 |
| --- | ---: | --- |
| `primary_` | 16 | UTF-16 code unit；`cell_type=2` 时为 `U+FFFD` |
| `type_` | 4 | blank / scalar / complex(alias surrogate/grapheme) / background |
| `width_` | 4 | narrow / wide / tail / spacer head |
| `flags_` | 8 | protected / hyperlink / semantic / reserved |
| `extent_` | 32 | 24 位 offset + 8 位 length |
| `style_` | 32 | screen-wide style table ID |
| `hyperlink_` | 32 | screen-wide hyperlink table ID |
| **总计** | **128** | **16 字节** |

实际结构：

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
    cell_extent extent_;
    cell_style style_;
    cell_hyperlink hyperlink_;
};
```

`type_`/`width_`/`flags_` 使用位域，使 16 位 primary 后的 32 位头空间刚好容纳
4+4+8 位；`extent`/`style`/`hyperlink` 是 32 位 enum，合计 12 字节，因此 cell 为
16 字节。`static_assert` 锁定大小、对齐、trivially copyable 和 standard-layout。

### 4.2 cell_type 的统一语义

```cpp
enum class cell_type : std::uint8_t
{
    blank = 0,
    scalar = 1,
    complex = 2,
    surrogate = 2,
    grapheme = 2,
    background = 3,
    image = 4,
};
```

- `complex`/`surrogate`/`grapheme` 是同一个值 2；
- type=2 时 primary 固定为 `U+FFFD`；
- 真实 primary/cluster 在 store；
- type 不负责区分 surrogate 和 grapheme；如果未来需要区分，使用 flags 或 side
  metadata，而不是增加 type 位宽；
- 4 位 type 字段保留后续扩展空间，但值 0..3 已覆盖当前语义。

#### 掩码查询

三个字段仍通过 mask 查询，而不是直接把位域当成无类型整数：

```cpp
namespace cell_type_ops
{
constexpr std::uint8_t mask = 0x0Fu;
constexpr std::uint8_t type_of(cell_type value) noexcept
{
    return static_cast<std::uint8_t>(value) & mask;
}
constexpr bool is_blank(cell_type value) noexcept
{
    return type_of(value) == static_cast<std::uint8_t>(cell_type::blank);
}
// scalar/complex/surrogate/grapheme/background 同理
}

namespace cell_width_ops
{
constexpr std::uint8_t mask = 0x0Fu;
constexpr std::uint8_t width_of(cell_width value) noexcept
{
    return static_cast<std::uint8_t>(value) & mask;
}
// narrow/wide/tail/spacer head 同理
}

namespace cell_flags_ops
{
constexpr std::uint8_t mask = 0xFFu;
constexpr std::uint8_t flags_of(cell_flags value) noexcept
{
    return static_cast<std::uint8_t>(value) & mask;
}
constexpr bool has(cell_flags value, cell_flags flag) noexcept
{
    return (flags_of(value) & static_cast<std::uint8_t>(flag)) != 0;
}
// protected/hyperlink/semantic input/prompt 同理
}
```

- type/width 是离散字段，查询时先 mask 再比较；
- flags 是可组合位，查询时先 mask 再按位测试；
- cell accessor（`is_blank()`/`is_wide()`/`has()` 等）复用这些 ops，
  不直接依赖 bitfield 的编译器布局；
- 位域只负责存储，查询语义由 ops 统一。

### 4.3 cell_extent：24 位 offset + 8 位 length

`cell_extent` 是 `std::int32_t` enum，内部打包：

```text
bits 0..23  : cluster offset
bits 24..31 : actual cluster length (1..255)
```

- offset 最大 `0xFFFFFF`，store 总容量 `0x1000000` 个 UTF-16 unit；
- length 是实际 1..255，不是 length-1；
- 一个 cell 最多一个 cluster，cluster 包含真实 primary；
- 标量/空白/背景不访问 store；
- 超过 255 unit 的输入拒绝或替换，不能拆成多个 cell；
- store 是 screen-wide 的，main/alternate/history 的 cell 共享同一 offset 空间；
- `can_append()` 由调用者先检查；append 不负责重建/截断；
- 超过 24 位容量时 `rebuild_graphemes()` 扫描 screen 的 cell/history，按 live cluster
  重建 store 并更新 extent。

`cell::offset()`/`cell::length()` 通过 `extent_offset()`/`extent_length()` 解码，不直接暴露
打包位。

### 4.4 style 与 hyperlink

- `style=0` 是默认样式；
- `hyperlink=0` 表示无链接；
- style table 是 screen-wide 的，保存 foreground/background/underline/attributes/legacy；
- hyperlink table 保存 UTF-16 `std::u16string` URI/id，不使用 `wstring`/`wchar_t`；
- cell 只保存 32 位 ID；
- Console API 只读取 legacy 投影，不读取 RGB 表；
- DWrite/窗口渲染读取完整 style 表。

### 4.5 cell API

`libscreen/cell.hpp` 提供：

- `blank`、`background`、`tail`、`spacer_head`；
- `scalar`（BMP code unit，孤立 surrogate 规范为 `U+FFFD`）；
- `complex`/`surrogate`/`grapheme`（offset/length）；
- `image`（image fragment ID，primary 为 `U+FFFD`）；
- `primary()`、`text_unit()`、`style()`、`hyperlink()`、`type()`、`width()`、
  `offset()`、`length()`、`flags()`；`cell_extent` 通过 `make_cell_extent()`/
  `extent_offset()`/`extent_length()` 编码；
- `has_cluster()`、`is_blank()`、`is_wide()`、`is_continuation()`、`is_spacer_head()`；
- `with_style()`、`with_hyperlink()`、`with_flag()`、`with_cluster_ref()`；
- `static_assert(sizeof(cell) == 16)`、trivially copyable、standard-layout。

## 5. 屏幕缓冲区结构

实现：[libscreen/screen_buffer.hpp](libscreen/screen_buffer.hpp)（header-only）。

### 5.1 Page 与 Row

```text
page:
  bizwen::deque<row_block>  // one contiguous cell vector per row

row_block:
  vector<cell> cells        // width cells, contiguous
  row_state state

row_state:                  // active-page metadata
  uint32 used_columns
  line_flags flags
  int32 command_exit_code   // sentinel = no command
  uint32 dirty_begin / dirty_end

history_row:
  vector<cell> cells
  history_state              // compact: flags + exit code only
```

- page 是 `bizwen::deque<row_block>`，不是单个 dense cell 数组；
- 每行 cell 仍连续，可返回 `span<const cell>`；
- row 不拥有 cluster 文本或 style；
- `used_columns` 记录实际使用列；
- `line_flags` 记录 wrappable/wrapped/marked/output-start/command-end/prompt-end/
  user-mark 和 line rendition；
- `command_exit_code` 保存 shell integration 的退出状态；
- damage 是每行的半开区间 `[dirty_begin, dirty_end)`；
- 初始 page 全部行标记为 dirty，保证首次渲染完整；
- 非零尺寸、32 位 style/hyperlink/image ID 未耗尽等内部不变量使用 `assert`，
  不作为可恢复异常处理。

### 5.2 Main、Alternate 与 History

- `screen_buffer` 拥有 main page；
- `set_alternate(true)` 创建/切换到 alternate page；
- alternate page 不记录 scrollback；
- main page 满屏 `scroll_up` 时，把顶部 `row_block` 的 cell vector 移入
  `bizwen::deque<history_row>`，不复制 16B cell；
- 进入 history 的 `row_state` 简化为 `history_state`：只保留 line flags 和 command exit
  code，不保存 active page 的 used-column/damage；
- history 上限由构造函数 `history_limit` 控制；
- history line 保存 cell 和紧凑 row metadata，保证 wrapped/marked/command 边界在
  scrollback 中仍可查询；
- 当前不复用空白 row vector：`push_blank` 每行分配新的 `vector<cell>`；
- resize 清空 history，避免宽高变化后旧行宽度不一致；
- style/hyperlink/grapheme/image store 是 screen-wide 的，main/alternate/history 共享。

### 5.3 写入、宽字符和 cluster

- `write_scalar`：写一个 BMP code unit；wide 时把 x+1 标记为 tail；
- `write_cluster`：检查 store 容量、append 完整 cluster、写 type=2 cell；
- `write_image`：写 image 类型 cell，fragment metadata 在 screen-wide image table；
- `write_codepoint`：把 `char32_t` 转为 UTF-16 code unit 或 surrogate-pair cluster；
- `glyph_at`/`codepoint_at`/`line_text`：UTF-16 读取完整 glyph/整行，`char16_t` 原生；
- `glyph_width`/`is_trailing`/`used_columns`：corehost 行布局需要的列信息；
- `legacy_at`/`write_legacy_scalar`：与 `CHAR_INFO`/`WORD` 属性边界对应；
- `write_cell`：低层写，负责清理旧的 wide head/tail 邻域；
- `fill_rect`、`clear`：直接填 cell；
- `scroll_up`：move 顶部 row vector 到 history，pop_front，push 空白 row_block；
- `scroll_region_up`：区域内移动/复制 cell，更新 damage、used_columns、line flags 和
  command exit code；
- 宽字符/继续列由 `width` 表达，渲染器跳过 tail；
- 覆盖 wide head 会清理 tail；覆盖 tail 会清理 head。

### 5.4 Grapheme store

实现：[libscreen/grapheme_store.hpp](libscreen/grapheme_store.hpp)。

- `std::vector<char16_t>`；
- `max_offset = 0xFFFFFF`，`max_units = 0x1000000`；
- `max_cluster_length = 255`；
- `ref { offset, length }`；
- `can_append(units)` 由调用者检查；
- `append` 只写，不重建；
- `view` 返回只读 span；
- `rebuild_graphemes()` 扫描 main/alternate/history，按 live cluster 重建并更新 cell refs。

### 5.5 Damage

`page::row_state` 保存每行的 `[begin,end)` 脏区间：

- 写入标记对应列；
- fill/scroll 标记区域；
- resize 的新 page 全部行标脏；
- `clear_damage` 只清当前 active page；
- 未来无头 VT 渲染器可按行输出脏区间，DWrite 可只重绘脏行。

### 5.6 Line flags 与 line rendition

`line_flag` 是 row 级 16 位 flags：

- `wrappable` / `wrapped`：reflow 和跨行复制/选择；
- `marked` / `output_start` / `command_end` / `prompt_end` / `user_mark`：OSC 133 /
  shell marks / 用户书签；
- `double_width` / `double_height_top` / `double_height_bottom`：DECDWL/DECDHL。

API：

- `flags(y)` / `set_flags(y)`；
- `add_flags` / `remove_flags` / `has_flag`；
- `is_double_width` / `is_double_height_top` / `is_double_height_bottom`；
- `set_command_exit_code` / `command_exit_code`；
- history 的 `history_flags` / `history_command_exit_code`。

这些状态都放在 row，不重复到每个 cell。

### 5.7 Image fragments

`cell_type::image` 的 cell 只保存 `image_fragment_id`；`screen_buffer::images()` 是
screen-wide `image_table`。

- `image_fragment` 包含 `image_id`、`placement_id`、source rect、layer；
- `write_image(x, y, fragment_id, ...)` 写入 image cell；
- `image_of(cell)` 返回 fragment metadata；
- 实际像素解码/合成是 renderer 责任；libscreen 只保存 placement/引用；
- image cell 仍受 wide/tail/damage 机制管理。

### 5.8 console_state

实现：[libscreen/console_state.hpp](libscreen/console_state.hpp)。

`console_state` 保存不属于 cell 的 console 级状态：

- cursor position/visible/style；
- `wrap_at_eol`、`insert_mode`、`origin_mode`；
- default style/hyperlink；
- scroll top/bottom region；
- tab stops；
- selection range（block normalized）；
- search matches；
- resize 后 clamp cursor、重置 scroll region、清 selection/search。

它与 screen_buffer 分离：cell/page 只保存内容，cursor/tab/margin/selection/search 属于
console 状态。

## 6. Windows API 与 DWrite 边界

### 6.1 `CHAR_INFO`

- scalar cell：primary 就是一个 `WCHAR`；
- type=2（complex/surrogate/grapheme）：整个 cell 投影为 `U+FFFD`；
- trailing/leading：保留宽字符列属性；
- style：投影为 legacy `WORD`；
- `ReadConsoleOutput`/`ReadConsoleOutputCharacter` 不读取 store，也不尝试恢复完整
  cluster；这符合 conhost 的 `Utf16ToUcs2` 行为。

### 6.2 `WriteConsoleOutput`

- 每个 `CHAR_INFO` 是一个 cell、一个 `WCHAR`；
- 不跨 `CHAR_INFO` 边界合并 surrogate pair；
- 孤立 surrogate 规范为 `U+FFFD`；
- 宽字符由 `COMMON_LVB_LEADING_BYTE`/`TRAILING_BYTE` 决定列跨度；
- 这是兼容优先、明确有损的路径。

### 6.3 `WriteConsoleW`

- 先合并合法 surrogate pair；
- 再按完整 grapheme 分割；
- 完整 cluster（含真实 primary）写入 grapheme store；
- cell primary 固定 `U+FFFD`；
- 读回仍按 `CHAR_INFO` 的 `U+FFFD` 规则有损。

### 6.4 DWrite 与无头

- DWrite：scalar 使用 primary；complex 从 store 读取完整 UTF-16 cluster；构建 run 和
  cell→UTF-16 offset map；DWrite glyph cluster 反推 cell cluster；
- 无头：从 scalar primary 或完整 cluster 编码 UTF-8；
- 两条路径共享同一 page/cell/style/damage，只替换最终编码器；
- 窗口模式不需要经过 `CHAR_INFO`，因此能显示 API 读不出的 emoji/cluster。

## 7. 性能与内存

### 7.1 Cell 内存

| 网格 | 16B cell 内存 | 说明 |
| --- | ---: | --- |
| 120 × 30 | 57.6 KB | 单屏 |
| 300 × 100 | 480 KB | 大窗口 |
| 1000 × 200 | 3.2 MB | 大 scrollback 区块 |

对比：

- Ghostty 8B cell 在同样网格是 28.8 KB / 240 KB / 1.6 MB；
- corehost 16B 用 2 倍 cell 内存换取 24 位 offset、8 位 length、32 位 style/hyperlink
  和 8 位 flags；
- conhost 的 per-cell `wstring`/属性通常超过 16B；
- Contour 的 SoA 多数组在小网格上可能低于 16B/cell，但字段分散，Windows API 投影需要
  额外整理。

### 7.2 热路径

- page 是 `bizwen::deque<row_block>`；行内 cell 连续，按行/列直接索引；
- 普通 BMP 写入不分配 cluster；
- complex 写入一次 store append；
- style/hyperlink 是 O(1) ID 查表；
- scroll 只移动 cell 和 row metadata，不搬移 cluster 文本；
- wide 清理是局部 O(1)；
- damage 让渲染器只扫描脏区间；
- 24 位 offset 让 16M UTF-16 unit 不需要频繁重建；重建只在极端容量或显式调用时发生。

### 7.3 与 Ghostty 的复杂度/性能对照

| 维度 | libscreen | Ghostty | 复杂度/差距 |
| --- | --- | --- | --- |
| cell | 16B，字段内联 | 8B packed cell | 顺序扫描带宽约 2× |
| page | `bizwen::deque<row_block>`，每行独立 vector | 连续 page + packed Row | 行内 O(1)；跨行多一次 deque/指针跳转 |
| row metadata | active `row_state` 约 20B；history 8B | 8B packed Row | 小网格影响小，大网格/多 page 有差距 |
| style | 32 位 screen-wide ID，flat_map 写 | page-local u16 style set | 写 O(log n) vs 近似 O(1)；渲染按 ID O(1) |
| hyperlink | 32 位 screen-wide ID，u16 flat_map | page-local offset map | 写 O(log n)，查 ID O(1) |
| grapheme | 连续 `vector<char16_t>`，append_range | 固定 chunk bitmap allocator + map | libscreen 有 vector 扩容/搬迁；Ghostty 更可预测 |
| image | flat_map fragment→ID | 独立 graphics/image 存储 | 写 O(log n)；图像不进入 8B cell |
| scroll | move row vector 到 history；空白行重新分配并填充 | 移动/复用 page，8B cell | 退役行 O(1)；空白填充仍是 O(width) |
| damage | 每行 `[begin,end)` | row dirty 位 + 批量扫描 | libscreen 更精确，Ghostty 扫描更省元数据 |
| selection/search | O(1) block selection；search O(matches) | 专用 selection/search | 小匹配集足够；大量匹配需索引 |

已做的常量优化（C++23）：

- `at()` 返回 `const cell &`，避免每次读取复制 16B；
- `glyph_at`/`glyph_width`/`legacy_at` 使用引用，`line_text` 直接扫描 row span；
- `std::span` 访问 row/cell；
- `std::flat_map` 用于 style/hyperlink/image/cluster remap；
- `append_range` 追加 cluster；
- `std::u16string` 存 hyperlink，避免 `wchar_t`；
- image intern 从线性扫描改为 flat_map 查找；
- `grapheme_store::reserve` 允许调用者预分配。

#### deque 方案

`std::deque<cell>` 不能解决 history 复制：

- deque 是分段连续，单行 cell 不保证能用一个 `std::span` 表示；
- `pop_front()` 会销毁顶部 cell，不能把这一段直接交给 history；
- history 仍要复制/移动 cell；
- corehost 的按行 `CHAR_INFO` 投影需要连续行 span。

可行的是“page = `bizwen::deque<row_block>`”：

```cpp
struct row_block
{
    std::vector<cell> cells;
    row_state state;
};

using page = bizwen::deque<row_block>;
using history = bizwen::deque<history_row>;
```

滚动时：

```cpp
auto &front = page.front();
history.push_back(history_row{
    std::move(front.cells),                       // vector move，O(1)
    history_state{front.state.flags, front.state.command_exit_code},
});
page.pop_front();
page.push_back(row_block{std::vector<cell>(width, blank), row_state{}}); // 当前不复用
```

这样：

- 退役行直接移动 `vector` 指针，不复制 16B cell；
- `history_line()` 仍返回该行连续 `span<const cell>`；
- `deque` 两端 push/pop 不移动其余行，也不使未删除元素的引用失效；
- 16B cell 和 32 位 screen-wide ID 完全保留；
- 单行滚动不再复制 cell。

代价：

- page 不再是单个连续 `vector<cell>`，整页扫描/clear 的局部性下降；
- 每行独立分配，需要 row pool 或 `std::pmr` 才能避免每次 scroll 分配；
- `deque` 随机访问常数高于 vector；
- `scroll_region_up` 需要 rotate/move row block，移动的是 row 对象，不是 cell；
- 需要保证 history trim 后 row block 可回收、span 生命周期规则清晰。

所以：

- `deque<cell>`：不行；
- `deque<row_block>`：可行，能解决 history 行复制，但会把 dense page 变成 row-block page；
- 纯 `deque<page>`：只能优化整页滚动，不能消除单行滚动复制。

结论：

- **复杂度同阶**：page/cell 访问、style/hyperlink 按 ID 查表、cluster append、scroll 都是
  O(1) 或 O(n) 的线性路径；
- **常数不同**：libscreen 16B cell、32B ID、较重的 row metadata 和 history copy 使其
  顺序扫描/滚动约比 Ghostty 慢 1.5–2×，但不会改变渐近复杂度；
- **16B cell / 32 位 ID 限制的是原始扫描带宽**，不是 page 复用；
- **page 复用与当前 cell/ID 目标不冲突**：它可以直接使用 16B cell 和 32 位 screen-wide
  ID。当前 history 选择行复制，是为了保持 `history_line()` 的稳定 span、按行 history
  limit 和简单的 resize 语义，不是 cell 布局的要求；
- 真正的限制是滚动粒度和所有权：如果一次只滚动一行，单纯“整页复用”仍要替换页内
  一行；要完全避免复制，需要 **row-block pool** 或 ring/page-list 存储，而不是只把
  `history_entry` 换成 page 指针；
- 因此 page/row 复用是下一项可做的存储优化；当前目标是先保持同一复杂度，并把可避免
  的 cell 复制和线性查找去掉。

### 7.4 空白行复用收益分析（未改代码）

当前实现不复用空白 row vector：

```text
scroll_up:
  move 顶部 row vector -> history       O(1)
  pop_front
  push_blank:
    vector<cell>(width)                 allocation
    fill width cells with blank         O(width)

history trim:
  pop_front
    destroy vector                       deallocation
```


每次滚入一行，至少一次 row vector 分配；history 超限时再释放一行。
空白行复用可以缓存被 trim/history 淘汰的 row vector：

```text
trim -> clear row, put in pool
push_blank -> take row from pool, fill width cells
```

收益：

- 去掉每行滚动一次的 vector 分配/释放；warmup 后分配次数从 O(scrolled lines)
  降为 O(concurrent rows)；
- 减少 allocator 元数据/锁竞争/内存碎片；
- 对高滚动速率、小宽度、默认分配器更明显；
- **不减少** blank fill 的 O(width) 写带宽，也不减少 history row 的 O(1) move。

成本/风险：

- pool 会保留内存；上限约 `pool_rows * width * 16B`，必须 cap；
- resize 后旧宽度 row 不能直接复用，需要清空 pool；
- 需要定义 history trim 后 row 的归属和 span 生命周期；
- 默认分配器仍用于新 row；若改用 PMR 可进一步降低 allocator 成本。

粗略判断：

- cell 填充是 O(width)，一次 120 列填充约 1.9KB 写入；
- 一次 vector 分配通常远小于 1.9KB 填充，但在高滚动频率下会形成可观察的
  allocator 开销；
- 预期收益主要在 **5%–15%** 的 scroll 路径，宽度越小、滚动越频繁收益越高；
- 不应期待它带来数量级提升；更大的收益仍来自行 move、减少 cell 拷贝和保持行 span。

### 7.5 为什么不是 8B

8B cell 无法同时容纳：

- 16 位 primary；
- 24 位 offset；
- 8 位 length；
- 32 位 style；
- 32 位 hyperlink；
- 8 位 flags。

如果继续压到 8B，必须：

- 把 offset 降回 16 位；
- 把 length 压到 6/7 位；
- 把 style/hyperlink 移出 cell；
- 或使用 page-local ID 并承担重映射。

16B 方案选择了“cell 内一次放够”，把复杂度转移到 screen-wide 的 table/store，而不是
每个 cell 的隐式编码。

## 8. 正确性与测试

测试位于 [libscreen/tests/test_libscreen.cpp](libscreen/tests/test_libscreen.cpp)，CTest 名称
`LibScreen.Reference`。

覆盖：

- `sizeof(cell)==16`、对齐、trivially copyable；
- primary/type/width/extent/style/hyperlink/flags；
- 孤立 surrogate 规范为 `U+FFFD`；
- type=2 的 primary 为 `U+FFFD`；
- type alias 相等；
- grapheme store can_append/append/view/长度上限；
- style interning、默认样式；
- hyperlink interning、默认空链接；
- 屏幕写入、cluster view、rebuild_graphemes；
- wide head/tail、覆盖清理；
- line flags、line rendition、command exit code/history；
- image fragment intern/write/lookup；
- console_state cursor/tabs/scroll region/selection/search；
- fill/clear、damage；
- scroll 进入 history、history 内容；
- alternate 主屏保留；
- resize 保留重叠、清空 history、标脏；
- row used_columns。

运行：

```text
cmake --build build --target test_libscreen --config Debug
ctest --test-dir build -C Debug -R LibScreen --output-on-failure
```

测试通过；Debug ASAN runtime 会复制到测试可执行文件目录，避免 CTest 启动失败。

## 9. 差异、取舍与迁移

### 9.1 与 conhost 的可观察差异

- 内部 cell 不是 `wstring`，但 Console API 投影保持 `U+FFFD` 规则；
- `ReadConsoleOutputCharacter` 的复杂 cell 返回 replacement；
- `WriteConsoleOutput` 不跨 cell 合并 surrogate；
- `WriteConsoleW` 按完整 grapheme 写入；conhost 的某些路径只按 codepoint；
- 24 位 offset/8 位 length 是 corehost 内部能力，不暴露给 API。

### 9.2 取舍

- cell 内存翻倍；
- cluster 需要一次间接访问；
- style/hyperlink 表是 screen-wide，需要管理 ID 生命周期；
- 24 位 offset 仍有 16M 上限；极端输入需要 rebuild 或拒绝；
- screen-wide store 的 rebuild 需要扫描 main/alternate/history，必须在会话锁内完成；
- 窗口 DWrite renderer 尚未实现，当前只提供共享的 cell/page/damage 模型。

### 9.3 迁移顺序

1. 把 `libscreen` cell/page API 接入现有 `screen_buffer` 适配层；
2. 先替换 ASCII/BMP 读写和 damage；
3. 接入 server-side cluster store 和 Console API 投影；
4. 接入无头 VT 序列化；
5. 接入 DWrite renderer；
6. 最后删除旧的 `u32string + offset` 行存储。

### 9.4 cell_width / cell_flags 的扩展性

#### cell_width

当前 4 位 width 只使用 4 个值：

```text
narrow / wide / tail / spacer head
```

还剩 12 个值。终端 cell 的**列语义**通常只有：

- 单列；
- 两列宽字符 head；
- 两列宽字符的 tail；
- 行尾 soft-wrap 的 spacer head。

因此：

- 继续增加 `cell_width` 枚举值的概率不高；
- 即使要增加“3 列宽”“特殊 image cell”等值，4 位也还有足够空间；
- **不建议把 text scale、line rendition、image 类型塞进 width**：
  - text scale 是渲染属性，应放 style/aux；
  - line rendition 是 row 级属性；
  - image 是 content type + side table。

结论：`cell_width` 的 4 位在未来相当长时间内够用，扩展风险低。

#### cell_flags

当前 8 位 flags 使用 4 位：

```text
protected_cell
hyperlink
semantic_input
semantic_prompt
```

还剩 4 位。可能的新 per-cell 布尔状态包括：

- image cell；
- text-scale cell；
- soft-font cell；
- 更细的 semantic/prompt 子类型；
- 某种“marked”状态。

这些功能不一定都该放进 flags：

- line wrap / line rendition / marks 是 **row** 状态；
- selection/search 是 **overlay**，不是持久 cell 状态；
- cursor/tab/margin 是 **screen** 状态；
- image/text-scale/soft-font 需要 payload，不应只靠一个 bit 表达。

因此：

- 当前 8 位 flags 可以再加入最多 4 个纯布尔标志；
- 如果要覆盖 Contour/Ghostty 的图像、text sizing、soft font，仅靠 flags 不够；
- 更合理的扩展是：
  1. 增加 `cell_type` 值（image/scale/soft-font）；
  2. 让 `cell_extent` 在这些类型下解释为 side-table ID，而不是 cluster offset/length；
  3. 用 row_state flags 保存 wrapped/rendition/marks；
  4. selection/search 使用独立 mask。

结论：`cell_flags` 可能会增加少量值，但 8 位不是完整功能覆盖的主要瓶颈；真正的扩展点是
**row flags + cell_type + auxiliary side table**。

### 9.5 当前设计对 conhost/Contour/Ghostty 的功能覆盖

| 功能 | corehost 16B | conhost/WT | Contour | Ghostty |
| --- | --- | --- | --- | --- |
| UTF-16 primary / BMP | 是 | 是 | 是（codepoint） | 是（21-bit codepoint） |
| 非 BMP / 完整 cluster | 是，side store | 是，cell text | 是，cluster pool | 是，grapheme map |
| wide/tail/spacer | 是 | 是 | 是 | 是 |
| truecolor / underline / attribute | 是，style table | 是 | 是 | 是 |
| hyperlink | 是，32 位 ID | 是 | 是 | 是 |
| protected | 是 | 是 | 是 | 是 |
| basic semantic input/prompt | 是（2 flags） | 是 | 部分 | 是（semantic content） |
| command exit status | 是（row metadata） | 是 | 是（line marks） | 是（semantic） |
| line wrapped/marked flags | 是（row_state flags） | 是 | 是 | 是 |
| line rendition（double width/height/top/bottom） | 是（row flags） | 是 | 是（LineFlags） | 部分/扩展 |
| text scaling（OSC 66 等） | 未实现（仅 Contour 支持） | 否/有限 | 是 | 否/有限 |
| inline images（kitty/sixel 等） | 是（cell ref + image table，renderer 未实现） | 图像路径独立于 cell | 是（image fragments） | 是（graphics 路径） |
| soft font / DECDLD | 未实现（仅 Contour 明确支持） | 有/有限 | 是 | 否/有限 |
| selection/search | 是（console_state overlay） | 是 | 是 | 是 |
| cursor/tabs/margins | 是（console_state） | 是 | 是 | 是 |
| scrollback | 是（history） | 是 | 是 | 是 |

### 9.6 按“至少两个实现支持”筛选的功能

判定规则：功能必须有终端实际用途，并且至少被 conhost/WT、Contour、Ghostty 中的两个
实现；满足则 libscreen 需要提供对应的数据模型/API。

#### 已实现

##### 1. line flags / line rendition

**支持者：** WT + Contour。

**用途：**

- `Wrapped`/`Wrappable` 决定 resize 时是否 reflow、复制/选择是否把两行视为同一逻辑行；
- `Marked`/`OutputStart`/`CommandEnd`/`PromptEnd`/`UserMark` 支持 shell integration、
  命令块导航、折叠、scrollbar 标记、用户书签；
- `DoubleWidth`/`DoubleHeightTop`/`DoubleHeightBottom` 支持 DECDWL/DECDHL 老式大字号
  行；它影响 screen→buffer 坐标、渲染和复制范围；
- `command_exit_code` 保存命令结束状态，供失败命令标记/导航。

**libscreen 实现：**

- `line_flag` 16 位枚举；
- `row_state.flags` + `optional<int32_t> command_exit_code`；
- `screen_buffer` 的 `flags/set_flags/add_flags/remove_flags/has_flag`、
  `is_double_width`/`is_double_height_*`、`set_command_exit_code`；
- `scroll_up` 把 flags/exit code 带进 history，`history_flags`/
  `history_command_exit_code` 可查询；
- 这些是 row 状态，不放进 cell。

##### 2. semantic prompt / command status

**支持者：** WT + Contour + Ghostty。

**用途：**

- OSC 133 A/B/C/D 标记 prompt、用户输入、命令输出、命令结束；
- shell 可以导航/折叠/复制命令块；
- scrollbar/命令状态/辅助功能知道命令边界和退出状态；
- cell 级 `semantic_input`/`semantic_prompt` 只能表达局部字符语义，跨行结构需要 row
  marks。

**libscreen 实现：**

- cell flags 保留 `semantic_input`/`semantic_prompt`；
- row `line_flag` 提供 `marked`/`output_start`/`command_end`/`prompt_end`/`user_mark`；
- row `command_exit_code` 保存退出状态；
- history 保留这些 marks，scrollback 中仍可查询。

##### 3. inline image fragments

**支持者：** Contour + Ghostty（WT 的图像也在文本 cell 之外处理）。

**用途：**

- kitty graphics / sixel 图片显示；
- `ls` 图片、neofetch、图片查看器；
- Replace/Below/Above 图层决定文字覆盖图像时是否保留；
- 图像必须跟随滚动/清除，并且复制/选择要知道它占据的 cell。

**libscreen 实现：**

- `cell_type::image`，cell 只保存 `image_fragment_id`；
- `image_table` 保存 `image_fragment`：image/placement ID、source rect、layer；
- `screen_buffer::write_image()` / `image_of()` / `images()`；
- 实际像素解码和 GPU 合成不在 libscreen；renderer 读取 fragment metadata；
- 这样 cell 仍然 16B，不把图片数据塞进每个 cell。

##### 4. selection / search / highlight

**支持者：** WT + Contour + Ghostty。

**用途：**

- 鼠标选择、复制范围、搜索匹配高亮、可访问性；
- 跨 wrapped 行选择需要 line flags 配合；
- 这些是视图状态，不是持久 cell 内容。

**libscreen 实现：**

- `console_state::selection_range`：anchor/caret、block 规范化、`is_selected`；
- `console_state::search_matches`：`std::vector<rectangle>`、`is_search_match`；
- resize 清空 selection/search，避免引用越界；
- 不修改 cell，renderer 在绘制时叠加 selection/search。

##### 5. cursor / tab stops / scroll margins

**支持者：** WT + Contour + Ghostty。

**用途：**

- 光标位置/可见性/形状；
- tab 导航；
- scroll region 决定局部滚动；
- 这些是 console 级状态，不属于 cell。

**libscreen 实现：**

- `console_state` 保存 cursor、wrap/insert/origin mode、default style/hyperlink；
- `scroll_top/scroll_bottom` + `set_scroll_region`/`reset_scroll_region`/
  `in_scroll_region`；
- `tab_stop_table` + `reset_tab_stops`/`next_tab_stop`/`previous_tab_stop`；
- resize 时 clamp cursor、重置 scroll region、清 selection/search。

#### 仍需未来实现

##### 6. text scaling（kitty OSC 66）

**支持者：** 目前只有 Contour 明确实现（`CellScale`、`LineSoA::scales`、
`textScaleExtras`）；Ghostty 没有找到 OSC 66 text sizing 实现，WT 也没有。

**用途：**

- 一个 glyph 占多行高；
- 分数缩放和 block 内对齐；
- 大字号状态图/UI，而不改全局字体。

**为什么不实现：**

- 只有 Contour 一个明确实现，不满足“至少两个”；
- 它需要 block 跨行、band、fraction、alignment、renderer 裁剪；不能只加一个 cell bit。

**未来表示：**

- `cell_type::scaled`；
- `cell_extent` 存 auxiliary table ID；
- aux 保存 block 原点/band/scale/fraction/alignment。

##### 7. soft font / DECDLD

**支持者：** 只有 Contour 明确有 `softGlyphs`/DECDLD 路径；未能从 WT 源码确认完整
软字体实现，Ghostty 也不是核心 cell 功能。

**用途：**

- 下载自定义 glyph/字符集；
- 老式 VT 应用/专用字符。

**为什么不实现：**

- 不满足“至少两个”的明确证据；
- 如果未来确认第二个实现支持，再用 `cell_type::soft_font` + extent 的 glyph ID +
  screen-wide soft glyph table 实现。

#### 仍然不属于 cell 的功能

- cursor/tab/margin/selection/search 已由 `console_state` 实现，不应放 cell；
- 颜色、下划线、hyperlink、wide、protected、grapheme 已在 cell/style/cluster store 中；
- 图像像素数据、字体 atlas、DWrite shaping 属于 renderer/shaper，不属于 cell。

### 9.7 覆盖结论

结论：

- **cell 覆盖了终端文本网格的核心**：UTF-16/BMP、非 BMP cluster、wide/tail、style、
  hyperlink、protected、basic semantic、scrollback；
- **按“至少两个实现支持”已经补上**：
  - line wrapped/marked/rendition：row_state line flags；
  - command exit status：row_state + history；
  - image fragment：cell_type::image + image table；
  - selection/search：console_state；
  - cursor/tabs/scroll margins：console_state；
- **仍不实现**：
  - text scaling/OSC 66：目前只有 Contour 明确实现；
  - soft font/DECDLD：目前只有 Contour 明确实现；
- 图像只实现 cell 引用和 placement metadata；像素解码/GPU 合成属于 renderer；
- 上述扩展大多不扩大 cell：line/selection/cursor 在 row/screen，唯一需要新 cell 类型的是
  image（以及未来的 scaled/soft-font），它们复用 `cell_type` + `cell_extent` 的 aux ID。

### 9.8 用 libscreen 替换 libcorehost screen 的缺口

`libcorehost::screen_buffer` 不是“cell 数组”这么简单，它同时承担：

- Console API 的 buffer/viewport 模型；
- 行级压缩存储和环形滚动；
- `CHAR_INFO`/attribute 读写；
- 文本写入的宽度测量、光标推进和换行；
- VT 输出和 Console API 之间的同步。

`libscreen` 目前更接近“cell/page 参考模型”，直接替换还缺以下层次。

#### 1. buffer 与 viewport

corehost：

```cpp
COORD size;
console_viewport viewport;
```

`size` 是 screen buffer 尺寸，`viewport` 是窗口可见矩形；二者可以不同。
API 还依赖：

- `viewport.rect()/origin()/size()`；
- `set_rect/set_size/set_origin/clamp_to_buffer/snap_to_cursor`；
- `ReadConsoleOutput` 的 buffer 绝对坐标；
- `SetConsoleWindowInfo`/`SetConsoleScreenBufferInfo` 的窗口/缓冲区分离；
- resize 后保持 viewport 合法。

libscreen 目前只有一个 page 几何，没有 viewport/origin。
这是替换的第一阻塞项，需要新增 `screen_viewport` 或复用 `console_viewport`。

#### 2. 行存储和滚动模型

corehost 的 `screen_buffer_row` 有：

- `u32string` 文本 + `_columns` 偏移；
- `_filled`、`_attrs_uniform`、`_single_width_layout` 压缩状态；
- `is_trailing`、`glyph_at`、`glyph_width`；
- `copy_from`、`fill`、`clear_cell`；
- `write_measured_run` 等批量路径；
- `_row_origin` 环形行移动。

libscreen 现在是 `bizwen::deque<row_block>`，每行完整 `vector<cell>`：

- 行内 cell 连续，适合 span/CHAR_INFO；
- 没有 filled/uniform 压缩，空白行也物化 width 个 16B cell；
- 没有 `screen_buffer_row` 的批量 `copy_from` 和 measured run；
- 滚动是 deque move，语义与 corehost 的 ring row 不完全相同。

替换后可能先正确、后性能；需要 row fill/attribute 压缩和 measured-run 快路径，否则大窗口/大量空白行内存和写入会退化。

#### 3. 文本写入、宽度和光标

corehost 的 API：

```cpp
write_glyph(COORD, u32string_view, width_columns, WORD attr)
set_u32(COORD, char32_t, WORD)
try_write_single_width_run(...)
write_text_row(COORD &cursor, u32string_view, WORD, text_measurement_mode, bool)
```

它们负责：

- `char_width_for_mode` 的宽度策略；
- ambiguous wide；
- 单宽 fast path；
- 行尾换行/滚动信号；
- cursor 推进；
- grapheme cluster 的完整写入。

libscreen：

- `write_scalar`/`write_cluster`/`write_codepoint` 都要求调用者给 width；
- 没有 `text_measurement_mode`；
- 没有 `write_text_row` 的 cursor/row_end；
- console_state 有 cursor，但没有和写入路径绑定。

需要新增：

- `write_measured_glyph`/`write_measured_run`；
- 文本宽度策略接口（或明确由 corehost 传入 widths）；
- `write_text_row` 的 cursor/wrap 语义；
- `char32_t`→`char16_t` 的 bulk 桥接。

#### 4. CHAR_INFO 和 attribute API

corehost 有一套完整的矩形/线性读写：

```text
row_to_ci / row_from_ci
read_wchars / read_wchars_linear
read_attrs / read_attrs_linear
write_char32 / write_char32_linear
write_attr_seq / write_attr_seq_linear
fill_char / fill_attr / fill_attrs / set_attr / attr_at
```

libscreen 目前只有：

- `legacy_at`；
- `write_legacy_scalar`；
- `fill_rect`/`clear`。

缺少：

- `CHAR_INFO` 的 leading/trailing DBCS 属性投影；
- attribute-only 写入（不改字符）；
- character-only 写入（保留 attribute）；
- 矩形短读/短写、limit、越界裁剪；
- `ReadConsoleOutputCharacter/Attribute` 的连续 buffer 语义；
- `WriteConsoleOutput` 的 DBCS/宽字符重建。

另外，libscreen 的 cell 只有 style ID，legacy `WORD` 通过 style table 投影；需要保证任意 Console API 属性写入不会让 style table 失控，并在 `legacy_at` 正确补 DBCS/leading/trailing 位。

#### 5. 2D scroll / copy

corehost：

```cpp
scroll(SMALL_RECT source, SMALL_RECT clip, bool use_clip,
       COORD destination, char32_t fill_char, WORD fill_attr)
```

支持：

- 任意矩形 source/clip；
- 目标 origin 可横/纵移动；
- 源/目标重叠；
- 超出部分填 fill_char/fill_attr；
- `ScrollConsoleScreenBuffer` 的 clip 语义。

libscreen 只有：

- 全页 `scroll_up`；
- 纵向 `scroll_region_up`；
- 没有 2D copy、目标 origin、fill char/attr 组合。

这是 Console API 兼容的第二个阻塞项。

#### 6. console_state 的缺口

libscreen `console_state` 目前有：

- cursor position/visible/style；
- wrap/insert/origin；
- default style/hyperlink；
- tab stops；
- scroll region；
- block selection；
- search matches。

corehost `console_state` 还包含：

- input/output mode 位掩码；
- code pages；
- title/original title；
- font/face/weight/size；
- 16 色 color table；
- window handle、display mode、mouse buttons；
- max window size；
- alt saved cursor、DECSC saved cursor；
- default/popup attributes；
- selection info（`CONSOLE_SELECTION_INFO`）；
- history 设置、DOSKEY aliases；
- text measurement mode / ambiguous wide；
- DEC line drawing；
- VT alt buffer active 状态。

如果替换 screen 但保留 corehost `console_state`，需要 adapter 同步 cursor/scroll region/attrs；如果连 console_state 一起替换，则需要补齐这些字段和 Console API 行为。

#### 7. selection / search / clipboard

libscreen 只有 block selection 和 search match 查询：

- 没有 line selection；
- 没有 `CONSOLE_SELECTION_INFO` 模式/锚点映射；
- 没有选择到剪贴板的复制；
- 没有鼠标选择和 QuickEdit 状态；
- 没有 search + selection 的交互。

corehost 的 selection 由 `console_state.selection_info` 驱动，替换时需要把这些语义接到 screen buffer。

#### 8. alternate / history / VT 同步

libscreen 有 main/alternate/history，corehost screen_buffer 没有 alt/history，alt 由 `console_state.vt_alt_buffer_active` 和终端同步处理。
替换时必须明确：

- 哪些 VT 序列改 main；
- 哪些只透传终端；
- alt 进入/退出时 cursor、viewport、selection 如何保存；
- history 是否暴露给 Console API；
- scrollback 与 `ReadConsoleOutput` 的边界。

#### 9. 错误/越界语义

corehost API 是 `noexcept` 且宽容：

- 无效坐标读字符返回空格；
- 无效属性返回 0x07；
- 短矩形截断；
- 写越界按实际写入长度返回。

libscreen 大量使用 `assert` 表示程序 BUG，直接调用会在 API 越界时终止。
adapter 必须先在 API 边界做 clamp/validate，或为 libscreen 增加 `try_at`/clamped read API。

#### 10. 性能和行压缩

corehost 的 row 是高度优化的：

- `u32string` 连续文本；
- 单宽 layout 快路径；
- filled/uniform attribute 压缩；
- `copy_from` 批量；
- ring row 滚动。

libscreen 的 row_block 当前是“每行完整 16B cell vector”：

- 正确性/行 span 好；
- 但空白行也占 width×16B；
- 没有属性 uniform 压缩；
- 没有 measured-run 批量写入；
- 行 block 独立分配。

替换后应先 benchmark，再决定是否加：

- row lazy fill/uniform attrs；
- measured-run fast path；
- ring/buffer viewport；
- row pool / PMR。

#### 建议的替换顺序

1. **adapter 层**：`COORD`/`SMALL_RECT`/`CHAR_INFO`/`WORD` 到 libscreen 类型；
2. **buffer + viewport**；
3. **rect read/write/fill + attribute-only/character-only**；
4. **2D scroll/copy**；
5. **width + write_text_row + cursor**；
6. **console_state 同步**；
7. **selection/alt/history**；
8. **row 压缩和性能**。

其中 1–5 是 Console API 兼容的 P0；6–7 是行为兼容；8 是性能回归修复。

## 10. 结论

16 字节 cell 是 corehost 面向 Windows 的一个明确取舍：

- 16 位 UTF-16 primary 让 BMP 路径直接对齐 Console API/DWrite；
- type=2 统一表示多 unit 内容，primary 为 `U+FFFD`，不伪造 surrogate cell；
- 24 位 offset + 8 位 length 让完整 cluster 的 side store 足够大且没有 minus-one 编码；
- 32 位 style/hyperlink 让 page/scrollback 复制不需要重映射；
- dense page + row damage + history 让屏幕缓冲区可以在无头 VT 和未来 DWrite 之间共享；
- Console API 的有损投影只发生在边界，内部始终保留完整 cluster。

与 conhost、Contour、Ghostty 的对比说明：没有一种 cell 结构是普遍最优的；conhost 优先
Console 语义，Contour 优先 SoA 扫描，Ghostty 优先紧凑 8B cell，corehost 则优先 Windows
UTF-16/24 位 cluster 地址/32 位共享 ID 的组合。16B cell 的代价是内存，收益是字段完整、
边界清晰和后续 DWrite/Console API 共享。

## 11. 代码位置

### libscreen

- `libscreen/cell.hpp`
- `libscreen/console_state.hpp`
- `libscreen/grapheme_store.hpp`
- `libscreen/hyperlink.hpp`
- `libscreen/image.hpp`
- `libscreen/screen_buffer.hpp`
- `libscreen/style.hpp`
- `libscreen/tests/test_libscreen.cpp`

libscreen 是 header-only INTERFACE target；实现全部在 `.hpp`。

### 参考实现

- `terminal/src/buffer/out/OutputCell.hpp`
- `terminal/src/host/consoleInformation.cpp`
- `terminal/src/host/directio.cpp`
- `terminal/src/host/output.cpp`
- `terminal/src/types/convert.cpp`
- `terminal/src/host/VtIo.cpp`
- `contour/src/vtbackend/grid/LineSoA.hpp`
- `contour/src/text_shaper/DirectWriteShaper.cpp`
- `contour/src/text_shaper/ClusterSpans.hpp`
- `ghostty/src/terminal/page.zig`
- `ghostty/src/terminal/size.zig`
- `ghostty/src/unicode/grapheme.zig`
