# corehost 屏幕缓冲区与 Unicode 数据模型技术分析

## 摘要

结论先说：**corehost 不应该把屏幕缓冲区的规范表示改为 UTF-16**。当前真正需要替换的，是
`screen_buffer_row` 中“整行 UTF-32 字符串 + 每列偏移表”的容器结构，而不是 Unicode 标量本身。

推荐的目标模型是：

1. 每个终端列使用一个紧凑的 **8 字节 packed cell**：
   - 21 位 Unicode 标量值（primary codepoint）；
   - 2 位列宽/扩展标志；
   - 1 位 cluster 标志；
   - 16 位 page-local style ID；
   - 其余位用于 wide/spacer、protected、semantic 等标志。
2. 多码点 grapheme cluster 只把 primary codepoint 放在 cell 内，额外码点放入行级
   cluster side pool；cell 只保存 cluster 标志。
3. 颜色、下划线、超链接等样式放入 page-local 的 style table，cell 保存 style ID。
4. 屏幕由 page/row 组成，主缓冲区、备用缓冲区和未来的 scrollback 共享同一套 cell/page
   结构；行移动是 page/row 操作，不需要搬移整行字符串。
5. UTF-16 只出现在 Windows Console API 和 DirectWrite 的**边界适配层**：
   - Console API 使用 `CHAR_INFO` 时做一次有明确规则的投影；
   - DWrite 使用 UTF-16 时按可见行/run 生成临时 UTF-16 文本和 cluster map；
   - 这两层都可以缓存，且不改变屏幕缓冲区的规范表示。
6. 无头模式仍从同一套 cell/page 输出 UTF-8/VT；窗口模式从同一套 cell/page 生成
   UTF-16 run 并交给 DWrite。两种模式共享 cell 模型、行迭代器、样式表和脏行追踪，只在
   最终编码器上分叉。

Contour 和 Ghostty 都支持这个判断：它们都使用 **Unicode 标量/codepoint 作为 cell 的
primary content**，再为多码点 cluster 建立 side pool；没有把 UTF-16 作为终端网格的规范
存储。Contour 的 DirectWrite shaper 在真正调用 DWrite 前把 `char32_t` 文本转成 UTF-16，
并把 UTF-16 的 surrogate/cluster 偏移映射回 cell cluster。

## 1. 分析范围与证据

本报告基于以下代码和文档进行功能与结构分析：

### corehost

- `libcorehost/screen_buffer_row.hpp`
  - `_text` 是整行 `std::u32string`；
  - `_columns` 是每列到 `_text` 的 16 位偏移表，最高位标记 wide glyph 的 trailing 列；
  - `_attrs` 是每列的 16 位 Win32 legacy 属性；
  - `_filled`、`_single_width_layout`、`_attrs_uniform` 是现有的行级压缩/快路径；
  - `write_glyph` 在替换 glyph 后平移同一行后续列的偏移；
  - `to_char_info` 和 `from_char_info` 在 Win32 API 边界做有损投影。
- `libcorehost/screen_buffer.hpp`
  - 行数组与 `_row_origin` 环形原点；
  - `write_text_row`、`row_to_ci`、`row_from_ci`、fill/scroll/resize 等 API 操作；
  - 当前行文本是变长存储，不是固定大小 cell 数组。
- `libcorehost/char_width.hpp`、`libcorehost/text_measurement_mode.hpp`
  - 存在 `console`、`wcswidth`、`graphemes` 三种宽度策略。
- `libcorehost/char_convert.hpp`
  - 已有 UTF-8、UTF-16、UTF-32 之间的批量转换和可复用缓冲。
- `libcorehost/api_handlers.hpp`、`libcorehost/vt_msg_dispatch.hpp`
  - Console API 输入输出与 VT 输出都写入同一份 `console_state`/`screen_buffer`。

### Contour

- `contour/src/vtbackend/grid/LineSoA.hpp`
  - `LineSoA` 把每行拆成 SoA 数组；
  - `codepoints` 使用 `char32_t`；
  - `widths` 保存 1/2 列宽；
  - `sgr` 保存每 cell 的图形属性；
  - `clusterSize`、`clusterPoolIndex`、`clusterPool` 保存多码点 grapheme；
  - `LineShape` 区分 `Uniform`、`Mixed`、`Spanning`，给批量 ASCII、批量渲染和逐 cell
    路径提供明确的快速路径；
  - `MaxGraphemeClusterSize = 16`，并注明真实的 RGI emoji 组合可达到 10 个码点。
- `contour/docs/drafts/grid-redesign.md`
  - 使用 dense/ring buffer、逻辑行、viewport、reflow 的网格设计。
- `contour/src/text_shaper/DirectWriteShaper.cpp`
  - shaper 输入仍是 `char32_t`；
  - 进入 DirectWrite 前转换为 `wchar_t`/UTF-16；
  - 对非 BMP 码点扩展成两个 UTF-16 code unit，并同步扩展 cluster 数组；
  - 用 UTF-16 cluster map 把 DWrite 的 glyph cluster 映射回 cell cluster；
  - fallback 以 cluster 为单位，不拆散 grapheme/ligature。

### Ghostty

- `ghostty/src/terminal/page.zig`
  - page 后备内存按页分配，cell 是 dense array；
  - `Cell` 是 `packed struct(u64)`，包含 21 位 codepoint、content tag、style ID、
    wide/spacer 标志、protected、hyperlink、semantic content；
  - 多码点 grapheme 通过 per-page 的 grapheme allocator 和 grapheme map 存储；
  - blank row 以 row 标记表示，避免 style/hyperlink/grapheme 的 managed memory；
  - style set 使用 page-local ID 和引用计数；
  - 有显式的 page/row dirty 标志。
- `ghostty/src/terminal/size.zig`
  - style/hyperlink/cell count 使用 16 位编号，和 page 的局部生命周期匹配。
- `ghostty/src/unicode/grapheme.zig`
  - grapheme 分割是输入模型的一部分，而不是按 UTF-16 code unit 处理。

这些实现的共同点是：**Unicode 标量/codepoint 是稳定的语义单位，UTF-16 是平台 API
的编码格式**。它们的性能并非来自把 codepoint 换成 UTF-16，而是来自固定大小 cell、side
pool、style interning、行级 fast path 和 page 管理。

## 2. Windows 约束

### 2.1 Console API

Win32 Console API 的 `CHAR_INFO` 是：

- 一个 `WCHAR` 字符；
- 一个 16 位 legacy 属性；
- 一个 cell 对应一个控制台列。

这意味着：

- BMP 字符可以一一对应；
- 非 BMP 码点需要代理对，但 `CHAR_INFO` 一个 cell 只有一个 `WCHAR`，因此 API 边界
  天然无法无损表示完整非 BMP 字符；
- 多码点 grapheme cluster 也无法由单个 `CHAR_INFO` 表示；
- `ReadConsoleOutput`/`WriteConsoleOutput` 的语义是列矩形，不是 Unicode scalar 序列。

因此，**Console API 的 UTF-16 性不等于屏幕缓冲区应该使用 UTF-16**。即使规范存储是
UTF-16，做 `CHAR_INFO` 投影时仍然要处理：

- 代理对跨越两个 code unit；
- cluster 跨越多个 code unit 和多个 cell；
- legacy 属性到样式的映射；
- 非 BMP 的替代字符策略。

### 2.2 DirectWrite

DWrite 的文本输入、文本范围和 cluster 偏移都是 UTF-16 code unit 语义：

- `DWRITE_TEXT_RANGE` 使用 UTF-16 偏移；
- `DWRITE_GLYPH_RUN` 的 cluster 值也是 UTF-16 source index；
- fallback、复杂 script、组合标记、emoji 都通过 shaper 的 cluster 关系处理。

如果规范存储是 codepoint，必须在 DWrite 边界做：

1. codepoint 序列 → UTF-16 序列；
2. codepoint index → UTF-16 offset 映射；
3. DWrite glyph cluster → cell cluster 映射。

如果规范存储是 UTF-16，第 1 步消失，但第 2、3 步仍然存在，因为：

- 一个 grapheme 可能是一个 cell；
- 一个非 BMP 码点在 UTF-16 中占两个 unit；
- ligature 可能把多个 cell 的文本合并成一个 glyph；
- fallback 必须以完整 cluster 为单位，不能拆开代理对或组合序列。

所以 UTF-16 canonical 并不能消除 DWrite 的 cluster 映射工作，只是把部分转码工作前移。

## 3. 当前 corehost 的实际结构

### 3.1 行结构

当前 `screen_buffer_row` 的核心是：

```text
_text      : std::u32string        // 行内所有 glyph 的 codepoint
_columns   : uint16_t[width+1]    // 每列到 _text 的偏移，最高位表示 trailing
_attrs     : uint16_t[width]      // 每列 legacy Win32 属性
```

对 ASCII 单宽行，快路径可以做到：

- `_single_width_layout = true`；
- 每个可见列直接等于 `_text` 的同下标 codepoint；
- 不需要读取 `_columns` 的完整映射。

一旦写入双宽字符或多码点 grapheme：

- `_single_width_layout` 被关闭；
- `_columns` 被物化；
- `write_glyph` 需要替换 `_text` 的一段，并平移后续非 trailing 列的偏移；
- `_columns` 使用 15 位 offset mask，单行 `_text` 长度有上限；
- 行文本长度变化会带来 O(行宽) 的 offset 维护成本。

### 3.2 内存和访问形态

简单单宽行大致为：

```text
4 bytes/cell  (u32 codepoint)
2 bytes/cell  (_columns)
2 bytes/cell  (_attrs)
= 8 bytes/cell
```

此外还有每行 `std::u32string` 和 `std::vector` 的容量、分配头和过度分配。多码点 cluster
的额外 codepoint 存在 `_text` 中，不另设 pool。

这个结构的问题不是“每个 codepoint 占 4 字节”本身，而是：

- 行文本是变长的，不能随机定位一个 cell；
- 列偏移需要额外表和逐行维护；
- 多码点 cluster 写入会移动同一行后续列的偏移；
- 行复制/滚动搬运的是多个变长容器；
- 行级 fast path 只有“全部单宽”与“非单宽”两类，缺少更细的 blank/uniform/spanning 状态；
- 每个 cell 只有 legacy `WORD` 属性，未来窗口模式的真彩色、下划线颜色、超链接等没有承载位置。

### 3.3 Console API 投影

当前 `to_char_info` 的规则是：

- glyph 的首个 codepoint 如果是 BMP，写入该 `WCHAR`；
- 非 BMP 写 `U+FFFD`；
- 多码点 cluster 只取首码点；
- trailing 列使用空字符/属性；
- `from_char_info` 对孤立 high surrogate 写 `U+FFFD`，但没有完整处理 surrogate pair，
  也没有对 low surrogate 做同样的规范化。

这些是有损的，但其中一部分是 Win32 `CHAR_INFO` 本身的限制。真正需要改进的是：

- 输入方向尽量合并合法 surrogate pair，而不是把每个 code unit 当成一个字符；
- 明确记录“内部 cluster → API CHAR_INFO”的有损规则；
- 让窗口渲染与 VT 输出使用内部完整 cluster，而不是被 API 投影限制。

### 3.4 宽度策略

corehost 已有三种宽度模式：

- `console`：传统 Console 宽度，适合兼容 conhost/CHAR_INFO 的列语义；
- `wcswidth`：POSIX/wcwidth 风格；
- `graphemes`：按 grapheme cluster 测量，适合现代终端 UI 和 emoji。

当前 `console_state.text_measurement` 是会话级设置，Console API 写入、VT 输出和窗口
渲染共享一个测量模式。这个设计需要在目标模型中明确：

- **Console API 写入**继续使用 legacy/console 列规则，保证 `WriteConsoleOutput` 等 API
  的列数和 conhost 一致；
- **VT/窗口输出**使用终端 grapheme 规则，保证 emoji/组合字符的显示质量；
- 两种写入最终都落到同一个 cell 网格，但**宽度在写入时解析并保存到 cell**，不再在渲染时
  重新猜测；
- 同一码点在不同 source policy 下可能有不同列宽，这是 Windows Console 兼容性与终端
  Unicode 质量之间的真实差异，应在 cell 的宽度标志/写入策略中显式体现。

## 4. 可选表示方案比较

### 4.1 UTF-16 canonical cell

设想：每个 cell 保存一个或两个 UTF-16 code unit，style ID 另存。

优点：

- DWrite 的 UTF-16 输入几乎零转码；
- BMP `CHAR_INFO` 映射直接；
- 理论上 ASCII 更紧凑。

缺点：

- 非 BMP 码点需要两个 code unit，cell 要么扩展为 `wchar_t[2]`，要么把 surrogate 放在
  side table；
- 一个终端 cell 对应一个列，而 UTF-16 surrogate pair 对应两个 code unit，破坏了
  “一个 cell 一个字符”的直觉；
- 代理对、组合标记、ZWJ 序列、selection、copy、search、宽度计算都必须做 surrogate-aware
  处理；
- Console API 投影仍然是有损的，因为 `CHAR_INFO` 一个 cell 只有一个 `WCHAR`；
- 无头模式反而需要 UTF-16 → UTF-8 的额外转换；
- 不能消除 DWrite 的 cluster mapping，只是改变映射起点。

结论：UTF-16 canonical 主要优化了 DWrite 的一个编码步骤，却把 Unicode 语义复杂度放到
cell 模型里。对一个需要支持 Console API、VT、窗口模式和未来 scrollback 的项目，不划算。

### 4.2 当前 UTF-32 行字符串 + 列偏移

优点：

- Unicode scalar 语义清晰；
- 多码点 cluster 可以塞进 `u32string`；
- 现有 API/VT 路径已经按 codepoint 工作。

缺点：

- 变长行文本，随机访问依赖 offset 表；
- 复杂 glyph 写入需要平移后续 offset；
- 行内 cluster 和文本没有独立的 side storage；
- 每行多个容器，滚动/复制/重排成本较高；
- 16 位 offset 存在上限；
- legacy 属性只有 16 位，未来窗口需要更丰富的 style。

结论：codepoint 方向正确，容器结构需要替换。

### 4.3 UTF-8 side pool

设想：cell 只保存 UTF-8 字节引用，完整文本放行级 arena。

优点：

- ASCII 内存最省；
- 无头 VT 输出几乎零转码；
- 适合把文本当字节流的场景。

缺点：

- 随机访问一个 cell 的 codepoint/grapheme 需要解码；
- Unicode scalar/宽度/selection 需要频繁解码；
- DWrite 仍要 UTF-16 转码；
- 多码点 cluster 与字节边界关系复杂；
- 不利于按列矩形读写 Console API。

结论：可以作为文本池压缩的补充，不适合作为 primary cell 表示。

### 4.4 紧凑 packed codepoint cell + side pool

这是推荐方案。核心思想：

- cell 固定 8 字节，primary content 存 21 位标量；
- 多码点 cluster 的额外内容放行级 side pool；
- style 放 page-local style table；
- 所有平台编码转换延后到边界；
- 行/page 级 fast path 处理 blank、ASCII、uniform、spanning。

它在 Unicode 语义、Windows API 映射、DWrite 适配和内存占用之间取得最佳折中，也最接近
Ghostty/Contour 已被验证的结构。

## 5. 推荐结构

### 5.1 Cell

概念结构：

```text
Cell (8 bytes, packed):
  codepoint     : 21 bits   // Unicode scalar, 0 = blank
  style_id      : 16 bits   // page-local style table index, 0 = default
  wide          : 2 bits    // narrow / wide / spacer tail / spacer head
  cluster       : 1 bit     // extra codepoints exist in row cluster pool
  content_tag   : 2 bits    // codepoint / grapheme / background-only
  protected     : 1 bit
  hyperlink     : 1 bit
  semantic      : 2 bits    // output/input/prompt, optional
  reserved      : remaining
```

设计要求：

- 零值必须表示一个合法的空白 cell，便于 page 初始化和 `memset`；
- `codepoint` 是 21 位 Unicode scalar，不存 UTF-16 code unit；
- 非 BMP 字符仍然只占一个 primary codepoint；其显示宽度由 `wide` 和宽度策略决定；
- 多码点 cluster 的额外 codepoint 不放 cell 内，避免 cell 膨胀；
- style ID 是 page-local 的，默认 0 不查表；
- `content_tag` 允许“只有背景色、没有文字”的 cell 不占 style table，类似 Ghostty 的
  background-only 优化；
- 8 字节对齐使 page 的 cell 数组可以直接按 `uint64` 清零/复制。

### 5.2 Style table

当前 `text_attribute` 只有 legacy `WORD`。未来窗口模式至少需要：

- 前景色、背景色；
- 256 色/RGB；
- bold/faint/italic/underline/blink/reverse/conceal/strikethrough；
- underline color；
- hyperlink；
- semantic/prompt 信息。

建议：

```text
Style:
  foreground color
  background color
  underline color
  attribute flags
  legacy WORD projection
```

- style 按 page 建立 `StyleSet`，cell 保存 16 位 `style_id`；
- 默认样式 ID 为 0，不需要查表；
- `legacy WORD` 由 style 投影得到，Console API 不需要理解 RGB/超链接；
- 跨 page 复制 cell 时重新映射 style ID；
- style ID 溢出时提升 page 的 ID 宽度或触发 page 拆分，而不是截断样式。

Contour 使用每 cell 的 `GraphicsAttributes`，Ghostty 使用 page-local `style_id`。corehost
同时面对 Windows legacy API 和未来 DWrite，采用 style table 更合适：它既减少 cell 体积，
也让 Console API 与窗口渲染共享同一份样式来源。

### 5.3 Row 和 Page

建议把 `screen_buffer` 的“行数组 + 变长行文本”改为“page + row + dense cell”：

```text
Page:
  Cell[rows * cols]     // dense, page-aligned
  Row[rows]             // cell_start, used_cols, flags, dirty range
  StyleSet              // page-local
  GraphemePool          // 仅多码点 cluster 使用
  GraphemeMap           // cell offset → pool slice

Row:
  cell_start
  used_columns
  flags                 // wrapped, dirty, grapheme, uniform...
  dirty_begin/dirty_end
  shape                 // blank / uniform / mixed / spanning
```

关键点：

- blank row 不分配 cell，只记录 fill style/char；
- `Uniform` 行允许整行相同 style 的批量渲染/批量 ASCII 写入；
- `Spanning` 行表示存在 wide continuation、多 cell glyph、图片等跨越 cell 的情况；
- `GraphemePool` 是 page 级 arena，多码点 cluster 使用 offset+length；
- cell 的 cluster 标志只表示“去 map 查询”，不把 pointer 放进每个 cell；
- row 移动/page 滚动不搬移 cluster 文本；cluster 数据跟随 page 生命周期；
- page 可以固定大小，main/alt/scrollback 使用 page list 或 ring；
- 当前 `screen_buffer` 的 `_row_origin` 环可以保留为 page/row 层优化，但不再是唯一滚动机制。

这里不直接采用“每行一个 `std::vector` + 一个 `u32string`”的形式，因为那正是当前结构的
主要成本来源。Dense page 更利于：

- Console API 的矩形读写；
- DWrite 的可见行遍历；
- 无头 VT 的行序列化；
- scrollback 的 page 复用；
- 批量 ASCII 和 blank 行优化。

### 5.4 Grapheme cluster

推荐规则：

- 每个 cell 最多表示一个 grapheme cluster；
- primary codepoint 在 cell 内；
- 额外 codepoint 在 page/row 的 `GraphemePool`；
- cluster 长度上限使用经 RGI 测试的值，建议至少 16，硬上限 64；
- 超过上限的输入要明确策略：
  - 截断并保留首个合法显示单元；
  - 或替换为 `U+FFFD`；
  - 不能把半个 cluster 写入多个 cell 导致列宽漂移；
- 组合标记、variation selector、ZWJ、regional indicator、tag sequence 都要在写入 cell
  之前完成 grapheme 分割；
- selection/copy/search 以 cell/cluster 为单位，不从 UTF-16 code unit 反推；
- DWrite fallback 以完整 cluster 为单位，不拆散 ligature、组合标记或代理对。

Contour 的 `MaxGraphemeClusterSize` 注释给出了一个重要的工程事实：真实 RGI emoji 已经
能到 10 个 codepoint，7 的上限会导致宽度和 cursor 位置错误。corehost 如果设置 cluster
上限，必须用完整的 Unicode grapheme 测试集验证，而不是只测一个 emoji。

### 5.5 Width、wide 和 spacer

- 每个 cell 保存已解析的 `wide` 状态；
- 宽字符 head 占 2 列；
- 第二列是 spacer tail，不包含独立文本；
- 行尾放不下宽字符时，按终端规则换行或放置 spacer head；
- `console` 宽度策略用于 Console API 的列语义；
- `graphemes`/`wcswidth` 用于 VT 和窗口的显示语义；
- 写入时决定宽度并保存到 cell，渲染时不再用 DWrite advance 反推列宽；
- DWrite 的 glyph advance 用于簇内定位和最终绘制，不改变终端列坐标。

## 6. 边界编码设计

### 6.1 Console API：UTF-16 / CHAR_INFO

**读取路径：**

```text
Cell/Page
  → cell_to_char_info projection
  → CHAR_INFO row/rectangle
  → console driver
  → application
```

规则建议：

- BMP 标量：写入对应 `WCHAR`；
- 非 BMP：写 `U+FFFD`（或按项目定义的兼容替代），不能写入半个代理对；
- 多码点 cluster：只投影首个可见标量，不能让 cluster 污染相邻列；
- trailing/spacer：写空字符并保留属性；
- style：投影为 legacy `WORD`；
- 读取矩形时按 cell 直接扫描，不重新做 Unicode 宽度计算。

**写入路径：**

```text
application
  → CHAR_INFO / UTF-16
  → code-unit decoder (pair surrogate)
  → codepoint + grapheme segmentation
  → width policy
  → cell/page write
```

必须明确处理：

- 合法 high surrogate + low surrogate 合并为一个标量；
- 孤立 surrogate → `U+FFFD`；
- `CHAR_INFO` 的 0 字符是 trailing 标记还是真正的 NUL，需要和 conhost 行为一致；
- `WriteConsoleW` 的字符串不是 `CHAR_INFO` 矩形，应该先转换成 codepoint 流，再按完整
  grapheme 写入，而不是每个 code unit 独立写 cell。

当前 `from_char_info` 只规范化 high surrogate，没有完整合并 surrogate pair，也没有对
low surrogate 做同样处理；新模型应把这段逻辑集中到统一的 UTF-16 解码器，避免两条 API
路径行为不一致。

### 6.2 DirectWrite：UTF-16 run + cluster map

窗口渲染不应把每个 cell 单独交给 DWrite。推荐按可见行的 run 做：

```text
Cell row
  → group by style/font/width policy
  → append codepoints to UTF-16 scratch
  → build cell_index → UTF-16 offset map
  → DWrite analyzer/shaping
  → map glyph clusters back to cell clusters
  → cache shaped runs
  → draw glyphs at cell positions
```

细节：

- 一个非 BMP codepoint 转成两个 UTF-16 code unit，两个 unit 都映射到同一个 cell；
- 一个多码点 cluster 的所有 codepoint 都映射到同一个 cell cluster；
- DWrite 的 glyph cluster 是 UTF-16 source offset，不是 cell index，必须通过 map 转换；
- ligature 可能让多个 cell 对应一个 glyph，fallback 必须以整个 cluster group 为单位；
- 字体 fallback 只对缺失 glyph 的完整 cluster 进行，不拆代理对、组合标记或 ZWJ；
- 颜色 emoji/COLR/CBDT 走 DWrite 的 color glyph 路径；
- 只为可见、脏的 run 做转换和 shaping；
- 缓存键至少包含 style、font、font size、DPI、文本 cluster 指纹。

Contour 的 DirectWrite shaper 已经示范了这个模式：输入 `std::u32string_view`，在 shaper
边界转成 `wchar_t`，把非 BMP 的两个 UTF-16 unit 都映射到同一个 cell cluster，然后用
UTF-16 glyph cluster 反推 cell cluster。这说明 codepoint canonical 与 DWrite 并不冲突。

### 6.3 无头模式：UTF-8 / VT

无头模式直接从 cell/page 输出：

```text
Cell row
  → cluster to UTF-8
  → style table to SGR
  → VT output buffer
  → vt_out
```

特点：

- 不需要 UTF-16 中转；
- ASCII/单宽行可以直接批量输出；
- style ID 转 SGR 时只查 style table；
- 脏行/脏范围可以决定增量输出；
- 与窗口模式共享 cell、row、style 和 dirty 信息，只替换最后编码器。

这正好回应了共享代码需求：**共享的是屏幕模型和行迭代，不是最终编码格式**。如果为了窗口
模式把规范存储改成 UTF-16，无头模式反而要为每一次 VT 输出做 UTF-16→UTF-8 转换，增加了
当前主路径的成本。

## 7. 性能分析

### 7.1 当前结构的成本

当前每次复杂 glyph 写入：

1. 可能物化 `_columns`；
2. 替换 `_text` 中的一段；
3. 计算长度差；
4. 平移后续所有非 trailing 列的 offset；
5. 更新 trailing 标记和属性。

这使中间插入/删除一个多码点 glyph 成为 O(width) 操作。对于 ASCII 快路径，很多写操作
不会触发；但一旦窗口程序输出 emoji/组合字符，复杂行会退化。Scroll/resize 还会复制多个
变长容器。

### 7.2 新结构的成本

新结构：

- 写入一个 cell 是 O(1)；
- 覆盖一个 cluster 先清理旧的 side pool entry，再写入新 entry；
- 不移动其他 cell 的 offset；
- 行内 ASCII 批量写入直接写 cell 数组；
- blank/uniform 行不分配 cell；
- page 滚动是 row/page 索引操作，不复制整行字符串；
- 只有多码点 cluster 才访问 side table，慢路径局限在真正复杂的 cell。

### 7.3 内存估算

8 字节 cell 的粗略估算：

| 网格 | cell 内存 | 说明 |
| --- | ---: | --- |
| 120 × 30 | 28.8 KB | 单屏 |
| 300 × 100 | 240 KB | 大窗口 |
| 1000 × 200 | 1.6 MB | 大 scrollback 区块 |

此外：

- blank row 不分配；
- style table 只保存实际使用的样式；
- grapheme pool 只保存多码点 cluster；
- 可见行 UTF-16 scratch 约为 2 bytes × UTF-16 units；120×30 满屏最多约 7.2 KB；
- Console API 的 `CHAR_INFO` 矩形是 4 bytes/cell，按调用临时分配/复用。

对比 UTF-16 canonical，8 字节 cell 的 ASCII 内存更高；但它避免了代理对 cell、cluster
side table 和 selection/copy/search 的额外复杂度，并且与 Ghostty 的 8 字节 cell 方案一致。
如果未来证明 ASCII 内存是瓶颈，可以增加 row-level ASCII compression，而不是把整个模型
改成 UTF-16。

### 7.4 转换成本

UTF-32 codepoint → UTF-16 的转换：

- BMP 一个分支；
- 非 BMP 输出两个 unit；
- 对 120×30 的可见屏幕，最多几千个 cell；
- DWrite 的 shaping、fallback、glyph rasterization 成本通常远高于编码转换；
- 只需要转换可见/脏行，并可缓存 UTF-16 run；
- Console API 的 `CHAR_INFO` 转换本来就必须做属性/列投影，无法通过 UTF-16 canonical
  消除。

因此，**UTF-16 canonical 的收益主要是减少一次编码循环，而不是减少整个渲染/API 路径的
主要工作量**。这个收益不足以抵消 Unicode 语义复杂度和无头路径的额外转换。

## 8. 正确性和边界条件

### 8.1 非 BMP

- 内部 cell 存 21 位标量，保留完整码点；
- UTF-16 边界输出一对 surrogate；
- `CHAR_INFO` 读取只能写替代字符，这是 API 限制，必须在文档/测试中固定；
- `CHAR_INFO` 写入合并合法 surrogate pair；
- 孤立 surrogate 统一替换为 `U+FFFD`。

### 8.2 组合字符和 emoji

必须测试：

- combining diacritics（base + 多个 mark）；
- variation selector 15/16；
- ZWJ 家庭/职业/肤色 emoji；
- regional indicator 国旗；
- tag sequence 旗帜；
- 多码点 cluster 的宽度和 cursor 推进；
- cluster 上限边界；
- 宽字符落在最后一列、跨行、spacer head/tail；
- ambiguous width 在 console/wcswidth/graphemes 三种策略下的差异。

### 8.3 Console API 往返

- `ReadConsoleOutput` 后 `WriteConsoleOutput` 的 BMP 文本/属性应保持一致；
- 非 BMP/多码点 cluster 往返是有损的，需定义预期；
- `WriteConsoleW` 的 surrogate pair 应尽量合并，而不是写两个 replacement；
- `CHAR_INFO` 的 0 字符/trailing 语义必须与 conhost 一致；
- `GetConsoleScreenBufferInfo`/属性查询使用 legacy 投影，不应改变内部 style。

### 8.4 窗口和无头共享

- VT 输出和 DWrite 渲染必须读取同一 cell/page，不允许两套文本存储；
- 无头输出 UTF-8 不能经过 UTF-16 中间层；
- 窗口输出 UTF-16 必须带 cell→UTF-16 cluster map；
- 两边都只读取/清理 dirty 范围；
- style table 更新要同时使 VT SGR 缓存和 DWrite shaping cache 失效。

## 9. 迁移方案

### 阶段 0：基线与测试

- 增加 Unicode/Console API/DWrite 基准；
- 记录当前 120×30、300×100 的 ASCII/CJK/emoji 写入、滚动、VT 输出和
  `CHAR_INFO` 转换数据；
- 固定 conhost 行为对照，尤其是非 BMP 和 surrogate 处理。

### 阶段 1：引入 Cell/Row/Page

- 实现 packed 8-byte `Cell`、`Row`、`Page`；
- 先让 `screen_buffer` 的 public 操作委托到新结构；
- 保留现有 `screen_buffer_row` 适配层，逐步迁移 fill/write/scroll/resize；
- 先把 ASCII/单宽快路径跑通，再迁移 cluster/wide/attribute。

### 阶段 2：Style table 和 cluster side pool

- 引入 page-local style table；
- 旧 `WORD` 属性通过 style 投影保持 Console API 兼容；
- 引入 grapheme pool/map，迁移多码点写入和复制/移动/滚动；
- 统一 grapheme 分割和宽度解析；
- 删除 `_columns` offset 机制。

### 阶段 3：共享渲染模型

- 定义只读 `RowView`/`CellRun` 和 style 访问接口；
- 无头 VT serializer 使用该接口输出 UTF-8/SGR；
- 窗口 DWrite renderer 使用该接口生成 UTF-16 run + cluster map；
- 加 run 缓存和 dirty 行增量渲染；
- 保证 headless 和 window 只在编码器/绘制器分叉。

### 阶段 4：Scrollback/Reflow 和优化

- 将 row/page 扩展为 main/alt/scrollback page list；
- 支持 reflow、wrapped line、page 回收；
- 评估 ASCII row compression、cell 压缩和 style ID 溢出策略；
- 删除旧 `u32string` 行存储和 `_row_origin` 兼容代码。

### 测试矩阵

- 单元：cell 编解码、cluster 池、style 表、宽度策略、surrogate 合并；
- Console API：矩形读写、属性、非 BMP、cluster 往返有损规则；
- VT：headless 输出快照、SGR、宽字符、emoji；
- DWrite：UTF-16 偏移映射、surrogate、组合标记、ZWJ、fallback、颜色 emoji；
- 回归：resize、scroll、alt buffer、selection/copy/search、并发/脏行；
- 性能：ASCII 批量、CJK、emoji、scrollback、VT 输出、DWrite shaping cache。

## 10. 风险与取舍

1. **内存**：8 字节 cell 比 UTF-16 code unit 更占内存。需要 page/blank 压缩和
   scrollback 容量控制；如果 ASCII 内存成为瓶颈，增加 row-level ASCII fast path，
   而不是改成 UTF-16 canonical。
2. **Style ID 生命周期**：page-local style ID 在跨 page 复制/滚动时要重新映射；16 位
   ID 溢出要可检测。
3. **Cluster 池所有权**：cluster 数据跟随 page/row；移动 cell 时必须移动/复制 cluster
   entry，并更新 map；需要严格的完整性检查。
4. **DWrite cluster map**：UTF-16 offset、surrogate pair、ligature、fallback 都不能拆
   cluster；这是实现复杂度的主要来源，但任何 canonical 表示都无法完全避免。
5. **Console API 有损性**：`CHAR_INFO` 的非 BMP/cluster 投影无法无损；必须明确规则并
   固化测试，不能为了 API 兼容牺牲内部窗口/VT 的 Unicode 质量。
6. **宽度策略**：Console legacy 宽度与终端 grapheme 宽度不同；必须按写入来源选择策略，
   不能在渲染时重新计算，否则同一 cell 的列宽会漂移。
7. **输入边界**：UTF-16 输入中的 surrogate pair、孤立 surrogate、NUL 和 trailing cell
   语义需要统一处理，避免不同 API 路径产生不同结果。

## 11. 结论

corehost 当前的问题可以概括为：

> 使用 Unicode scalar 作为语义单位是正确的；使用**整行 UTF-32 字符串 + 列偏移表**作为
> 容器是低效的；把规范表示改成 UTF-16 则会把 Windows API 的编码限制带入内部模型。

推荐的设计是：

- **Cell 固定 8 字节**：21 位 primary codepoint、16 位 page-local style ID、wide/spacer
  和 cluster 标志；
- **行级/page 级 grapheme side pool**：只保存多码点 cluster 的额外内容；
- **page + row + dense cell**：支持主/备用缓冲区和未来 scrollback；
- **style table**：内部保存完整的颜色/属性/超链接，legacy `WORD` 只在 Console API 投影；
- **UTF-16 只做边界编码**：`CHAR_INFO` 和 DWrite 各自使用明确的转换/映射层；
- **无头 UTF-8/VT 与窗口 DWrite 共享 cell/row/style**，只在最终编码器分叉。

Contour 使用 `char32_t` primary + cluster pool + `LineShape`，Ghostty 使用 8 字节 packed
cell + style ID + grapheme map；两者都把 UTF-16 留给平台 shaper，而不是作为终端网格的
规范存储。这与本报告的推荐一致，也说明“为了 Console API/DWrite 的 UTF-16 而改成
UTF-16 屏幕缓冲区”并不是高质量终端的必要选择。

## 参考代码位置

### corehost

- `libcorehost/screen_buffer_row.hpp`
- `libcorehost/screen_buffer.hpp`
- `libcorehost/char_width.hpp`
- `libcorehost/char_convert.hpp`
- `libcorehost/text_measurement_mode.hpp`
- `libcorehost/api_handlers.hpp`
- `libcorehost/vt_msg_dispatch.hpp`

### Contour

- `contour/src/vtbackend/grid/LineSoA.hpp`
- `contour/docs/drafts/grid-redesign.md`
- `contour/src/text_shaper/DirectWriteShaper.cpp`
- `contour/src/text_shaper/ClusterSpans.hpp`

### Ghostty

- `ghostty/src/terminal/page.zig`
- `ghostty/src/terminal/size.zig`
- `ghostty/src/terminal/Parser.zig`
- `ghostty/src/unicode/grapheme.zig`
