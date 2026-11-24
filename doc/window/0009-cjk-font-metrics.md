# CJK / Fallback Font Metrics

当前状态：搁置修改，记录 Ghostty 与 Windows Terminal 的做法。

## Ghostty

依据：

- `ghostty/src/font/Metrics.zig`：`FaceMetrics.ic_width` 是字体中 `水`（U+6C34）的 advance；`icWidth()` 缺省估计为 `min(asciiHeight, 2 * cell_width)`。
- `ghostty/src/font/Collection.zig`：`SizeAdjustment` 默认使用 `ic_width`；`scaleFactor()` 把 fallback face 的 `icWidth()` 归一化到 primary face 的 `icWidth()`，得到 fallback 的 font size scale。
- `ghostty/src/font/Glyph.zig`：另有 stretch/align constraint，但默认 fallback 调整是字体大小归一化，不是强制 glyph advance。

效果：

- CJK fallback 字体按 `primary.icWidth / fallback.icWidth` 放大/缩小；
- CJK glyph 自然填满约 2 个 primary cell；
- 保持网格，不水平拉伸 glyph。

优点：

- 视觉上与 primary 字体大小协调；
- 不改变 Latin cell 宽度；
- 对 CJK、全角标点、部分 emoji fallback 都更自然。

代价：

- 需要知道每个 fallback run 的字体和 `ic_width`；
- 每个 fallback run 需要独立 font size；
- 会改变 fallback 的 line height/基线，需要额外对齐；
- DWrite 的 `MapCharacters` 通常不返回非 1 scale，需要自己测量 fallback face。

## Windows Terminal AtlasEngine

依据：

- `terminal/src/renderer/atlas/AtlasEngine.api.cpp`：primary cell width 由字符 `0` 的 advance 计算；fallback 通过 `IDWriteFontFallback`。
- `AtlasEngine.cpp::_mapCharacters()`：`MapCharacters` 返回的 `scale` 被 `assert(scale == 1)`，说明 WT 不依赖 fallback size 调整。
- `AtlasEngine.cpp` simple text：`glyphAdvance = (col2 - col1) * cellSize.x`，直接强制成所需列宽。
- `AtlasEngine.cpp` complex text：累计实际 advance，并把差值补到最后一个 glyph 的 advance。
- glyph 本身不缩放；只调整 shaping advance。

效果：

- CJK  glyph 保持 fallback 字体原始大小；
- advance 被强制成 2 个 cell；
- 如果 fallback glyph 本身窄于 2 cells，视觉上仍可能出现空隙。

优点：

- 实现简单，不改变 glyph 尺寸；
- 所有列对齐网格；
- 适合 WT 现有的 per-line shaping/atlas 架构。

代价：

- 不保证 CJK 视觉填满 2 cells；
- 结果依赖 fallback 字体度量；
- 复杂 cluster 需要 advance 差值修正。

## corehost 当前

- `text_engine` 用 primary font 的 `M` advance 作为 cell_width；
- CJK 作为 grapheme 送入同一个 `IDWriteTextLayout`；
- glyph 以原始大小绘制在 cell 起点；
- 没有 fallback font size adjustment，也没有强制 advance。

因此 CJK 在 2 cells 中会出现可见空隙。

## 待决定

用户要求先记录，暂不修改。可选方向：

1. Ghostty 式：按 fallback `ic_width` 调整 font size；
2. WT 式：只强制 glyph advance；
3. 缩放 glyph quad 到 2 cells；
4. 换 CJK fallback 字体。
