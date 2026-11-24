# Color Emoji

## 结构

普通文本与彩色 Emoji 使用完全分离的两套 atlas / texture / pipeline：

```text
cell_atlas (R8)
  └── text_engine::rasterize
  └── text pipeline

color_cell_atlas (RGBA8)
  └── text_engine::rasterize_color
  └── color text pipeline
```

约束：

- 彩色 atlas 不持久缓存，每帧 `begin_frame()` 重建；
- 每个 DirectWrite color glyph layer 单独生成一个 atlas 区域和一个 quad；
- 彩色 atlas 的 RGB 直接来自 `DWRITE_COLOR_GLYPH_RUN::runColor`，alpha 来自 layer alpha texture；
- 彩色 quad 使用白色 tint；非彩色 fallback layer 使用当前 foreground tint；
- 普通 grapheme cluster 仍走 color atlas 的 fallback 单层路径，保持现有 grapheme cluster 规则。

## DirectWrite

`text_engine::rasterize_color`：

1. `IDWriteTextLayout::Draw` 收集 glyph run；
2. `IDWriteFactory2::TranslateColorGlyphRun` 枚举 color layers；
3. 每层用 `CreateGlyphRunAnalysis` 生成 alpha；
4. 保存 layer 的 `runColor`、alpha scale 和 bounds。

没有 color layer 时回退成单层白色 alpha，因此不会丢失普通 cluster 文本。

## 验证

`test_text_metrics` 检查：

- `U+1F600` 能产生 non-empty color atlas layer；
- 至少一层 `colored == true`。

窗口验证：

```text
EMOJI: 😀 🚀
```

显示为彩色，普通文本 `echo hi` 与 PowerShell 错误输出不回归。

## 未完成

- CJK 字距按 `0009-cjk-font-metrics.md` 先搁置；
- color bitmap fonts（非 COLR/CPAL）尚未单独处理。
