# corehost 16 字节 cell 屏幕缓冲区技术报告

## 摘要

本报告重新设计 corehost 的屏幕缓冲区，采用 **16 字节 cell**：

- 16 位 UTF-16 primary；
- 16 位 `cell_info`：
  - bits 0-2 type；
  - bits 3-5 width；
  - bits 6-15 flags；
- 32 位 `cell_extent`（实际打包 24 位 offset + 8 位 length）；
- 32 位 `cell_style`；
- 32 位 `cell_hyperlink`。

`complex`/`surrogate`/`grapheme` 都是 `cell_info` 中 type 位 `1u << 1` 的别名，统一表示
“这个 cell 需要多个 UTF-16 code unit”。当 type 为 complex 时，cell 的 primary 固定为
`U+FFFD`；真实 primary 和完整 cluster 保存在 screen-wide 的 `std::vector<char16_t>` 中，
由 `cell_extent` 内部的 24 位 offset 和 8 位 length 定位。

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
32 位 hyperlink、cell_info 中 10 位 flags** 同时成立，并把隐式编码全部去掉。

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

- 16 字节 cell；primary + cell_info + 三个 32 位字段；
- UTF-16 primary，BMP 直接保存，complex 时为 `U+FFFD`；
- 24 位 cluster offset；
- 8 位 cluster length；
- 32 位 screen-wide style ID；
- 32 位 screen-wide hyperlink ID；
- `cell_info` 中 10 位 flags；
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
primary 16 | cell_info 16 (type 3 | width 3 | flags 10)
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
| `primary_` | 16 | UTF-16 code unit；type=complex 时为 `U+FFFD` |
| `info_` | 16 | type 3 位 + width 3 位 + flags 10 位 |
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
    cell_info info_;
    cell_extent extent_;
    cell_style style_;
    cell_hyperlink hyperlink_;
};
```

`cell_info` 把原 `type_`/`width_`/`flags_` 三个字段合并成一个 16 位值；type/width/flags
的位段由 `cell_info_ops` 的 mask 和查询函数维护。`static_assert` 锁定
`sizeof(cell)==16`、对齐、trivially copyable 和 standard-layout。

### 4.2 cell_info 的统一语义

```cpp
enum class cell_info : std::uint16_t
{
    scalar = 1u << 0,
    complex = 1u << 1,
    surrogate = 1u << 1,
    grapheme = 1u << 1,
    background = 1u << 2,

    wide = 1u << 3,
    tail = 1u << 4,
    spacer_head = 1u << 5,

    protected_cell = 1u << 6,
    hyperlink = 1u << 7,
    semantic_input = 1u << 8,
    semantic_prompt = 1u << 9,
};
```

`cell_info_ops` 提供：

- `cell_type_mask = 0x07`；
- `cell_width_mask = 0x38`；
- `cell_flags_mask = 0xFFC0`；
- `type_of`/`width_of`/`flags_of`；
- `is_type_*`、`is_width_*`、`is_flags_*`；
- `combine`/`with_type`/`with_width` 辅助组合。

规则：

- `complex`/`surrogate`/`grapheme` 是同一个 type 位 `1u << 1`；
- complex 类型时 primary 固定为 `U+FFFD`；
- type 位不负责区分 surrogate 和 grapheme；如果未来需要区分，使用 flags 或 side
  metadata；
- width 是离散字段而不是组合位，因此 `is_width_*` 使用掩码后比较；
- flags 可组合，`is_flags_*` 使用位测试。

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
- hyperlink table 保存 URI 和 id；
- cell 只保存 32 位 ID；
- Console API 只读取 legacy 投影，不读取 RGB 表；
- DWrite/窗口渲染读取完整 style 表。

### 4.5 cell API

`libscreen/cell.hpp` 提供：

- `blank`、`background`、`tail`、`spacer_head`；
- `scalar`（BMP code unit，孤立 surrogate 规范为 `U+FFFD`）；
- `complex`/`surrogate`/`grapheme`（offset/length）；
- `primary()`、`text_unit()`、`style()`、`hyperlink()`、`info()`；
- `offset()`、`length()`；`cell_extent` 通过 `make_cell_extent()`/
  `extent_offset()`/`extent_length()` 编码；
- `has_cluster()`、`is_blank()`、`is_wide()`、`is_continuation()`、`is_spacer_head()`；
- `has(cell_info)` 查询 flags；
- `with_info()`、`with_style()`、`with_hyperlink()`、`with_flag()`、`with_cluster_ref()`；
- `static_assert(sizeof(cell) == 16)`、trivially copyable、standard-layout。

## 5. 屏幕缓冲区结构

实现：[libscreen/screen_buffer.hpp](libscreen/screen_buffer.hpp)、
[libscreen/screen_buffer.cpp](libscreen/screen_buffer.cpp)。

### 5.1 Page 与 Row

```text
page:
  Cell[width * height]      // dense, no per-cell allocation
  row_state[height]         // used_columns, flags, dirty range

row_state:
  used_columns
  flags
  dirty_begin / dirty_end
```

- page 是 dense 的 16B cell 数组；
- row 不拥有 cluster 文本或 style；
- `used_columns` 记录实际使用列；
- damage 是每行的半开区间 `[dirty_begin, dirty_end)`；
- 初始 page 全部行标记为 dirty，保证首次渲染完整。

### 5.2 Main、Alternate 与 History

- `screen_buffer` 拥有 main page；
- `set_alternate(true)` 创建/切换到 alternate page；
- alternate page 不记录 scrollback；
- main page 满屏 `scroll_up` 时，把顶部行复制到 `std::deque<line>` history；
- history 上限由构造函数 `history_limit` 控制；
- history line 只保存 cell，不保存 row metadata；
- resize 清空 history，避免宽高变化后旧行宽度不一致；
- style/hyperlink/grapheme store 是 screen-wide 的，main/alternate/history 共享。

### 5.3 写入、宽字符和 cluster

- `write_scalar`：写一个 BMP code unit；wide 时把 x+1 标记为 tail；
- `write_cluster`：检查 store 容量、append 完整 cluster、写 type=2 cell；
- `write_cell`：低层写，负责清理旧的 wide head/tail 邻域；
- `fill_rect`、`clear`：直接填 cell；
- `scroll_up`、`scroll_region_up`：移动行/cell，更新 damage 和 used_columns；
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
  和 `cell_info` 中 10 位 flags；
- conhost 的 per-cell `wstring`/属性通常超过 16B；
- Contour 的 SoA 多数组在小网格上可能低于 16B/cell，但字段分散，Windows API 投影需要
  额外整理。

### 7.2 热路径

- page 是连续 `std::vector<cell>`，按行/列直接索引；
- 普通 BMP 写入不分配 cluster；
- complex 写入一次 store append；
- style/hyperlink 是 O(1) ID 查表；
- scroll 只移动 cell 和 row metadata，不搬移 cluster 文本；
- wide 清理是局部 O(1)；
- damage 让渲染器只扫描脏区间；
- 24 位 offset 让 16M UTF-16 unit 不需要频繁重建；重建只在极端容量或显式调用时发生。

### 7.3 为什么不是 8B

8B cell 无法同时容纳：

- 16 位 primary；
- 24 位 offset；
- 8 位 length；
- 32 位 style；
- 32 位 hyperlink；
- 10 位 flags。

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
- primary/info/extent/style/hyperlink；cell_info mask/ops；
- 孤立 surrogate 规范为 `U+FFFD`；
- type=2 的 primary 为 `U+FFFD`；
- type alias 相等；
- grapheme store can_append/append/view/长度上限；
- style interning、默认样式；
- hyperlink interning、默认空链接；
- 屏幕写入、cluster view、rebuild_graphemes；
- wide head/tail、覆盖清理；
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
- `libscreen/grapheme_store.hpp`
- `libscreen/style.hpp`, `style.cpp`
- `libscreen/hyperlink.hpp`, `hyperlink.cpp`
- `libscreen/screen_buffer.hpp`, `screen_buffer.cpp`
- `libscreen/tests/test_libscreen.cpp`

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
