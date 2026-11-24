# Console Theme / Palette

## 结构

`libcorewindow/console_theme.hpp` 定义：

```cpp
struct console_color
{
    std::uint8_t r, g, b;
};

struct console_palette
{
    std::array<console_color, 16> indexed;
    console_color default_foreground;
    console_color default_background;

    static console_palette campbell() noexcept;
};
```

- `indexed[0..15]` 是 ANSI/console 索引色；
- `rgb` 直接使用 0xRRGGBB；
- `color_kind::default_color` 映射到主题默认前景/背景；
- `negative` 属性交换前景/背景。

默认调色板：

```text
Windows Terminal Campbell
default foreground #CCCCCC
default background #0C0C0C
```

## 接口

`render_worker` 构造函数接收：

```cpp
console_palette palette = console_palette::campbell()
```

调用方可提供自定义调色板，不修改渲染器代码。

## Cell 颜色

- 非默认背景生成背景 quad；
- glyph 使用 cell 的 foreground；
- 光标使用所在 cell 的 foreground；
- 默认背景由 render target clear color 处理，避免每 cell 背景 quad。

## 待扩展

- bold/faint/underline/上划线等属性；
- 宽字符背景宽度；
- 选择反色和搜索高亮；
- 主题切换时重新上传/重绘。
