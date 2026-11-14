# corehost 屏幕缓冲区与 Unicode 数据模型技术分析

## 摘要

结论先说：corehost **不应采用“每个 cell 独立 UTF-16 code unit、非 BMP 拆成两个 surrogate
cell”的 UTF-16 canonical**；但可以采用 **16 位 UTF-16 primary + row-local 完整 cluster
side store** 的混合模型。这个混合模型直接对齐 Windows API/DWrite 的 UTF-16 单位，同时
不把 surrogate pair 拆散到多个 cell。

推荐的目标模型是：

1. 每个终端列使用一个紧凑的 **8 字节 bitfield cell**：
   - 16 位 UTF-16 primary（BMP 文本的实际 code unit；多 unit 时为 `U+FFFD`）；
   - 16 位 row-local cluster store offset；
   - 16 位 page-local style ID（在 16 位边界对齐）；
   - 6 位 `offset2`（实际保存 cluster length - 1，覆盖 1..64 个 UTF-16 unit）；
   - 2 位 content 类型；
   - 2 位列宽/continuation；
   - 5 位 flags；
   - 1 位保留位。
2. 非 BMP、多码点 grapheme 或其他复杂 cluster 的**完整文本（含真实 primary）**放入
   row-local 的 `std::vector<char16_t>`；cell 的 `primary` 为 `U+FFFD`，并保存
   offset/offset2；cell 不保存 pointer。
3. 颜色、下划线、超链接等样式放入 page-local 的 style table，cell 保存 style ID。
4. 屏幕由 page/row 组成，主缓冲区、备用缓冲区和未来的 scrollback 共享同一套 cell/page
   结构；行移动是 page/row 操作，不需要搬移整行字符串。
5. UTF-16 primary 直接用于 Windows Console API 和 DirectWrite 的 BMP 路径；复杂 cluster
   的 primary 固定为 `U+FFFD`，完整文本从 side store 读取，不经过 surrogate-split cell：
   - Console API 使用 `CHAR_INFO` 时做一次有明确规则的投影；
   - DWrite 使用完整 cluster 生成 UTF-16 run 和 cluster map；
   - 这两层都可以缓存，且不改变屏幕缓冲区的规范表示。
6. 无头模式仍从同一套 cell/page 输出 UTF-8/VT；窗口模式从同一套 cell/page 生成
   UTF-16 run 并交给 DWrite。两种模式共享 cell 模型、行迭代器、样式表和脏行追踪，只在
   最终编码器上分叉。

Contour 和 Ghostty 都支持这个判断：它们都使用 **Unicode 标量/codepoint 作为 cell 的
primary content**，再为多码点 cluster 建立 side pool；没有把 UTF-16 作为终端网格的规范
存储。Contour 的 DirectWrite shaper 在真正调用 DWrite 前把 `char32_t` 文本转成 UTF-16，
并把 UTF-16 的 surrogate/cluster 偏移映射回 cell cluster。

原版 conhost 和 Windows Terminal 也验证了另一个关键点：conhost 内部 `OutputCell` 保存
完整的 UTF-16 文本，但 `ReadConsoleOutput`/`ReadConsoleOutputCharacter` 会把多 unit cell
主动投影为 `U+FFFD`，`WriteConsoleOutput` 则把每个 `CHAR_INFO` 当作一个 `WCHAR` cell；
Windows Terminal/DWrite 使用的是内部丰富文本，而不是 `CHAR_INFO`。因此，**Console API
有损并不要求屏幕缓冲区也有损；相反，内部模型应保留完整文本，并在 API 边界集中处理投影。**
这也是本报告对 corehost 的核心建议。

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

### terminal / conhost

- `terminal/src/buffer/out/OutputCell.hpp`
  - 内部 cell 使用 `std::wstring _text`，可以保存多个 UTF-16 code unit。
- `terminal/src/host/consoleInformation.cpp`
  - `CONSOLE_INFORMATION::AsCharInfo` 用 `Utf16ToUcs2` 生成 `CHAR_INFO`。
- `terminal/src/host/directio.cpp`
  - `ReadConsoleOutput`/`WriteConsoleOutput` 矩形路径。
- `terminal/src/host/output.cpp`
  - `ReadConsoleOutputCharacter` 的 `ReadOutputStringW` 路径。
- `terminal/src/types/convert.cpp`
  - `Utf16ToUcs2` 对多 unit cell 返回 `U+FFFD`。
- `terminal/src/host/VtIo.cpp`
  - 旧 `CHAR_INFO` 路径的 VT writer 与 surrogate 清理。
- `terminal/src/renderer/atlas/AtlasEngine.api.cpp`
  - Windows Terminal 通过 TextBuffer/DWrite 渲染文本。

Contour 和 Ghostty 的共同点是：**Unicode 标量/codepoint 是稳定的语义单位，UTF-16 是
平台 API 的编码格式**。它们的性能并非来自把 codepoint 换成 UTF-16，而是来自固定大小
cell、side pool、style interning、行级 fast path 和 page 管理。conhost 的内部
`OutputCell` 虽然保存 UTF-16 文本，但同样把内部丰富文本与有损的 `CHAR_INFO` 投影分开；
它证明的是**分层原则**，而不是必须把 cell 存成 UTF-16。

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
但要注意：**4.4 推荐的是“16 位 UTF-16 primary + 完整 cluster side store”的混合模型**，
不是 4.1 描述的 canonical model；非 BMP/grapheme 仍以一个 cell 的完整 cluster 表示，不会
拆成两个独立 surrogate cell。

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

### 4.4 16-bit UTF-16 primary cell + full-cluster side store

这是推荐的 Windows 专用混合方案。核心思想：

- cell 固定 8 字节，primary 是 16 位 UTF-16 code unit；
- 非 BMP、多码点 grapheme、其他复杂 cluster 的**完整文本（含真实 primary）**放 row-local
  `std::vector<char16_t>`；这些 cell 的 primary 固定为 `U+FFFD`；
- cell 保存 16 位 offset、6 位 `offset2`（实际存 length-1，覆盖 1..64 unit）、16 位
  style ID；
- content 使用 2 位；`complex`/`surrogate`/`grapheme` 都是值 2 的别名，统一表示
  “需要 side store、primary 为 U+FFFD”；
- 所有平台编码转换延后到边界；
- 行/page 级 fast path 处理 blank、ASCII、uniform、spanning。

对 BMP 文本，primary 与 Console API/DWrite 的 UTF-16 unit 同构；对非 BMP 或复杂 cluster，
cell 的 primary 是 replacement，完整文本在 side store，DWrite 和窗口渲染不会拆散
surrogate/ZWJ。它比 4.1 的 UTF-16 canonical 更安全，也避免当前 `u32string + offset` 的
逐列偏移维护。

这个方案的内存/8 字节约束在 13.1 中详细计算：以 16 位 style、16 位 offset、16 位
primary、2 位 content/width 和 5 位 flags 为前提时，cluster length 只能使用 6 位
`offset2` 保存 length-1；16 位 length 与 style/flags 不能同时塞进 8 字节。

## 5. 推荐结构

### 5.1 Cell

概念结构：

```text
Cell (8 bytes, bitfields):
  primary     : 16 bits  // BMP code unit, or U+FFFD for multi-unit content
  offset      : 16 bits  // row-local grapheme store offset
  style_id    : 16 bits  // page-local style table index, 0 = default, 16-bit aligned
  offset2     : 6 bits   // actually length-1 (1..64 UTF-16 units); historical name
  content     : 2 bits   // blank/scalar/complex(alias surrogate/grapheme)/background
  width       : 2 bits   // narrow / wide / tail / spacer head
  flags       : 5 bits   // protected/hyperlink/semantic/reserved
  reserved    : 1 bit    // keeps cell at 8 bytes
```

设计要求：

- 零值必须表示一个合法的空白 cell，便于 page 初始化和 `memset`；
- `primary` 在 `content=scalar` 时是实际 BMP code unit；在 `content` 为 2 时固定为
  `U+FFFD`，真实 primary 和完整 cluster 都在 side store；
- `complex`、`surrogate`、`grapheme` 都是值 2 的别名，统一表示“需要多个 UTF-16
  code unit”；它们不再区分类型；
- 非 BMP 的完整代理对不能被拆成两个独立 cell；content=2 的 offset/offset2 指向
  row-local store 中的完整 pair/cluster；
- `offset2` 的**实际语义是 `length - 1`**，不是第二个存储偏移；它保存 1..64 个
  UTF-16 unit 的 length-1，因此名字具有历史误导性，读取时必须通过 `cluster_length()`；
- style ID 是 page-local 的，默认 0 不查表；它排在 offset 后，保证 16 位对齐；
- `content=background` 允许“只有背景色、没有文字”的 cell；背景色仍由 style ID 提供；
- 8 字节对齐使 page 的 cell 数组可以直接按 `uint64` 清零/复制；bitfield 布局必须用
  `static_assert(sizeof(cell) == 8)` 锁住。

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
  Row[rows]             // first_cell, used_cols, flags, side store
  StyleSet              // page-local

Row:
  first_cell
  used_columns
  flags                 // wrapped, dirty, grapheme, uniform...
  dirty_begin/dirty_end
  optional<GraphemeStore>  // only complex rows own extras
```

关键点：

- blank row 不分配独立的 cell 数组或 side store，只设置 row flags/fill style；page 的 dense
  cell 数组仍可统一清零；
- `Uniform` 行允许整行相同 style 的批量渲染/批量 ASCII 写入；
- `Spanning` 行表示存在 wide continuation、多 cell glyph、图片等跨越 cell 的情况；
- `GraphemeStore` 是 row-local `std::vector<char16_t>`，保存完整 cluster（含真实 primary）；
  cell 的 primary 固定为 `U+FFFD`，并使用 16 位 offset + 6 位 offset2（实际存
  length-1，覆盖 1..64 unit），不把 pointer 放进每个 cell；
- `surrogate/grapheme/complex` 的完整文本都在 row store；`scalar/blank/background` 不访问
  store；
- store 只增长；超过 `UINT16_MAX` 时扫描该 row 的 cell，重建 vector 并更新
  offset/offset2；
- row 移动时连同 optional `GraphemeStore` 一起移动；page 滚动移动 page，不搬移 cluster 文本；
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
- cell 的 `primary` 在 `scalar` 时是 BMP code unit；content=2 时固定为 `U+FFFD`；
- 完整 cluster（含真实 primary）在 row 的 `std::vector<char16_t>`；cell 存 offset/offset2；
- cluster 长度上限按 **UTF-16 unit** 计算，硬上限 64；offset2 保存 length-1；
- 超过上限的输入要明确策略：
  - 拒绝并返回错误；
  - 或替换为单个 `U+FFFD` cell；
  - 不能把半个 surrogate pair/grapheme 写入多个 cell 导致列宽漂移；
- 组合标记、variation selector、ZWJ、regional indicator、tag sequence 都要在写入 cell
  之前完成 grapheme 分割；
- selection/copy/search 以 cell/cluster 为单位；复杂 cluster 从 row store 读取完整
  UTF-16 文本，不从单个 code unit 反推；
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

- `scalar`（单个 BMP code unit）：写入对应 `WCHAR`；
- `content=2`（`complex`/`surrogate`/`grapheme` 任一别名）：整个 cell 投影为 `U+FFFD`，
  与 conhost 的 `Utf16ToUcs2` 规则一致；cell 的 primary 本身也已经是 `U+FFFD`，不能
  取 side store 的首个 unit 或半个 pair 污染相邻列；
- trailing/spacer：写空字符并保留属性；
- style：投影为 legacy `WORD`；
- 读取矩形时按 cell 直接扫描，不重新做 Unicode 宽度计算。

**写入路径 A：`WriteConsoleOutput` / `CHAR_INFO`**

```text
application
  → CHAR_INFO rectangle
  → one CHAR_INFO = one WCHAR = one cell
  → width flags (LEADING/TRAILING)
  → cell/page write
```

规则：

- 不跨 `CHAR_INFO` 边界自动合并 high/low surrogate；conhost 的 `CHAR_INFO` 构造路径就是
  一个 `CHAR_INFO` 对应一个 `wchar_t` cell；
- `COMMON_LVB_LEADING_BYTE`/`TRAILING_BYTE` 决定宽字符列跨度，而不是 Unicode 宽度；
- 0 字符、trailing 标记和 NUL 的语义按 conhost 兼容规则处理；
- 每个非 0 `CHAR_INFO` 先被视为一个 BMP code unit；孤立 surrogate 规范为 `U+FFFD`，
  不能让 high surrogate 单独进入 `scalar`/渲染路径；
- 这是一条**兼容优先、明确有损**的路径。

**写入路径 B：`WriteConsoleW` / `WriteConsoleOutputCharacterW`**

```text
application
  → UTF-16 string
  → code-unit decoder (combine surrogate pair)
  → grapheme segmentation
  → full cluster in char16_t side store
  → cell with offset/offset2
```

规则：

- 合法 high surrogate + low surrogate 合并为一个完整 UTF-16 cluster，而不是两个独立 cell；
- 孤立 surrogate → `U+FFFD`；
- 按完整 grapheme 写入，而不是每个 UTF-16 code unit 独立写 cell；
- side store 保存包含 primary 的完整 cluster；cell 的 `primary` 与 store 第一个 unit 一致；
- 读取时仍按 6.1 的读取投影规则返回 `CHAR_INFO`，因此写入更丰富不代表读取无损。

conhost 的 `WriteConsoleW` legacy 路径按 grapheme 写入，`WriteConsoleOutputCharacterW`
的 `OutputCellIterator` 默认按 codepoint 写入。corehost 可以把两者统一为 grapheme 粒度，
但这是相对 conhost 的行为差异；严格兼容模式应保留 `WriteConsoleOutputCharacterW` 的
逐 codepoint 语义，并记录该路径无法在 API 往返中保持完整 cluster。

当前 `from_char_info` 把每个 `CHAR_INFO` 当成一个 wchar 写入，且只规范化 high
surrogate；这符合 `CHAR_INFO` 路径“不跨 cell 合并”的方向，但需要补齐孤立
low surrogate 的规范化，并和 `WriteConsoleW` 的 UTF-16 字符串路径明确分开。

### 6.2 DirectWrite：UTF-16 run + cluster map

窗口渲染不应把每个 cell 单独交给 DWrite。推荐按可见行的 run 做：

```text
Cell row
  → group by style/font/width policy
  → scalar: append primary UTF-16 unit
    complex/surrogate/grapheme: append full cluster from row store
  → build cell_index → UTF-16 offset map
  → DWrite analyzer/shaping
  → map glyph clusters back to cell clusters
  → cache shaped runs
  → draw glyphs at cell positions
```

细节：

- scalar cell 只有一个 UTF-16 unit；
- surrogate/grapheme/complex cell 从 row store 读取完整 cluster，所有 unit 都由同一个 cell
  拥有，不能拆成多个 cell 的独立文本；
- 一个多码点 cluster 的所有 UTF-16 unit 都映射到同一个 cell cluster；
- DWrite 的 glyph cluster 是 UTF-16 source offset，不是 cell index，必须通过 map 转换；
- ligature 可能让多个 cell 对应一个 glyph，fallback 必须以整个 cluster group 为单位；
- 字体 fallback 只对缺失 glyph 的完整 cluster 进行，不拆代理对、组合标记或 ZWJ；
- 颜色 emoji/COLR/CBDT 走 DWrite 的 color glyph 路径；
- 只为可见、脏的 run 做转换和 shaping；
- 缓存键至少包含 style、font、font size、DPI、文本 cluster 指纹。

Contour 的 DirectWrite shaper 已经示范了同样的 boundary 模式：输入 `std::u32string_view`，
在 shaper 边界转成 `wchar_t`，把非 BMP 的两个 UTF-16 unit 都映射到同一个 cell cluster，
然后用 UTF-16 glyph cluster 反推 cell cluster。corehost 的新模型只是把这个边界前移到
16 位 primary/side store，DWrite 的 cluster map 责任并没有消失。

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

- scalar cell 直接使用 primary UTF-16 unit；复杂 cell 从 row store 读取完整 UTF-16 cluster；
- 输出时把 UTF-16 code unit 编码为 UTF-8；不经过 `CHAR_INFO`；
- ASCII/单宽行可以直接批量输出；
- style ID 转 SGR 时只查 style table；
- 脏行/脏范围可以决定增量输出；
- 与窗口模式共享 cell、row、style 和 dirty 信息，只替换最后编码器。

这正好回应了共享代码需求：**共享的是屏幕模型和行迭代，不是最终编码格式**。16 位
primary 让 Windows 的 BMP 路径少一次转码；非 BMP/复杂 cluster 仍以完整 UTF-16 文本保存在
side store，不会因为无头输出而被拆成 surrogate cell。

## 7. conhost 与 Windows Terminal 的 Console API 往返策略

这一节回答一个关键问题：原版 conhost 明知 `CHAR_INFO`/Console API 有损，为什么仍能
高质量显示 Unicode？corehost 应该模仿它的哪一层，而不是模仿它的哪一层。

### 7.1 conhost 的内部表示不是 CHAR_INFO

conhost 的 `OutputCell` 持有 `std::wstring _text`，一个 cell 的文本可以是多个 UTF-16
code unit；`TextBuffer` 和 `OutputCellView` 也是围绕这个可变长度 cell 文本工作的。
这说明：

- conhost 内部**不是**把 `CHAR_INFO` 当作唯一真相；
- 内部 cell 可以保存 surrogate pair、组合序列或多码点文本；
- Windows Terminal 的 `TextBuffer`/renderer 读取的是这套丰富的 cell 文本，而不是
  `CHAR_INFO` 矩形；
- `CHAR_INFO` 是**Console API 的兼容投影**，不是内部渲染模型。

### 7.2 读取路径：主动替换为 U+FFFD

`CONSOLE_INFORMATION::AsCharInfo` 生成 `CHAR_INFO` 时调用 `Utf16ToUcs2(cell.Chars())`。
`Utf16ToUcs2` 的规则非常明确：

- cell 文本为空：失败；
- cell 文本长度等于 1 个 UTF-16 code unit：返回该 unit；
- cell 文本长度大于 1：返回 `U+FFFD`（`UNICODE_REPLACEMENT`）。

因此 `ReadConsoleOutput` 对以下情况返回替换字符，而不是把完整文本拆成多个 cell：

- 非 BMP 标量（内部是两个 UTF-16 code unit）；
- 组合字符序列；
- emoji ZWJ 序列；
- 其他多码点 grapheme cluster；
- 任何内部 cell 文本长度超过一个 code unit 的内容。

`ReadOutputStringW`（`ReadConsoleOutputCharacter` 的底层）也做同样的处理：遇到
`chars.size() > 1` 时把整个 cell 的文本替换为 `U+FFFD`，并且跳过 trailing cell。
它不是“把代理对拆成两格”，而是**承认无法表达，显式降级**。

这提供了一个重要结论：conhost 的 API 往返不是“尽量保留非 BMP”，而是**在 API 边界
明确采用替换语义**。内部显示质量与 API 投影被刻意分开。

### 7.3 写入路径：CHAR_INFO、codepoint 与 grapheme 三种粒度

`WriteConsoleOutputWImplHelper` 把每个 `CHAR_INFO` 包装成
`OutputCellIterator(CHAR_INFO)`，而 `OutputCellIterator` 的 `CHAR_INFO` 构造路径使用
单个 `wchar_t` 的 `wstring_view`：

- 一个 `CHAR_INFO` 对应一个 cell；
- 相邻的 high/low surrogate 不会被自动合并；
- 宽字符由 `COMMON_LVB_LEADING_BYTE`/`COMMON_LVB_TRAILING_BYTE` 表达列跨度；
- `WriteConsoleOutput` 不承诺把两个相邻 `CHAR_INFO` 还原成一个非 BMP 标量。

`WriteConsoleW` 走 `DoWriteConsole` → `WriteCharsLegacy` → `TextBuffer::Replace` 路径；
底层 `ROW::WriteHelper` 使用 `GraphemeNext` 推进，因此可以把一个完整 grapheme cluster
（包括代理对、组合标记和 ZWJ 序列）存入一个内部 cell。读取时仍会碰到 7.2 的替换规则。

`WriteConsoleOutputCharacterW` 走 `FillConsoleImpl` → `OutputCellIterator` 的宽字符串
路径；该路径使用 `til::utf16_next` **按 codepoint** 推进，因此能正确保留一个代理对
（两个 code unit 在同一个 cell），但不保证把多码点 grapheme cluster 整体放入一个 cell。
`OutputCell` 本身可以持有更长的 `std::wstring`，但这条 API 路径的默认迭代粒度是
codepoint。

因此，conhost 的三条写入路径粒度不同：

- `WriteConsoleW`：legacy 路径按 grapheme 写入；
- `WriteConsoleOutputCharacterW`：按 codepoint 写入；
- `WriteConsoleOutput`：按一个 `CHAR_INFO` = 一个 `WCHAR` = 一个 cell 写入。

### 7.4 旧 Console API 的 VT 输出也会降级

conhost 的 `VtIo::Writer::WriteInfos` 接受 `CHAR_INFO` 范围并写入 VT 输出；它的宽字符
处理会：

- 丢弃 trailing 半列；
- 在边界不完整时补空格；
- 对 surrogate 调用 `SanitizeUCS2`，结果是 `U+FFFD`。

因此，通过旧 `WriteConsoleOutput` 路径输出到终端时，非 BMP 也可能降级。这个降级发生在
**CHAR_INFO/VT writer 边界**，而不是 DWrite renderer 内部。对于现代应用直接写 VT 的路径，
文字可以以 UTF-8/VT 保留，不需要经过 `CHAR_INFO`。

### 7.5 Windows Terminal 的位置

Windows Terminal 不是 Console API 的实现者。它消费 VT/UTF-8 文本，或用 conhost 共享的
`TextBuffer`/`OutputCell` 作为显示数据；`AtlasEngine` 再把这些文本交给 DirectWrite
（DWrite 的 text format/layout 路径）渲染。Windows Terminal 不需要把 cell 投影成
`CHAR_INFO`，所以它可以显示 conhost 的 Console API 无法读回的多码点文本。

这说明原版体系的分层是：

```text
内部 TextBuffer/OutputCell（丰富 UTF-16 文本）
        ├── Console API CHAR_INFO 投影（有损，明确替换）
        └── VT/DWrite 渲染路径（尽可能保留完整文本）
```

### 7.6 往返行为矩阵

| 操作路径 | conhost 的内部结果 | 再读回的结果 | 结论 |
| --- | --- | --- | --- |
| `WriteConsoleW` 写入 BMP 单码点 | 一个 UTF-16 unit 的 cell | `ReadConsoleOutput`/`ReadConsoleOutputCharacter` 返回同一 unit | 可往返（属性仍按 legacy 映射） |
| `WriteConsoleW` 写入合法 surrogate pair 或 grapheme cluster | legacy `TextBuffer` 路径按 grapheme 写入，cell 可保存完整 cluster | 读取返回 `U+FFFD` | 内部保留完整 cluster，读取有损 |
| `WriteConsoleOutputCharacterW` 写入合法 surrogate pair | `OutputCellIterator` 按 codepoint 推进，一个 cell 保存两个 UTF-16 unit | 读取返回 `U+FFFD` | 保留一个 codepoint，读取有损 |
| `WriteConsoleOutputCharacterW` 写入组合序列/emoji cluster | 按 codepoint 拆成多个 cell（通常不是完整 grapheme） | 读回按 cell/投影处理，不能还原完整 cluster | 写入端不做完整 grapheme 合并 |
| `WriteConsoleOutput` 的 `CHAR_INFO` 含 high/low surrogate 两格 | 每格按一个 `WCHAR` 存储，不自动合并 | 读回两个独立 UTF-16 unit，不能还原原标量 | 不做代理对重组 |
| `WriteConsoleOutput` 写入宽字符 | 由 `COMMON_LVB_LEADING_BYTE`/`TRAILING_BYTE` 表达列跨度 | trailing 按 cell 规则处理 | 宽字符宽度语义保留，文本内容受 `CHAR_INFO` 限制 |
| 直接 VT/UTF-8 路径（Windows Terminal 接收） | VT 文本不经过 `CHAR_INFO` | 终端按 Unicode 文本渲染 | 不受 Console API 投影限制 |
| Windows Terminal/DWrite 渲染共享 `TextBuffer` | 读取完整 `OutputCell` 文本 | 渲染显示完整 cluster | 不经过 Console API |

这张矩阵说明：**“内部能否表示”与“Console API 能否往返”是两个独立问题**。conhost 选择
让内部表示更丰富，让 Console API 明确有损；没有为了 `CHAR_INFO` 而放弃内部 Unicode。

### 7.7 对 corehost 的直接结论

corehost 应该模仿 conhost 的分层，而不是把内部 cell 改成 `CHAR_INFO` 形状：

1. **内部 cell 保留 16 位 primary + 完整 cluster side store**，不因为 Console API 有损
   而丢弃 emoji/组合字符；非 BMP 的完整 surrogate pair 必须留在同一个 cluster 中。
2. **`ReadConsoleOutput`/`ReadConsoleOutputCharacter` 的投影规则应与 conhost 对齐**：
   - 一个 cell 的完整文本正好是一个 UTF-16 code unit 时返回该 unit；
   - cell 是多码点/多 UTF-16 unit 时返回 `U+FFFD`，而不是首个 codepoint；
   - trailing/leading、属性和宽字符 flags 按 conhost 规则保留。
3. **`WriteConsoleOutput` 的每个 `CHAR_INFO` 继续被解释为一个 cell 一个 `WCHAR`**，
   不自动合并相邻 surrogate pair；这是兼容性行为，不应在底层偷偷改变。
4. **宽字符串写入路径**的目标模型应统一为：先合并合法 surrogate pair，再做完整
   grapheme 分割，然后写入丰富内部 cell。conhost 的 `WriteConsoleW` legacy 路径已经按
   grapheme 写入，而 `WriteConsoleOutputCharacterW` 的 `OutputCellIterator` 默认按
   codepoint 写入；corehost 可以选择统一提升到 grapheme 粒度，但必须记录这是相对
   conhost 的行为差异。严格兼容实现则保留 `WriteConsoleOutputCharacterW` 的逐 codepoint
   语义。无论选哪种，读回仍按 2 的规则有损。
5. **窗口模式**直接从内部 cell 生成 DWrite UTF-16 run，不经过 `CHAR_INFO`；因此窗口可以
   显示 `ReadConsoleOutput` 读不出的 emoji/cluster。
6. **无头模式**直接从内部 cell 生成 UTF-8/VT；不需要为了 Console API 投影把内部文本
   先降级。
7. **旧 Console API 的 VT writer** 对 non-BMP 若必须走 `CHAR_INFO`，应明确按 conhost
   的替换规则输出；现代 VT/raw 路径保留完整 UTF-8。

这条策略的关键是：**有损只发生在 Console API 投影和旧 CHAR_INFO writer，不发生在
屏幕缓冲区、窗口渲染或无头 VT 输出。**

## 8. 性能分析

### 8.1 当前结构的成本

当前每次复杂 glyph 写入：

1. 可能物化 `_columns`；
2. 替换 `_text` 中的一段；
3. 计算长度差；
4. 平移后续所有非 trailing 列的 offset；
5. 更新 trailing 标记和属性。

这使中间插入/删除一个多码点 glyph 成为 O(width) 操作。对于 ASCII 快路径，很多写操作
不会触发；但一旦窗口程序输出 emoji/组合字符，复杂行会退化。Scroll/resize 还会复制多个
变长容器。

### 8.2 新结构的成本

新结构：

- 写入一个 cell 是 O(1)；
- 覆盖一个 cluster 先清理旧的 side pool entry，再写入新 entry；
- 不移动其他 cell 的 offset；
- 行内 ASCII 批量写入直接写 cell 数组；
- blank/uniform 行不分配 cell；
- page 滚动是 row/page 索引操作，不复制整行字符串；
- 只有多码点 cluster 才访问 side table，慢路径局限在真正复杂的 cell。

### 8.3 内存估算

8 字节 cell 的粗略估算：

| 网格 | cell 内存 | 说明 |
| --- | ---: | --- |
| 120 × 30 | 28.8 KB | 单屏 |
| 300 × 100 | 240 KB | 大窗口 |
| 1000 × 200 | 1.6 MB | 大 scrollback 区块 |

此外：

- blank row 不分配；
- style table 只保存实际使用的样式；
- grapheme store 只保存 surrogate/grapheme/complex cluster 的完整 UTF-16 文本；
- 可见行 UTF-16 scratch 约为 2 bytes × UTF-16 units；120×30 满屏最多约 7.2 KB；
- Console API 的 `CHAR_INFO` 矩形是 4 bytes/cell，按调用临时分配/复用。

这个混合模型没有把 non-BMP 拆成两个独立 surrogate cell；复杂 cluster 仍由一个 cell 指向
完整 side store。与纯 UTF-16 canonical 相比，它多了一次 side store 间接访问，但避免了
surrogate pair 跨 cell、selection/copy/search 和 DWrite cluster map 的语义分裂。如果未来
证明 ASCII 内存是瓶颈，可以增加 row-level ASCII compression，而不是把完整 cluster 拆成
多个 cell。

### 8.4 转换成本

新模型的编码路径比“纯 UTF-32 行字符串”更短：

- scalar cell 的 primary 已经是 UTF-16 unit，BMP 不需要 codepoint→UTF-16 转码；
- surrogate/grapheme/complex cell 的完整文本已经在 row-local `std::vector<char16_t>` 中，
  DWrite 只需要组装 run，不需要再从 UTF-32 展开代理对；
- DWrite 的 shaping、fallback、glyph rasterization 成本仍然远高于 side store 读取；
- 只需要读取可见/脏行，并可缓存 UTF-16 run；
- Console API 的 `CHAR_INFO` 转换仍然必须做属性/列投影，不能靠 16 位 primary 消除；
- 无头模式需要把 UTF-16 cluster 编码为 UTF-8；这是唯一新增的稳定成本，但只发生在
  输出边界，且可按行批量编码。

因此，16 位 primary 的收益是让 Windows 的 BMP/UTF-16 路径少一层编码，同时保留完整
cluster side store 的 Unicode 质量；它不试图把 non-BMP 拆成独立 cell 来换取“零转码”。

## 9. 正确性和边界条件

### 9.1 非 BMP

- 内部 cell 的 primary 是 16 位 UTF-16 code unit；content=2 时固定为 `U+FFFD`；
- 完整代理对（含 high+low）保存在 row-local `std::vector<char16_t>`，由 cell 的
  offset/offset2 指向；
- 非 BMP 不能被拆成两个独立 surrogate cell；`complex/surrogate/grapheme` 都指向完整
  store；
- `CHAR_INFO` 读取只能写替代字符，这是 API 限制，必须在文档/测试中固定；
- `WriteConsoleW`/宽字符串路径合并合法 surrogate pair；`WriteConsoleOutput` 的
  `CHAR_INFO` 路径按 conhost 保持每格一个 `WCHAR`，不跨格合并；
- 孤立 surrogate 在进入 cell 前统一替换为 `U+FFFD`。

### 9.2 组合字符和 emoji

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

### 9.3 Console API 往返

- `ReadConsoleOutput` 后 `WriteConsoleOutput` 的 BMP 文本/属性应保持一致；
- 非 BMP/多码点 cluster 的读取投影应返回 `U+FFFD`，而不是首个 codepoint 或半个代理对；
- `WriteConsoleW`/宽字符串路径的合法 surrogate pair 合并，而不是写两个 replacement；
- `WriteConsoleOutput` 的相邻 `CHAR_INFO` 不自动合并代理对，保持 conhost 的单 `WCHAR`
  cell 语义；
- `CHAR_INFO` 的 0 字符/trailing 语义必须与 conhost 一致；
- `GetConsoleScreenBufferInfo`/属性查询使用 legacy 投影，不应改变内部 style。

### 9.4 窗口和无头共享

- VT 输出和 DWrite 渲染必须读取同一 cell/page，不允许两套文本存储；
- 无头输出直接从 scalar primary 或完整 cluster 编码 UTF-8，不经过 `CHAR_INFO` 投影；
- 窗口输出 UTF-16 必须带 cell→UTF-16 cluster map；
- 两边都只读取/清理 dirty 范围；
- style table 更新要同时使 VT SGR 缓存和 DWrite shaping cache 失效。

## 10. 迁移方案

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

## 11. 风险与取舍

1. **内存**：8 字节 cell 比 UTF-16 code unit 更占内存。需要 page/blank 压缩和
   scrollback 容量控制；如果 ASCII 内存成为瓶颈，增加 row-level ASCII fast path，
   而不是改成 UTF-16 canonical。
2. **Style ID 生命周期**：page-local style ID 在跨 page 复制/滚动时要重新映射；16 位
   ID 溢出要可检测。
3. **Cluster 池所有权**：cluster 数据跟随 row 的 `GraphemeStore`；移动 cell/row 时必须
   移动/复制完整 cluster，并更新 cell 的 offset/offset2；超过 16 位 offset 时要重建；
   需要严格的完整性检查。
4. **DWrite cluster map**：UTF-16 offset、surrogate pair、ligature、fallback 都不能拆
   cluster；这是实现复杂度的主要来源，但任何 canonical 表示都无法完全避免。
5. **Console API 有损性**：`CHAR_INFO` 的非 BMP/cluster 投影无法无损；必须明确规则并
   固化测试，不能为了 API 兼容牺牲内部窗口/VT 的 Unicode 质量。
6. **宽度策略**：Console legacy 宽度与终端 grapheme 宽度不同；必须按写入来源选择策略，
   不能在渲染时重新计算，否则同一 cell 的列宽会漂移。
7. **输入边界**：UTF-16 输入中的 surrogate pair、孤立 surrogate、NUL 和 trailing cell
   语义需要统一处理，避免不同 API 路径产生不同结果。

## 12. 结论

corehost 当前的问题可以概括为：

> 使用 Unicode 作为语义单位是正确的；使用**整行 UTF-32 字符串 + 每列偏移表**作为容器是
> 低效的；把 non-BMP 拆成两个独立 UTF-16 surrogate cell 的 canonical 模型又会破坏
> grapheme/selection/DWrite 的完整性。

推荐的设计是：

- **Cell 固定 8 字节**：16 位 UTF-16 primary、16 位 row-local cluster offset、16 位
  page-local style ID、6 位 offset2（实际存 length-1，覆盖 1..64 UTF-16 unit）、2 位
  content、2 位 width、5 位 flags、1 位 reserved；content=2 的 primary 为 `U+FFFD`；
- **行级完整 cluster store**：`std::vector<char16_t>` 保存包括 primary 在内的完整
  surrogate pair/grapheme，超出 16 位 offset 时扫描 row 重建；
- **page + row + dense cell**：支持主/备用缓冲区和未来 scrollback；
- **style table**：内部保存完整的颜色/属性/超链接，legacy `WORD` 只在 Console API 投影；
- **UTF-16 primary 不是 canonical split**：非 BMP/复杂内容始终以完整 cluster 表示，
  Console API 和 DWrite 只在边界做投影/映射；
- **无头 UTF-8/VT 与窗口 DWrite 共享 cell/row/style**，只在最终编码器分叉。

conhost 的做法进一步支持这个结论：内部 `OutputCell` 保存完整文本，但
`ReadConsoleOutput`/`ReadConsoleOutputCharacter` 对多 unit cell 返回 `U+FFFD`，
`WriteConsoleOutput` 把每个 `CHAR_INFO` 当作一个 `WCHAR`；Windows Terminal 则用
`TextBuffer`/`OutputCell` 和 DWrite 渲染完整文本。corehost 应该采用同样的分层：**内部保留
完整 cluster，Console API 边界集中做有损投影，窗口和无头路径绕过该投影。**

当前 corehost 的 `to_char_info` 对多码点 cluster 取首个 codepoint，这和 conhost 的
`Utf16ToUcs2` 行为不一致。迁移时应改为：**多 unit cell 整体返回 `U+FFFD`**；BMP 单
unit cell 保持原字符。这样既保持内部窗口/VT 的 Unicode 质量，也让 Console API 行为与
conhost 一致。

Contour 使用 `char32_t` primary + cluster pool + `LineShape`，Ghostty 使用 8 字节 packed
cell + style ID + grapheme map；它们证明的是“完整 cluster 必须由 side store 保持，平台
shaper 只做边界转换”，而不是“必须使用某一固定编码”。corehost 的 16 位 UTF-16 primary
是 Windows 专用优化：BMP 直接对齐 `CHAR_INFO`/DWrite，非 BMP/复杂 cluster 仍由完整
side store 表示，没有采用会拆散 surrogate 的 UTF-16 canonical。

## 13. UTF-16 primary cell 的评估与 C++ 定义

### 13.1 修正后的设计判断

“cell 使用 16 位 UTF-16 primary + 完整 cluster side store”这个方向是合理的。经过这轮
约束修正后，规则是：

1. `content` 只有 2 位，值 2 表示“需要多个 UTF-16 code unit”；
   `complex`/`surrogate`/`grapheme` 是同一个值 2 的别名，不再区分类型。
2. 当 `content == 2` 时，cell 的 `primary` 固定为 `U+FFFD`；真实 primary 和完整
   surrogate pair/grapheme 都在 row-local side store。
3. cell 保存 16 位 offset 和 6 位 `offset2`；`offset2` 的实际语义是
   `length - 1`（1..64 个 UTF-16 unit），不是第二个存储地址。
4. `offset2` 使用 6 位：0..63 加上 1 正好覆盖 1..64；`cluster_length()` 负责加一。
5. `style` 排在 `offset` 之后、`offset2` 之前，使 style 位于 16 位边界。
6. `grapheme_store::append()` 不负责检查容量；调用者必须先调用 `can_append()`，再
   append；容量不足时由 row 执行重建。

> 命名说明：`offset2` 是按用户指定的历史名称，但它不表示 offset。它实际保存
> cluster length-1；代码注释和访问器必须明确这一点，避免把 `offset2` 误用为第二个
> 存储地址。

#### 8 字节预算

| 字段 | 位数 | 含义 |
| --- | ---: | --- |
| `primary` | 16 | BMP code unit；content=2 时为 `U+FFFD` |
| `offset` | 16 | row-local cluster store offset |
| `style_id` | 16 | page-local style table 索引，16 位对齐 |
| `offset2` | 6 | 实际保存 cluster length-1（1..64 个 UTF-16 unit） |
| `content` | 2 | blank/scalar/complex(alias surrogate/grapheme)/background |
| `width` | 2 | narrow/wide/tail/spacer head |
| `flags` | 5 | protected/hyperlink/semantic/reserved |
| `reserved` | 1 | 补齐到 8 字节 |
| **总计** | **64** | **8 字节** |

不能把 `offset2` 扩到 16 位：`primary 16 + offset 16 + style 16 + offset2 16 = 64`，
已经没有 content/width/flags 的空间。当前 6 位长度覆盖 1..64 个 UTF-16 unit，足以覆盖
真实 RGI emoji；如果未来要扩大长度上限，需要重新分配 cell 字段，而不是简单加宽。

#### 为什么 offset2 是 6 位

6 位无符号字段的范围是 0..63。保存 `length - 1` 后，可以表示 1..64 个 UTF-16 unit；
这正好覆盖 `cell::max_cluster_units = 64`。读取时使用 `cluster_length()` 返回
`offset2 + 1`，外部代码不应直接使用原始 `offset2` 值作为长度。

#### 为什么 style 放在 offset 后面

字段顺序为：

```text
primary 16 | offset 16 | style 16 | offset2 6 | content 2 | width 2 | flags 5 | reserved 1
```

`style` 从 bit 32 开始，正好落在 16 位边界；`offset2` 从 bit 48 开始。这样 style 是
自然的 `std::uint16_t` 对齐值，不需要跨 16 位字读取。

#### 关于 content 的重复值

```cpp
complex = 2,
surrogate = 2,
grapheme = 2,
```

在 C++ 中这是合法的别名定义。它们都表示“这个 cell 需要多个 UTF-16 code unit，primary
是 `U+FFFD`，完整文本在 side store”。因此不应再用 `content()` 去区分 surrogate 和
grapheme；如果未来确实需要区分，应使用额外的 flags 或 side metadata，而不是增加 content
位宽。

### 13.2 C++ 定义

以下代码以 **C++23** 为基线，因为使用 `std::to_underlying`。代码是接口/布局设计，不是
完整实现。

```cpp
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <type_traits>
#include <utility>
#include <vector>

namespace corehost::screen
{

using style_id = std::uint16_t;

// 2 bit content。complex/surrogate/grapheme 是同一个值 2 的别名：
// 都表示“需要多个 UTF-16 code unit，primary 为 U+FFFD，完整文本在 side store”。
enum class cell_content : std::uint8_t
{
    blank = 0,
    scalar = 1,
    complex = 2,
    surrogate = 2,
    grapheme = 2,
    background = 3,
};

// 2 bit 列语义。
enum class cell_width : std::uint8_t
{
    narrow = 0,
    wide = 1,
    tail = 2,
    spacer_head = 3,
};

// 5 bit flags，当前使用低 4 位。
enum class cell_flag : std::uint8_t
{
    none = 0,
    protected_cell = 1u << 0,
    hyperlink = 1u << 1,
    semantic_input = 1u << 2,
    semantic_prompt = 1u << 3,
};

// 终端网格的最小存储单元。
//
// - content=scalar 时 primary 是实际 BMP code unit；
// - content=2 时 primary 固定为 U+FFFD，完整 cluster 在 row-local grapheme_store；
// - offset/offset2 只在 content=2 时有效；offset2 实际是 length-1；
// - style_id 是 page-local 索引；
// - cell 固定 8 字节，全部字段是 std::uint64_t bitfield。
class cell final
{
public:
    using flag = cell_flag;

    // 一个 cluster 最多 64 个 UTF-16 code unit。
    static constexpr std::uint8_t max_cluster_units = 64;

    // 零值：blank、默认样式、无 cluster。
    constexpr cell() noexcept = default;

    // 空白 cell：没有文字，只有样式。
    [[nodiscard]] static constexpr cell blank(style_id style = 0) noexcept
    {
        return make(u' ', style, cell_content::blank, cell_width::narrow, 0, 0, 0);
    }

    // 单个 BMP code unit。孤立 surrogate 会被规范为 U+FFFD；非 BMP 请使用
    // complex()/surrogate()/grapheme() 并提供 side store。
    [[nodiscard]] static constexpr cell scalar(char16_t primary,
                                               style_id style = 0,
                                               cell_width width = cell_width::narrow) noexcept
    {
        return make(is_surrogate_unit(primary) ? u'\uFFFD' : primary,
                    style,
                    cell_content::scalar,
                    width,
                    0,
                    0,
                    0);
    }

    // 多 UTF-16 code unit cell。primary 固定为 U+FFFD；真实文本在 side store。
    [[nodiscard]] static constexpr cell complex(std::uint16_t offset,
                                                std::uint8_t length,
                                                style_id style = 0,
                                                cell_width width = cell_width::narrow) noexcept
    {
        return make(u'\uFFFD', style, cell_content::complex, width, offset, length, 0);
    }

    // complex 的语义别名；它们不增加新的 content 值。
    [[nodiscard]] static constexpr cell surrogate(std::uint16_t offset,
                                                  std::uint8_t length,
                                                  style_id style = 0,
                                                  cell_width width = cell_width::narrow) noexcept
    {
        return complex(offset, length, style, width);
    }

    [[nodiscard]] static constexpr cell grapheme(std::uint16_t offset,
                                                 std::uint8_t length,
                                                 style_id style = 0,
                                                 cell_width width = cell_width::narrow) noexcept
    {
        return complex(offset, length, style, width);
    }

    // 只有背景色、没有文字。
    [[nodiscard]] static constexpr cell background(style_id style = 0) noexcept
    {
        return make(u' ', style, cell_content::background, cell_width::narrow, 0, 0, 0);
    }

    // 宽字符的第二列。
    [[nodiscard]] static constexpr cell tail(style_id style = 0) noexcept
    {
        return make(u' ', style, cell_content::blank, cell_width::tail, 0, 0, 0);
    }

    // 行尾 soft-wrap 的宽字符延续标记。
    [[nodiscard]] static constexpr cell spacer_head(style_id style = 0) noexcept
    {
        return make(u' ', style, cell_content::blank, cell_width::spacer_head, 0, 0, 0);
    }

    // primary code unit；content=2 时固定为 U+FFFD。
    [[nodiscard]] constexpr char16_t primary() const noexcept
    {
        return static_cast<char16_t>(primary_);
    }

    // 单 code unit 视图：blank 返回空格；复杂 cell 返回 U+FFFD。
    [[nodiscard]] constexpr char16_t text_unit() const noexcept
    {
        return is_blank() ? u' ' : primary();
    }

    // page-local style ID。
    [[nodiscard]] constexpr style_id style() const noexcept
    {
        return style_;
    }

    [[nodiscard]] constexpr cell_content content() const noexcept
    {
        return static_cast<cell_content>(content_);
    }

    [[nodiscard]] constexpr cell_width width() const noexcept
    {
        return static_cast<cell_width>(width_);
    }

    [[nodiscard]] constexpr std::uint8_t flags() const noexcept
    {
        return static_cast<std::uint8_t>(flags_);
    }

    [[nodiscard]] constexpr bool has(flag value) const noexcept
    {
        return (flags_ & std::to_underlying(value)) != 0;
    }

    [[nodiscard]] constexpr bool is_blank() const noexcept
    {
        return content_ == std::to_underlying(cell_content::blank) ||
               content_ == std::to_underlying(cell_content::background);
    }

    // content=2 即需要 side store；complex/surrogate/grapheme 在这里不可区分。
    [[nodiscard]] constexpr bool has_cluster() const noexcept
    {
        return content_ == std::to_underlying(cell_content::complex);
    }

    [[nodiscard]] constexpr bool is_complex() const noexcept
    {
        return has_cluster();
    }

    [[nodiscard]] constexpr bool is_wide() const noexcept
    {
        return width_ == std::to_underlying(cell_width::wide);
    }

    [[nodiscard]] constexpr bool is_continuation() const noexcept
    {
        return width_ == std::to_underlying(cell_width::tail);
    }

    [[nodiscard]] constexpr bool is_spacer_head() const noexcept
    {
        return width_ == std::to_underlying(cell_width::spacer_head);
    }

    // 仅 has_cluster() 时有效。
    [[nodiscard]] constexpr std::uint16_t cluster_offset() const noexcept
    {
        return offset_;
    }

    // 实际长度（1..64）；非 cluster 返回 0。offset2_ 保存 length-1，
    // 这里负责加一，调用方不要直接使用 offset2 原始值。
    [[nodiscard]] constexpr std::uint8_t cluster_length() const noexcept
    {
        return has_cluster() ? static_cast<std::uint8_t>(offset2_ + 1) : 0;
    }

    // 返回替换 style 的副本。
    [[nodiscard]] constexpr cell with_style(style_id value) const noexcept
    {
        auto result = *this;
        result.style_ = value;
        return result;
    }

    // 返回设置/清除 flag 的副本。
    [[nodiscard]] constexpr cell with_flag(flag value, bool enabled = true) const noexcept
    {
        auto result = *this;
        const auto bit = std::to_underlying(value);
        result.flags_ = enabled ? static_cast<std::uint64_t>(result.flags_ | bit)
                                : static_cast<std::uint64_t>(result.flags_ & ~bit);
        return result;
    }

    // 重建 side store 后，用新的 offset 和实际 length 更新 cell。
    // offset2_ 保存 length-1，所以这里做一次减一。
    [[nodiscard]] constexpr cell with_cluster_ref(std::uint16_t offset,
                                                  std::uint8_t length) const noexcept
    {
        auto result = *this;
        result.offset_ = offset;
        result.offset2_ = length == 0 ? 0 : static_cast<std::uint64_t>(length - 1);
        return result;
    }

    friend constexpr bool operator==(const cell &, const cell &) noexcept = default;

private:
    // 所有构造经 make()，集中维护字段顺序和 enum 编码。
    [[nodiscard]] static constexpr cell make(char16_t primary,
                                             style_id style,
                                             cell_content content,
                                             cell_width width,
                                             std::uint16_t offset,
                                             std::uint8_t length,
                                             std::uint8_t flags) noexcept
    {
        cell result;
        result.primary_ = static_cast<std::uint64_t>(static_cast<std::uint16_t>(primary));
        result.offset_ = offset;
        result.style_ = style;
        result.offset2_ = length == 0 ? 0 : static_cast<std::uint64_t>(length - 1);
        result.content_ = std::to_underlying(content);
        result.width_ = std::to_underlying(width);
        result.flags_ = static_cast<std::uint64_t>(flags) & 0x1F;
        return result;
    }

    [[nodiscard]] static constexpr bool is_surrogate_unit(char16_t value) noexcept
    {
        return (value & 0xF800u) == 0xD800u;
    }

    // 字段顺序：primary | offset | style | offset2 | content | width | flags | reserved。
    // style 从 bit 32 开始，保证 16 位对齐。
    // offset2 虽然名为 offset2，实际保存 length-1；保留该历史名称是刻意约定。
    std::uint64_t primary_ : 16;
    std::uint64_t offset_ : 16;
    std::uint64_t style_ : 16;
    std::uint64_t offset2_ : 6;
    std::uint64_t content_ : 2;
    std::uint64_t width_ : 2;
    std::uint64_t flags_ : 5;
    std::uint64_t reserved_ : 1;
};

static_assert(sizeof(cell) == sizeof(std::uint64_t));
static_assert(alignof(cell) == alignof(std::uint64_t));
static_assert(std::is_trivially_copyable_v<cell>);
static_assert(std::is_standard_layout_v<cell>);

// Row-local 完整 cluster store。调用者负责容量检查：
//   if (!store.can_append(cluster.size())) row.rebuild();
//   auto ref = store.append(cluster);
class grapheme_store final
{
public:
    static constexpr std::uint8_t max_cluster_units = cell::max_cluster_units;
    static constexpr std::size_t max_units = std::numeric_limits<std::uint16_t>::max();

    struct ref final
    {
        std::uint16_t offset{};
        std::uint8_t length{};
    };

    // 调用者必须保证 units>0、units<=64，且 units_ + units <= UINT16_MAX。
    [[nodiscard]] bool can_append(std::size_t units) const noexcept
    {
        return units > 0 && units <= max_cluster_units &&
               units <= max_units - units_.size();
    }

    // 只负责写入；容量检查由调用者完成。caller 未检查时 assert 失败（debug）。
    [[nodiscard]] ref append(std::span<const char16_t> cluster)
    {
        assert(can_append(cluster.size()));

        const auto offset = static_cast<std::uint16_t>(units_.size());
        units_.insert(units_.end(), cluster.begin(), cluster.end());
        return ref{offset, static_cast<std::uint8_t>(cluster.size())};
    }

    [[nodiscard]] std::span<const char16_t> view(ref value) const noexcept
    {
        if (value.length == 0 || value.offset + value.length > units_.size())
        {
            return {};
        }

        return std::span<const char16_t>{units_}.subspan(value.offset, value.length);
    }

    [[nodiscard]] bool needs_compaction() const noexcept
    {
        return !can_append(max_cluster_units);
    }

private:
    // 所有 cluster 的完整 UTF-16 文本连续存放，只增长；row 在必要时重建。
    std::vector<char16_t> units_;
};

} // namespace corehost::screen
```

### 13.3 配套的 style、row 和 page

`offset/offset2` 是 row-local 的，因此 row 拥有 `grapheme_store`；`style_id` 是 page-local
的，因此 page 拥有 `style_table`。

```cpp
#include <compare>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <unordered_map>
#include <vector>

namespace corehost::screen
{

struct rgb final
{
    std::uint8_t r{};
    std::uint8_t g{};
    std::uint8_t b{};

    friend constexpr auto operator<=>(const rgb &, const rgb &) noexcept = default;
};

enum class color_kind : std::uint8_t
{
    default_color = 0,
    indexed = 1,
    rgb = 2,
};

struct color final
{
    color_kind kind{color_kind::default_color};
    std::uint32_t value{};

    friend constexpr auto operator<=>(const color &, const color &) noexcept = default;
};

struct text_style final
{
    color foreground{};
    color background{};
    color underline{};
    std::uint16_t attributes{};
    std::uint16_t legacy_attributes{};

    friend constexpr auto operator<=>(const text_style &, const text_style &) noexcept = default;
};

struct text_style_hash final
{
    [[nodiscard]] std::size_t operator()(const text_style &style) const noexcept;
};

class style_table final
{
public:
    [[nodiscard]] style_id intern(const text_style &style);
    [[nodiscard]] const text_style &at(style_id id) const noexcept
    {
        return styles_[id];
    }

private:
    std::vector<text_style> styles_{text_style{}};
    std::unordered_map<text_style, style_id, text_style_hash> ids_;
};

enum class row_flag : std::uint16_t
{
    none = 0,
    blank = 1u << 0,
    wrapped = 1u << 1,
    dirty = 1u << 2,
    style_uniform = 1u << 3,
};

struct row final
{
    std::size_t first_cell{};
    std::size_t used_columns{};
    std::uint16_t flags{};
    std::optional<grapheme_store> graphemes{};
};

class page final
{
public:
    page(std::size_t rows, std::size_t columns)
        : cells_(rows * columns, cell::blank()), rows_(rows), columns_(columns)
    {
        for (std::size_t y = 0; y != rows; ++y)
        {
            rows_[y].first_cell = y * columns;
        }
    }

    [[nodiscard]] std::span<cell> cells(std::size_t y) noexcept
    {
        return std::span<cell>{cells_}.subspan(y * columns_, columns_);
    }

    [[nodiscard]] std::span<const cell> cells(std::size_t y) const noexcept
    {
        return std::span<const cell>{cells_}.subspan(y * columns_, columns_);
    }

    [[nodiscard]] row &row_at(std::size_t y) noexcept
    {
        return rows_[y];
    }

    [[nodiscard]] const row &row_at(std::size_t y) const noexcept
    {
        return rows_[y];
    }

    [[nodiscard]] style_table &styles() noexcept
    {
        return styles_;
    }

    [[nodiscard]] const style_table &styles() const noexcept
    {
        return styles_;
    }

private:
    std::vector<cell> cells_;
    std::vector<row> rows_;
    style_table styles_;
    std::size_t columns_{};
};

} // namespace corehost::screen
```

### 13.4 关键约束

1. **content=2 的统一语义**
   - `complex`/`surrogate`/`grapheme` 都是 2，不能通过 content 区分；
   - content=2 时 `primary()` 固定为 `U+FFFD`；
   - 真实 primary/cluster 在 side store；
   - 如果未来需要区分“非 BMP / 组合 grapheme / 其他”，用 flags 或 side metadata，
     不要扩大 content 字段。

2. **offset2 实际是 length-1**
   - `offset2` 字段保存 1..64 的 length-1（0..63），不是第二个 offset；
   - 6 位刚好覆盖 1..64；读取时用 `cluster_length()` 加一；
   - 非 cluster 的 offset2 为 0，读取时通过 `has_cluster()` 判断；
   - 这个名字具有误导性；如果未来允许重命名，建议改成 `length_minus_one_`。

3. **style 对齐**
   - 字段顺序为 primary/offset/style/offset2/content/width/flags/reserved；
   - style 从 bit 32 开始，是 16 位对齐；
   - offset2 从 bit 48 开始，不跨 style 的 16 位边界。

4. **append 容量由调用者检查**
   - `grapheme_store::append()` 不返回 optional，也不在内部重建；
   - 调用者先 `can_append(cluster.size())`，不足时执行 row 重建；
   - append 只用 debug assert 防御调用者遗漏；
   - 重建时扫描 row 的 cell，按顺序重新 append 完整 cluster，并用 `with_cluster_ref()`
     更新 offset/offset2；重建必须在会话锁内完成，旧 span 失效。

5. **bitfield 和 ABI**
   - 所有字段使用 `std::uint64_t` bitfield；
   - `sizeof == 8`、alignment、trivially copyable、standard-layout 都用静态断言锁住；
   - bitfield 顺序不是跨编译器 ABI，不能把 cell 直接序列化到磁盘/协议；
   - `std::to_underlying` 需要 C++23；C++20 项目用
     `static_cast<std::underlying_type_t<T>>`。

6. **Console API 和 DWrite**
   - `ReadConsoleOutput` 对 scalar 返回单个 `WCHAR`；content=2 返回 `U+FFFD`；
   - `WriteConsoleOutput` 的 `CHAR_INFO` 仍然是一格一个 `WCHAR`，不跨格合并代理对；
   - `WriteConsoleW` 先把 UTF-16 序列解码成完整 cluster，再分配 store 和 cell；
   - DWrite 对 scalar 直接使用 primary；对 complex 使用 store 的完整 UTF-16 units；
   - 无头 UTF-8 输出从 scalar primary 或完整 cluster 编码，不经过 `CHAR_INFO`。

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

### terminal / conhost

- `terminal/src/host/consoleInformation.cpp`
  - `CONSOLE_INFORMATION::AsCharInfo` 与 `ReadConsoleOutput` 的 `CHAR_INFO` 投影。
- `terminal/src/host/directio.cpp`
  - `ReadConsoleOutputWImplHelper`、`WriteConsoleOutputWImplHelper` 的矩形读写。
- `terminal/src/host/output.cpp`
  - `ReadOutputStringW` 对多 unit cell 的 `U+FFFD` 替换。
- `terminal/src/host/_stream.cpp`
  - `DoWriteConsole`/`WriteCharsLegacy` 的 `WriteConsoleW` legacy 写入路径。
- `terminal/src/buffer/out/Row.cpp`
  - `ROW::WriteHelper` 使用 `GraphemeNext` 写入完整 grapheme。
- `terminal/src/host/_output.cpp`
  - `FillConsoleImpl` 与 `WriteConsoleOutputCharacterW` 的 `OutputCellIterator` 路径。
- `terminal/src/types/convert.cpp`
  - `Utf16ToUcs2` 的“长度大于 1 即替换”规则。
- `terminal/src/buffer/out/OutputCell.hpp`
  - 内部 cell 的 `std::wstring _text`。
- `terminal/src/buffer/out/OutputCellIterator.cpp`
  - `CHAR_INFO` 单 `wchar_t` view 与宽字符串 `utf16_next` 路径。
- `terminal/src/host/VtIo.cpp`
  - `WriteInfos`/`SanitizeUCS2` 的旧 API VT 输出与 surrogate 替换。
- `terminal/src/renderer/atlas/AtlasEngine.api.cpp`
  - Windows Terminal 使用 TextBuffer/DWrite 的渲染路径。
