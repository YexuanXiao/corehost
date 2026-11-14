# corehost 屏幕缓冲区与 Unicode 数据模型技术分析

## 摘要

结论先说：**corehost 不应该把屏幕缓冲区的规范表示改为 UTF-16**。当前真正需要替换的，是
`screen_buffer_row` 中“整行 UTF-32 字符串 + 每列偏移表”的容器结构，而不是 Unicode 标量本身。

推荐的目标模型是：

1. 每个终端列使用一个紧凑的 **8 字节 packed cell**：
   - 21 位 Unicode 标量值（primary codepoint）；
   - 16 位 page-local style ID；
   - 16 位 row-local cluster ID（0 表示没有额外 codepoint）；
   - 2 位 content 类型；
   - 2 位列宽/continuation；
   - 其余 7 位用于 protected、hyperlink、semantic 等标志。
2. 多码点 grapheme cluster 只把 primary codepoint 放在 cell 内，额外码点放入行级
   cluster side pool；cell 保存 16 位 `cluster_id` 索引，不保存 pointer。
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
  cluster_id    : 16 bits   // row-local grapheme side-store index, 0 = none
  content       : 2 bits    // blank / scalar / grapheme / background-only
  width         : 2 bits    // narrow / wide / spacer tail / spacer head
  flags         : 7 bits    // protected / hyperlink / semantic, reserved
```

设计要求：

- 零值必须表示一个合法的空白 cell，便于 page 初始化和 `memset`；
- `codepoint` 是 21 位 Unicode scalar，不存 UTF-16 code unit；
- 非 BMP 字符仍然只占一个 primary codepoint；其显示宽度由 `wide` 和宽度策略决定；
- 多码点 cluster 的额外 codepoint 不放 cell 内，避免 cell 膨胀；
- `cluster_id` 是 row-local 的 side-store 索引，0 表示没有额外 codepoint；cell 不携带
  pointer，row 移动时不会产生悬空引用；
- style ID 是 page-local 的，默认 0 不查表；
- `content` 允许“只有背景色、没有文字”的 cell 不构造 grapheme；背景色仍由 style ID
  提供，不能把 RGB 直接塞进 codepoint；
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
- `GraphemeStore` 是 row-local side store，多码点 cluster 使用 `cluster_id` + offset/length；
- cell 的 `cluster_id` 只是 row-local 索引，不把 pointer 放进每个 cell；
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
- primary codepoint 在 cell 内；
- 额外 codepoint 在 row 的 `GraphemeStore`；
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
- 多码点 cluster 或多 UTF-16 unit：整个 cell 投影为 `U+FFFD`，与 conhost 的
  `Utf16ToUcs2` 规则一致；不能用首个标量污染相邻列；
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
- 写入孤立 surrogate 时必须在内部规范为 `U+FFFD` 或保留为明确的“不可渲染内容”，不能
  让非标量进入通用 codepoint/cluster 路径；
- 这是一条**兼容优先、明确有损**的路径。

**写入路径 B：`WriteConsoleW` / `WriteConsoleOutputCharacterW`**

```text
application
  → UTF-16 string
  → code-unit decoder (combine surrogate pair)
  → codepoint + grapheme segmentation
  → width policy
  → cell/page write
```

规则：

- 合法 high surrogate + low surrogate 合并为一个 Unicode 标量；
- 孤立 surrogate → `U+FFFD`；
- 按完整 grapheme 写入，而不是每个 UTF-16 code unit 独立写 cell；
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

1. **内部 cell 保留完整 primary codepoint + grapheme side pool**，不因为 Console API
   有损而丢弃 emoji/组合字符。
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
- grapheme pool 只保存多码点 cluster；
- 可见行 UTF-16 scratch 约为 2 bytes × UTF-16 units；120×30 满屏最多约 7.2 KB；
- Console API 的 `CHAR_INFO` 矩形是 4 bytes/cell，按调用临时分配/复用。

对比 UTF-16 canonical，8 字节 cell 的 ASCII 内存更高；但它避免了代理对 cell、cluster
side table 和 selection/copy/search 的额外复杂度，并且与 Ghostty 的 8 字节 cell 方案一致。
如果未来证明 ASCII 内存是瓶颈，可以增加 row-level ASCII compression，而不是把整个模型
改成 UTF-16。

### 8.4 转换成本

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

## 9. 正确性和边界条件

### 9.1 非 BMP

- 内部 cell 存 21 位标量，保留完整码点；
- UTF-16 边界输出一对 surrogate；
- `CHAR_INFO` 读取只能写替代字符，这是 API 限制，必须在文档/测试中固定；
- `WriteConsoleW`/宽字符串路径合并合法 surrogate pair；`WriteConsoleOutput` 的
  `CHAR_INFO` 路径按 conhost 保持每格一个 `WCHAR`，不跨格合并；
- 孤立 surrogate 在进入内部标量模型前统一替换为 `U+FFFD`。

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
- 无头输出 UTF-8 不能经过 UTF-16 中间层；
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
   移动/复制 cluster entry，并更新 `cluster_id`；需要严格的完整性检查。
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

> 使用 Unicode scalar 作为语义单位是正确的；使用**整行 UTF-32 字符串 + 列偏移表**作为
> 容器是低效的；把规范表示改成 UTF-16 则会把 Windows API 的编码限制带入内部模型。

推荐的设计是：

- **Cell 固定 8 字节**：21 位 primary codepoint、16 位 page-local style ID、16 位
  row-local cluster ID、content/width/flags 位；
- **行级 grapheme side store**：只保存多码点 cluster 的额外内容；
- **page + row + dense cell**：支持主/备用缓冲区和未来 scrollback；
- **style table**：内部保存完整的颜色/属性/超链接，legacy `WORD` 只在 Console API 投影；
- **UTF-16 只做边界编码**：`CHAR_INFO` 和 DWrite 各自使用明确的转换/映射层；
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
cell + style ID + grapheme map；两者都把 UTF-16 留给平台 shaper，而不是作为终端网格的
规范存储。这与本报告的推荐一致，也说明“为了 Console API/DWrite 的 UTF-16 而改成
UTF-16 屏幕缓冲区”并不是高质量终端的必要选择。

## 13. 建议的 C++ cell 类型定义

以下代码以 **C++20** 为基线，使用 `std::span`/`std::optional`/`std::vector` 等标准库类型；
如果项目已经使用 C++23，可以把相应的 enum cast 替换为 `std::to_underlying`。这是**接口/布局
设计**，不是完整实现。目标是给出可以直接落地的现代 C++ 形状：

- `cell` 固定 8 字节、trivially copyable、standard-layout；
- 不包含 `std::string`、`std::variant` 或任何每 cell 分配；
- cluster 只保存 16 位 `cluster_id`，完整额外 codepoint 放在 row 的 side store；
- style 只保存 16 位 page-local `style_id`；
- 默认构造的零值就是合法空白 cell；
- `std::span`/`std::optional`/`std::vector` 作为边界和容器类型。

```cpp
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <type_traits>
#include <vector>

namespace corehost::screen
{

using style_id = std::uint16_t;
using cluster_id = std::uint16_t;

enum class cell_content : std::uint8_t
{
    blank = 0,
    scalar = 1,
    grapheme = 2,
    background = 3,
};

enum class cell_width : std::uint8_t
{
    narrow = 0,
    wide = 1,
    tail = 2,
    spacer_head = 3,
};

enum class cell_flag : std::uint8_t
{
    none = 0,
    protected_cell = 1u << 0,
    hyperlink = 1u << 1,
    semantic_input = 1u << 2,
    semantic_prompt = 1u << 3,
};

class cell final
{
public:
    using flag = cell_flag;
    static constexpr cluster_id no_cluster = 0;

    constexpr cell() noexcept = default;

    [[nodiscard]] static constexpr cell blank(style_id style = 0) noexcept
    {
        return make(U' ', style, cell_content::blank, cell_width::narrow, no_cluster, 0);
    }

    [[nodiscard]] static constexpr cell scalar(char32_t codepoint,
                                              style_id style = 0,
                                              cell_width width = cell_width::narrow) noexcept
    {
        return make(codepoint, style, cell_content::scalar, width, no_cluster, 0);
    }

    [[nodiscard]] static constexpr cell grapheme(char32_t primary,
                                                 cluster_id cluster,
                                                 style_id style = 0,
                                                 cell_width width = cell_width::narrow) noexcept
    {
        return make(primary, style, cell_content::grapheme, width, cluster, 0);
    }

    [[nodiscard]] static constexpr cell background(style_id style = 0) noexcept
    {
        return make(U' ', style, cell_content::background, cell_width::narrow, no_cluster, 0);
    }

    [[nodiscard]] static constexpr cell tail(style_id style = 0) noexcept
    {
        return make(U' ', style, cell_content::blank, cell_width::tail, no_cluster, 0);
    }

    [[nodiscard]] static constexpr cell spacer_head(style_id style = 0) noexcept
    {
        return make(U' ', style, cell_content::blank, cell_width::spacer_head, no_cluster, 0);
    }

    [[nodiscard]] constexpr char32_t codepoint() const noexcept
    {
        return static_cast<char32_t>(_bits & codepoint_mask);
    }

    [[nodiscard]] constexpr char32_t text_codepoint() const noexcept
    {
        return is_blank() ? U' ' : codepoint();
    }

    [[nodiscard]] constexpr style_id style() const noexcept
    {
        return static_cast<style_id>((_bits >> style_shift) & style_mask);
    }

    [[nodiscard]] constexpr cluster_id cluster() const noexcept
    {
        return static_cast<cluster_id>((_bits >> cluster_shift) & cluster_mask);
    }

    [[nodiscard]] constexpr cell_content content() const noexcept
    {
        return static_cast<cell_content>((_bits >> content_shift) & content_mask);
    }

    [[nodiscard]] constexpr cell_width width() const noexcept
    {
        return static_cast<cell_width>((_bits >> width_shift) & width_mask);
    }

    [[nodiscard]] constexpr std::uint8_t flags() const noexcept
    {
        return static_cast<std::uint8_t>((_bits >> flags_shift) & flags_mask);
    }

    [[nodiscard]] constexpr bool has(flag value) const noexcept
    {
        return (flags() & static_cast<std::uint8_t>(value)) != 0;
    }

    [[nodiscard]] constexpr bool is_blank() const noexcept
    {
        return content() == cell_content::blank || content() == cell_content::background;
    }

    [[nodiscard]] constexpr bool is_grapheme() const noexcept
    {
        return content() == cell_content::grapheme;
    }

    [[nodiscard]] constexpr bool is_wide() const noexcept
    {
        return width() == cell_width::wide;
    }

    [[nodiscard]] constexpr bool is_continuation() const noexcept
    {
        return width() == cell_width::tail;
    }

    [[nodiscard]] constexpr bool is_spacer_head() const noexcept
    {
        return width() == cell_width::spacer_head;
    }

    [[nodiscard]] constexpr cell with_style(style_id value) const noexcept
    {
        auto result = *this;
        result._bits = (result._bits & ~style_bits) |
                       (static_cast<std::uint64_t>(value) << style_shift);
        return result;
    }

    [[nodiscard]] constexpr cell with_flag(flag value, bool enabled = true) const noexcept
    {
        auto result = *this;
        const auto bit = static_cast<std::uint8_t>(value);
        const auto old = result.flags();
        const auto next = enabled
                              ? static_cast<std::uint8_t>(old | bit)
                              : static_cast<std::uint8_t>(old & static_cast<std::uint8_t>(~bit));
        result._bits = (result._bits & ~flags_bits) |
                       (static_cast<std::uint64_t>(next) << flags_shift);
        return result;
    }

    friend constexpr bool operator==(const cell &, const cell &) noexcept = default;

private:
    // 64-bit layout:
    //   [0, 21)   codepoint
    //   [21, 37)  style_id
    //   [37, 53)  cluster_id
    //   [53, 55)  content
    //   [55, 57)  width
    //   [57, 64)  flags
    static constexpr unsigned codepoint_shift = 0;
    static constexpr unsigned style_shift = 21;
    static constexpr unsigned cluster_shift = 37;
    static constexpr unsigned content_shift = 53;
    static constexpr unsigned width_shift = 55;
    static constexpr unsigned flags_shift = 57;

    static constexpr std::uint64_t codepoint_mask = 0x1FFFFF;
    static constexpr std::uint64_t style_mask = 0xFFFF;
    static constexpr std::uint64_t cluster_mask = 0xFFFF;
    static constexpr std::uint64_t content_mask = 0x3;
    static constexpr std::uint64_t width_mask = 0x3;
    static constexpr std::uint64_t flags_mask = 0x7F;

    static constexpr std::uint64_t style_bits = style_mask << style_shift;
    static constexpr std::uint64_t flags_bits = flags_mask << flags_shift;

    [[nodiscard]] static constexpr cell make(char32_t codepoint,
                                             style_id style,
                                             cell_content content,
                                             cell_width width,
                                             cluster_id cluster,
                                             std::uint8_t flags) noexcept
    {
        cell result;
        result._bits = (static_cast<std::uint64_t>(codepoint) & codepoint_mask) |
                       (static_cast<std::uint64_t>(style) << style_shift) |
                       (static_cast<std::uint64_t>(cluster) << cluster_shift) |
                       ((static_cast<std::uint64_t>(content) & content_mask) << content_shift) |
                       ((static_cast<std::uint64_t>(width) & width_mask) << width_shift) |
                       ((static_cast<std::uint64_t>(flags) & flags_mask) << flags_shift);
        return result;
    }

    std::uint64_t _bits{};
};

static_assert(sizeof(cell) == sizeof(std::uint64_t));
static_assert(alignof(cell) == alignof(std::uint64_t));
static_assert(std::is_trivially_copyable_v<cell>);
static_assert(std::is_standard_layout_v<cell>);

// A row-local side store for the extra codepoints of grapheme clusters.
// cluster_id == 0 means "no cluster"; IDs are never reused within a row.
class grapheme_store final
{
public:
    static constexpr std::size_t max_cluster_codepoints = 64;

    [[nodiscard]] std::optional<cluster_id> append(std::span<const char32_t> extra)
    {
        if (extra.empty())
        {
            return cluster_id{0};
        }

        if (extra.size() + 1 > max_cluster_codepoints ||
            entries_.size() >= static_cast<std::size_t>(std::numeric_limits<cluster_id>::max()))
        {
            return std::nullopt;
        }

        const auto offset = static_cast<std::uint32_t>(codepoints_.size());
        codepoints_.insert(codepoints_.end(), extra.begin(), extra.end());

        const auto id = static_cast<cluster_id>(entries_.size());
        entries_.push_back({offset, static_cast<std::uint16_t>(extra.size())});
        return id;
    }

    [[nodiscard]] std::span<const char32_t> extras(cluster_id id) const noexcept
    {
        if (id == 0 || static_cast<std::size_t>(id) >= entries_.size())
        {
            return {};
        }

        const auto &entry = entries_[id];
        return std::span<const char32_t>{codepoints_}.subspan(entry.offset, entry.length);
    }

    void clear() noexcept
    {
        codepoints_.clear();
        entries_.resize(1);
    }

private:
    struct entry final
    {
        std::uint32_t offset{};
        std::uint16_t length{};
    };

    std::vector<char32_t> codepoints_;
    std::vector<entry> entries_{entry{}};
};

} // namespace corehost::screen
```

### 13.1 与 cell 配套的 style 和 page

cell 不能孤立存在：`style_id` 需要 page-local style table，`cluster_id` 需要 row-local
`grapheme_store`。推荐的配套类型如下。

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

### 13.2 设计说明

1. **cell 是值类型，不是文本对象**
   - `cell` 只保存 primary codepoint、样式 ID、cluster ID 和列语义；
   - 不保存 `std::wstring`/`std::u32string`，没有每 cell 堆分配；
   - `sizeof(cell) == 8`，可直接 `memset`、`memcpy`、按 page 批量清零。

2. **cluster_id 是 row-local 索引**
   - `cell::cluster()` 返回 `cluster_id`，0 表示没有额外 codepoint；
   - `grapheme_store::extras()` 返回额外 codepoint 的 `std::span`；
   - 这样 cell 不携带 pointer，row 移动时可以连同 `grapheme_store` 一起移动；
   - 跨 row 复制时重新 append cluster，不能直接复制 cluster ID。

3. **style_id 是 page-local 索引**
   - 默认样式 ID 0 不查表；
   - style table 保存完整颜色、下划线、超链接标志和 legacy 投影；
   - 跨 page 复制 cell 时重新映射 style ID。

4. **宽字符和 continuation**
   - head cell 使用 `cell_width::wide`；
   - 第二列使用 `cell::tail()`，内容为空，不保存独立文本；
   - 行尾放不下时使用 `cell_width::tail` 或按终端规则换行；
   - DWrite 的 glyph advance 不参与列宽计算。

5. **空白 cell**
   - 默认构造的 `cell{}` 是 blank，所有位为 0；
   - `cell::blank(style)` 显式保存空白字符和样式；
   - `cell::background(style)` 表示无文字、仅背景；
   - page 初始化可以直接用 `std::vector<cell>(n)` 或 `memset`。

6. **非法输入**
   - `cell` 假定 `codepoint <= 0x10FFFF` 且不是 surrogate；
   - UTF-16、UTF-8 输入在构造 cell 之前完成解码、代理对合并、U+FFFD 替换和 grapheme
     分割；
   - `grapheme_store::append` 只接收合法的额外 codepoint；超过 64 个 codepoint 的 cluster
     必须按项目策略拒绝、截断或替换，不能拆成多个宽 cell 破坏列对齐。

7. **Console API 边界**
   - `cell` 不直接暴露 `CHAR_INFO`；
   - 读取投影按 conhost 规则：cell 的完整文本只有一个 UTF-16 unit 时返回该 unit，否则
     返回 `U+FFFD`；
   - `WriteConsoleW` 可以构造完整 grapheme cell；`WriteConsoleOutput` 的 `CHAR_INFO`
     路径仍是一格一个 `WCHAR`；
   - 窗口 DWrite 和无头 UTF-8 直接读取 cell/row，不经过 `CHAR_INFO`。

8. **性能边界**
   - 热路径只访问 `std::span<cell>`；
   - 复杂 row 才需要 `std::optional<grapheme_store>`；
   - style table 和 cluster store 都是 page/row 生命周期，不进入 cell 热数据；
   - page 的 dense cell 数组可以连续遍历，适合 Console API 矩形和 DWrite run 构建。

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
