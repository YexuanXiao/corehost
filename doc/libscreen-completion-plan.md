# libscreen 完善计划

Status: Phase 0–7 implemented; Phase 8–9 pending. See "执行记录" below.

## 1. 目标

把 `libscreen` 从“cell/page 参考实现”完善为“可以承载 corehost 屏幕语义的独立库”，但不立即替换
`libcorehost::screen_buffer`。

完成后的 `libscreen` 应能：

- 表达 screen buffer 与可见 viewport 的分离；
- 支持 Console API 所需的矩形/属性/字符读写；
- 支持带宽度策略的文本写入和 cursor 推进；
- 提供 `CHAR_INFO`/legacy `WORD` 的稳定投影；
- 提供 2D scroll/copy；
- 提供完整的 console 状态模型；
- 支持 selection、alt、history 的明确边界；
- 保持 header-only、UTF-16、16B cell、32 位 screen-wide ID；
- 用测试证明与 corehost screen 的可观察行为兼容。

## 2. 非目标

本计划不做：

- 不修改 `libcorehost` 的生产路径；
- 不替换 `api_handlers`、`vt_msg_dispatch`、`output_buffer`、`viewport_render`；
- 不实现 Windows API 本身；
- 不实现 DWrite/GPU renderer；
- 不改变 16B cell、32 位 style/hyperlink ID 的核心取舍；
- 不追求与 Ghostty 相同的 8B cell 或 page-local ID。

替换是后续独立项目；本计划只负责把 `libscreen` 准备到“可以接 adapter”的程度。

## 3. 当前状态

已有：

- `cell.hpp`：16B cell、type/width/flags mask、extent/style/hyperlink；
- `grapheme_store.hpp`：UTF-16 cluster store；
- `style.hpp`/`hyperlink.hpp`：screen-wide table；
- `image.hpp`：image fragment metadata；
- `screen_buffer.hpp`：
  - `bizwen::deque<row_block>` page；
  - main/alternate/history；
  - line flags/rendition/command status；
  - damage；
  - 单 cell/glyph 写入；
  - 有限 UTF-16 读取；
- `console_state.hpp`：
  - cursor；
  - tab stops；
  - scroll region；
  - block selection；
  - search matches。

缺口：

- 没有 buffer/viewport 分离；
- 没有 `CHAR_INFO`/矩形/属性 API；
- 没有文本宽度策略和 bulk text writer；
- 没有 2D scroll/copy；
- 没有完整 console_state；
- 没有 selection/copy 和 corehost 语义；
- 没有兼容性测试；
- 行存储没有 corehost 的 filled/uniform 压缩。

## 4. 模块规划

目标文件布局：

```text
libscreen/
  cell.hpp
  grapheme_store.hpp
  style.hpp
  hyperlink.hpp
  image.hpp
  geometry.hpp          // new: coord/rect/viewport
  screen_buffer.hpp     // buffer + page + row
  text_writer.hpp       // new: width policy + write_text_row
  legacy_projection.hpp // new: char_info/legacy attributes
  console_state.hpp     // expanded console state
  selection.hpp         // new: selection/search/copy
  tests/
    test_libscreen.cpp
    test_geometry.cpp
    test_screen_api.cpp
    test_legacy_projection.cpp
    test_text_writer.cpp
    test_console_state.cpp
    test_selection.cpp
    test_compat_corehost.cpp
```

所有模块保持 header-only；`libscreen` 仍是 CMake INTERFACE target。

## 5. 缺口与实现方式

### 5.1 geometry/viewport

目标 API：

```cpp
struct coord
{
    std::int32_t x{};
    std::int32_t y{};
};

struct rect
{
    std::int32_t left{};
    std::int32_t top{};
    std::int32_t right{};  // inclusive
    std::int32_t bottom{}; // inclusive
};

class viewport
{
  public:
    rect bounds() const noexcept;
    coord origin() const noexcept;
    coord size() const noexcept;
    bool contains(coord) const noexcept;
    bool set_rect(rect, coord buffer_size) noexcept;
    bool set_origin(coord, coord buffer_size) noexcept;
    bool set_size(coord, coord buffer_size) noexcept;
    bool clamp_to_buffer(coord buffer_size) noexcept;
    bool snap_to_cursor(coord cursor, coord buffer_size) noexcept;
};
```

实现要点：

- 所有坐标使用 buffer 绝对坐标；
- viewport 始终 clamp 到 buffer；
- `size` 至少 1×1；
- 提供 `to_relative`/`to_absolute`；
- `rectangle`（现有 x/y/w/h）与 `rect` 的转换放在 geometry；
- 不引入 Windows `COORD`/`SMALL_RECT`，adapter 在外部转换。

验收：

- viewport 覆盖/部分/越界/负尺寸测试；
- `snap_to_cursor` 只移动 origin，不缩放；
- resize 后 viewport 合法。

### 5.2 screen_buffer 的 buffer/viewport API

在 `screen_buffer` 增加：

```cpp
coord buffer_size() const;
coord viewport_origin() const;
coord viewport_size() const;
viewport &view();
const viewport &view() const;
void set_buffer_size(coord);
void set_viewport(rect);
bool snap_viewport_to_cursor(coord);
```

实现要点：

- page 保存 buffer 尺寸；viewport 只是逻辑矩形；
- 所有 Console API 坐标先按 buffer 校验，再映射到 page；
- 现有 `width/height` 保留为 buffer 尺寸；
- viewport 不改变 cell 存储；
- resize 保留重叠 cell，裁掉越界部分，viewport 重新 clamp。

验收：

- buffer 大于/小于 viewport；
- resize 同时改变 buffer 和 viewport；
- cursor 在 viewport 外时 snap；
- 读取/写入使用 buffer 绝对坐标。

### 5.3 矩形/字符/属性 API

新增 `legacy_projection.hpp` 和 `screen_buffer` API：

```cpp
struct char_info
{
    char16_t character{u' '};
    std::uint16_t attributes{0x07};
};

enum class char_info_projection
{
    replacement, // complex -> U+FFFD
    full_cluster // 只用于内部调试
};

char_info project(const cell &, char_info_projection) const;
std::optional<cell> try_at(coord) const; // 不 assert 的 API 边界
```

需要实现：

```text
read_char_info(rect, span<char_info>)
write_char_info(rect, span<const char_info>)
read_wchars(start, span<char16_t>)
read_attrs(start, span<uint16_t>)
write_chars(start, u16string_view)
write_attrs(start, span<const uint16_t>)
fill_chars(rect, char16_t)
fill_attrs(rect, uint16_t)
set_attr(coord, uint16_t)
attr_at(coord)
```

实现要点：

- 公开 API 不 assert 越界；先 clamp，短读/短写返回实际数量；
- `char_info` 投影遵循 `U+FFFD` 规则；
- 宽字符 head 设置 leading，tail 设置 trailing；
- legacy attribute 用 style table 的 `legacy_attributes`；
- attribute-only 操作只改 style，不替换字符；
- 字符-only 操作保留原 style；
- 提供 `try_at` 给 adapter，内部仍可用 assert 的 `at`。

验收：

- 矩形裁剪、短读、短写、越界；
- BMP、宽字符、surrogate-pair cluster 的 `U+FFFD` 投影；
- DBCS leading/trailing 位；
- attribute-only 不改变字符，character-only 不改变属性。

### 5.4 文本宽度和 text_writer

新增 `text_writer.hpp`：

```cpp
enum class text_measurement
{
    console,
    unicode,
};

struct width_options
{
    text_measurement mode{text_measurement::console};
    bool ambiguous_is_wide{false};
};

int glyph_width(char32_t, width_options) noexcept;
int grapheme_width(std::u16string_view, width_options) noexcept;
```

`screen_buffer` 增加：

```cpp
struct text_write_result
{
    std::size_t consumed{};
    bool row_end{};
};

text_write_result write_text_row(
    coord &cursor,
    std::u16string_view text,
    std::span<const std::uint8_t> widths,
    cell_style style,
    width_options options);

bool write_glyph(coord, std::u16string_view, int columns, cell_style);
```

实现要点：

- 内部 UTF-16；corehost `u32string_view` 由 adapter 转成 `u16string`；
- widths 由调用者提供或由 `glyph_width` 计算；
- 单宽 run fast path；
- 宽字符写 head/tail；
- 行尾返回 `row_end`，不直接 scroll；
- cursor 推进只由 writer 负责；
- 不把 cursor 放进 cell。

验收：

- BMP/宽字符/非 BMP/组合字符；
- ambiguous wide 两种模式；
- 行尾、跨行、插入模式；
- `row_end` 和 consumed 与 Console API 语义一致。

### 5.5 2D scroll/copy

新增：

```cpp
void copy_rect(rect source, rect clip, bool use_clip, coord destination);
void scroll_rect(rect source, rect clip, bool use_clip, coord destination,
                 char16_t fill_char, std::uint16_t fill_attr);
```

实现要点：

- source/destination 可横向/纵向偏移；
- 重叠时按方向安全复制；
- clip 与 source 的交集；
- 目标未覆盖区域填 fill_char/fill_attr；
- 宽字符 head/tail 在复制后保持关系；
- 共享 cluster/image 引用不改写 store；
- damage 标记源/目标区域。

验收：

- 同向/反向/重叠移动；
- 部分 clip；
- fill char/attr；
- 宽字符边界不被拆坏；
- damage 范围正确。

### 5.6 console_state 扩展

目标是把 console 级状态拆成模块，不把 Windows 类型带进 libscreen：

```cpp
struct console_modes
{
    std::uint32_t input{};
    std::uint32_t output{};
    std::uint32_t input_private{};
};

struct console_font
{
    std::uint32_t index{};
    coord size{8, 12};
    std::uint32_t family{};
    std::uint32_t weight{400};
    std::u16string face_name{u"Consolas"};
};

struct console_colors
{
    std::array<std::uint32_t, 16> table{};
};

struct console_title
{
    std::u16string title;
    std::u16string original;
};

class console_state
{
    // cursor, modes, font, colors, title
    // default attrs, max window, text measurement
    // scroll region, tabs, saved cursor
    // alt saved cursor, dec line drawing
    // selection, history settings
};
```

实现要点：

- 保留现有 cursor/tab/scroll/selection/search API；
- 增加 input/output mode、code page、title、font、color table；
- 增加 `default_attributes`、`max_window_size`；
- 增加 `saved_cursor`、`alt_saved_cursor`；
- 增加 `dec_line_drawing_mode`；
- selection 使用 `selection.hpp`；
- 不引入 `HWND`/`COLORREF`/`CONSOLE_SELECTION_INFO`，用平台无关的等价类型；
- corehost adapter 负责 Win32 结构转换。

验收：

- 模式默认值/回读；
- title/font/color table；
- cursor save/restore；
- resize 后 clamp；
- alt 状态保存/恢复。

### 5.7 selection / search / copy

新增 `selection.hpp`：

```cpp
enum class selection_mode
{
    block,
    line,
};

struct selection_state
{
    bool active{};
    selection_mode mode{selection_mode::block};
    coord anchor{};
    coord caret{};
};

class selection_model
{
    void begin(coord, selection_mode);
    void extend(coord);
    void clear();
    bool contains(coord) const;
    std::u16string copy(const screen_buffer &) const;
};
```

实现要点：

- block 与 line 选择；
- 选择范围按 buffer 坐标；
- 跨 wrapped 行复制；
- cluster/宽字符正确处理；
- search matches 使用矩形集合，提供 `is_match`；
- 不把 selection 写进 cell。

验收：

- 正向/反向选择；
- 跨行/跨 wrapped 行；
- 宽字符/emoji 复制；
- clear 和 resize；
- search 命中/未命中。

### 5.8 alternate / history 边界

明确：

- `screen_buffer` 保留 main/alternate；
- `console_state` 保存 alt saved cursor；
- history 只由 `screen_buffer` 管理，不进入 Console API 的 buffer 坐标；
- `set_alternate` 不改变 viewport，由 console 层决定；
- 退出 alt 恢复 main cursor/attrs；
- history trim/resize 规则不变。

验收：

- alt 切换后 main 内容保留；
- alt cursor 保存/恢复；
- history 不污染 `ReadConsoleOutput`；
- resize 清 history，alt 保留。

### 5.9 行存储压缩/性能

当前 `row_block` 是完整 `vector<cell>`。逐步增加：

- `row_fill`：整行同一 blank/style 时延迟物化；
- `attrs_uniform`：整行同一属性；
- `single_width_layout`：全单宽时减少宽度查询；
- `copy_from`：行内批量复制；
- measured-run fast path；
- 可选 row pool（默认分配器，先不做）。

实现顺序：

1. 先 benchmark 当前 row_block；
2. 加 `row_flags`/`fill` 状态；
3. 加 uniform attrs；
4. 加 fast path；
5. 再评估 row pool/PMR。

验收：

- 空白行不分配 width 个 cell（或显著减少）；
- 全屏填充/scroll 分配次数下降；
- 行 span/CHAR_INFO 行为不变；
- 性能不低于当前 row_block 基线。

## 6. 分阶段实施

### Phase 0：契约与基线

- 固定公开类型（coord/rect/viewport）；
- 建立 corehost 行为测试清单；
- 记录当前性能基线（120×30、300×100、1000 history）。

产出：`doc/libscreen-completion-plan.md`、基线测试。

### Phase 1：geometry + viewport

- `geometry.hpp`；
- viewport 实现；
- `screen_buffer` buffer/viewport 分离；
- resize/snap 测试。

### Phase 2：矩形/属性 API

- `char_info` 投影；
- read/write/fill chars/attrs；
- `try_at` 宽容访问；
- DBCS/`U+FFFD` 测试。

### Phase 3：text writer + cursor

- width policy；
- `write_text_row`；
- 单宽 fast path；
- cursor/wrap 测试。

### Phase 4：2D scroll/copy

- `copy_rect`/`scroll_rect`；
- overlap/clip/fill；
- damage/宽字符测试。

### Phase 5：console_state 扩展

- modes/font/colors/title；
- saved cursor/alt；
- text measurement/dec line drawing；
- selection 接入。

### Phase 6：selection/search

- block/line selection；
- copy；
- search match；
- 跨 wrapped 行。

### Phase 7：alt/history 边界

- alt cursor；
- history 不进入 Console API；
- resize 规则。

### Phase 8：性能

- row fill/uniform attrs；
- measured run；
- benchmark；
- 评估 row pool/PMR。

### Phase 9：corehost 兼容 adapter

只在测试/适配层添加：

```text
libscreen/tests/corehost_screen_adapter.hpp
```

提供：

- `COORD`/`SMALL_RECT` ↔ `coord`/`rect`；
- `CHAR_INFO` ↔ `char_info`；
- `console_state` 字段同步；
- `api_handlers` 所需的调用面。

不修改 `libcorehost` 生产代码。

## 7. 测试策略

### 单元测试

- geometry/viewport；
- cell/UTF-16/cluster；
- row/CHAR_INFO 投影；
- text writer；
- rect read/write；
- 2D scroll/copy；
- console_state；
- selection/search。

### 兼容测试

按 corehost 行为建立场景：

1. `WriteConsoleOutputCharacter/Attribute` 短写；
2. `ReadConsoleOutputCharacter/Attribute` 越界；
3. `FillConsoleOutputCharacter/Attribute`；
4. `ScrollConsoleScreenBuffer` clip/overlap；
5. `SetConsoleScreenBufferSize` + viewport；
6. `SetConsoleWindowInfo`；
7. DBCS leading/trailing；
8. emoji cluster → `U+FFFD`；
9. wrap/insert/origin；
10. alt cursor save/restore。

### 性能测试

- 120×30、300×100；
- 1000 行 history；
- BMP 写入、宽字符、cluster、fill、scroll、resize、CHAR_INFO 读取；
- 记录 allocation 次数、吞吐、内存。

## 8. 验收标准

`libscreen` 完成条件：

- viewport/buffer API 可用；
- Console API 所需的矩形/属性/字符操作齐全；
- text writer/宽度策略/cursor 可用；
- 2D scroll/copy 可用；
- console_state 覆盖 screen 相关状态；
- selection/copy/search 可用；
- alt/history 边界明确；
- 兼容测试全绿；
- benchmark 不低于当前基线；
- `libcorehost` 未被修改。

## 9. 风险与决策

### 16B cell vs Ghostty 8B

不改变 16B。接受约 2× 顺序扫描带宽，优化常量和行存储。

### style table 属性爆炸

legacy `WORD` 属性通过 style table 投影；需要 benchmark 大量不同属性的写入，必要时加 legacy attribute fast path。

### row_block vs corehost row

corehost 的 `screen_buffer_row` 有 filled/uniform/measured-run 优化；libscreen 先保证 API 正确，再补压缩。不要为了性能提前复杂化 API。

### asserts vs API 宽容

内部不变量继续 assert；Console API 边界使用 `try_*`/clamp。不要用 assert 处理用户输入。

### console_state 重复

先保留 corehost `console_state` 作为 adapter 目标；`libscreen::console_state` 只扩展 screen 相关状态。整体替换 console_state 是后续项目。

## 10. 优先级总表

| 模块 | 优先级 | 依赖 |
| --- | --- | --- |
| geometry/viewport | P0 | 无 |
| screen_buffer buffer/viewport | P0 | geometry |
| char_info/rect/attrs | P0 | buffer/viewport |
| text_writer/width/cursor | P0 | buffer/viewport |
| 2D scroll/copy | P0 | rect/attrs |
| console_state modes/font/title | P1 | 无 |
| selection/copy | P1 | text_writer/console_state |
| alt/history 边界 | P1 | viewport/console_state |
| row fill/uniform 压缩 | P2 | benchmark |
| corehost adapter | P1 | P0/P1 |

## 11. 执行记录

### 已完成

- **libinterop**：新增 ABI 兼容的 `coord`、`small_rect`、`char_info`、
  `console_screen_buffer_info`、`console_cursor_info`、`console_selection_info`、
  `console_font_info`/`console_font_infoex`；有大小/offset 静态断言；不包含 Windows 头文件。
- **libunicode**：`libscreen` 通过 `unicode::unicode` 获得 wcwidth 等价的
  `wcswidth` 和 grapheme cluster 宽度；UTF-16/UTF-32 转换使用
  `unicode::convert_to`、`unicode::encoder<char16_t>`、`unicode::decoder<char16_t>`，
  不再手写 surrogate 编码/解码；没有使用 Windows 头文件。
- **Phase 0**：新增 `libscreen/bench/bench_libscreen.cpp` 与 `bench_libscreen` target；
  Debug/ASAN 基线可运行（数字仅作回归基线，不作为发布性能）。
- **Phase 1**：`geometry.hpp` 提供 `coord`/`rect`/`viewport`；`screen_buffer` 增加
  buffer/viewport 分离、`view()`、`set_viewport()`、`snap_viewport_to_cursor()`。
- **Phase 2**：`legacy_projection.hpp` + `screen_buffer` 增加 `try_at`、
  `char_info_at`、`read/write_char_info`、`read/write_chars`、`read/write_attrs`、
  `fill_chars/fill_attrs`、`set_attr/attr_at`；支持 DBCS leading/trailing。
- **Phase 3**：`text_writer.hpp` 提供 `console`/`graphemes`/`wcswidth` 三种
  `text_measurement`，提供 `char_width_for_mode()`、`text_width_for_mode()`；
  `grapheme_range`/`grapheme_iterator` 每次输出一个
  `grapheme_cluster { std::span<const char16_t> text, int width }`，不再返回
  `std::vector<grapheme_span>`；`screen_buffer` 增加 `write_text_row`、`write_glyph`。
- **Phase 4**：`copy_rect`、`scroll_rect`，支持 clip、重叠、fill char/attr。
- **Phase 5**：`console_state` 增加 modes/code pages/title/font/colors/max window/
  default attributes/saved cursor/alt cursor/DEC line drawing/text measurement；
  `save_cursor/restore_cursor/save_alt_cursor/restore_alt_cursor`。
- **Phase 6**：selection 支持 block/line、`selected_text()`；search 查询保留。
- **Phase 7**：alt/history 行为有测试；history 保留 compact `history_state`。

### 偏差

- `selection` 没有单独 `selection.hpp`，目前放在 `console_state.hpp`；API 已覆盖 block/line
  selection 和 copy。
- `write_text_row` 的 `graphemes` 模式已使用 libunicode 的 grapheme segmenter；
  `console`/`wcswidth` 模式按 codepoint 写入；仍缺 corehost 的 measured-run 快路径。
- `copy_rect`/`scroll_rect` 为正确性优先实现，仍有临时 cell 缓冲；Phase 8 再优化。
- Phase 8（row fill/uniform attrs/PMR/pool）未做。
- Phase 9（corehost 兼容 adapter）未做；目前只完成测试侧类型/ABI 基础。

### 验证

```text
LibScreen.Reference  Passed
LibScreen.Features   Passed
```

`libscreen` 不包含 `windows.h`；`libinterop` 也不包含 Windows 头文件。

## 12. 完成定义

当以下条件全部满足时，计划完成：

- `libscreen` 能独立表达 corehost screen 的 buffer/viewport 模型；
- 能用测试覆盖 Console API 的字符/属性/矩形/滚动行为；
- 能通过 adapter 把 corehost 的 `screen_buffer` 调用映射到 libscreen；
- 没有修改 `libcorehost` 生产代码；
- 性能不出现明显回归；
- 设计文档、测试、benchmark 和迁移说明齐全。
