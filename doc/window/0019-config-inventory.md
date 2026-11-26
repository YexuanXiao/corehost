# Window Mode 配置项清单

目的：把散落在 `window_host`、`render_worker`、`console`、`tsf_host` 中的策略常量
收敛到 `libconfig` 的配置对象。

当前状态：滚动条上限、启动/最小 grid、候选偏移、hyperlink modifier 已接入；
配置文件加载仍未实现。

## 已接入的默认值

所有默认值在各自配置头文件中用 `inline constexpr` 定义，不散落在实现里。

### console_config.hpp

```cpp
inline constexpr std::size_t default_scrollback_lines = 3000;

struct console_config final
{
    std::size_t scrollback_lines{default_scrollback_lines};
};
```

### window_config.hpp

```cpp
inline constexpr std::size_t default_initial_columns = 120;
inline constexpr std::size_t default_initial_rows = 30;
inline constexpr std::size_t default_min_columns = 36;
inline constexpr std::size_t default_min_rows = 2;
inline constexpr std::int32_t default_candidate_offset_cells = 2;
inline constexpr hyperlink_modifier default_hyperlink_modifier = hyperlink_modifier::control;
inline constexpr bell_mode default_bell_mode = bell_mode::visual;
inline constexpr backdrop_mode default_backdrop_mode = backdrop_mode::none;
inline constexpr std::uint8_t default_opacity = 255;
inline constexpr bool default_dark_mode = true;
inline constexpr client_exit_policy default_exit_policy = client_exit_policy::always_exit;
```

```cpp
struct window_config final
{
    console_palette palette{default_palette};
    bell_mode bell{default_bell_mode};
    uitext::text_options text_options{};
    backdrop_mode backdrop{default_backdrop_mode};
    std::uint8_t opacity{default_opacity};
    bool dark_mode{default_dark_mode};
    client_exit_policy exit_policy{default_exit_policy};
    std::int32_t candidate_offset_cells{default_candidate_offset_cells};
    std::size_t initial_columns{default_initial_columns};
    std::size_t initial_rows{default_initial_rows};
    std::size_t min_columns{default_min_columns};
    std::size_t min_rows{default_min_rows};
    hyperlink_modifier hyperlink{default_hyperlink_modifier};
};
```

### 文字默认值

`text_options.hpp`：

```cpp
inline constexpr font_family default_font_family = font_family::cascadia_mono;
inline constexpr bool default_contrast = true;
inline constexpr bool default_alpha_correction = true;
```

## 已确认行为

| 配置 | 归属 | 默认 | 说明 |
|---|---|---:|---|
| `scrollback_lines` | `console_config` | 3000 | window 模式传入 console 构造 |
| `initial_columns/rows` | `window_config` | 120x30 | 启动 console 和初始窗口共用 |
| `min_columns/min_rows` | `window_config` | 36x2 | 按字体 cell 度量和窗口边框计算像素 |
| `candidate_offset_cells` | `window_config` | 2 | TSF `GetTextExt` 按 cell 计算 |
| `hyperlink` | `window_config` | control | Ctrl/Alt/Shift/none 可配置 |
| `bell` | `window_config` | visual | 终端窗口专属 |
| `palette` | `window_config` | Campbell | 当前默认调色板 |
| `backdrop/opacity/dark_mode` | `window_config` | none/255/true | DWM 外观 |
| `exit_policy` | `window_config` | always_exit | client 退出行为 |
| `word_separators` | 不配置 | 现有 word_segmenter | 用户确认使用现有默认 |
| 滚动键语义 | 不配置 | 现有行为 | 用户确认不改 |
| Sixel 最大尺寸 | 不配置 | 不设限 | 不增加宽高/像素上限 |
| Sixel 纹理缓存 | 不配置 | 仅屏幕内 | 内容 generation 不变不重传；屏幕外释放 |

## frame_latency / vsync 说明

这两个是 DXGI 呈现策略，不是终端语义：

- `frame_latency`：swapchain 允许排队的最大帧数。值越小，输入到显示延迟越低，
  但更容易受 GPU 抖动影响；值越大吞吐更平滑但延迟更高。当前 renderer 没有设置
  `SetMaximumFrameLatency`，使用 DXGI 默认。
- `vsync`：`Present` 的 sync interval。当前固定：
  ```cpp
  swapchain_->Present(1, 0);
  ```
  即开启 vsync，按显示器刷新率节流；若设为 0 可降低延迟/允许 tearing，但会改变
  呈现节奏和功耗。

是否暴露给用户需要产品决定。暂未加入 `window_config`，避免在语义未定时增加 API。

## 字体 fallback 列表

当前：

- `uitext::text_options.font` 只能选 `cascadia_mono` / `cascadia_code`；
- 实际 fallback 顺序由 `uitext::default_font_families(font)` 固定返回；
- 字体大小硬编码为 16 DIP（`window_host::font_size_dip` 和 renderer 初始化）；
- 字体列表不能由 `window_config` 覆盖。

可配置候选：

| 候选 | 类型 | 当前值 | 说明 |
|---|---|---|---|
| `font_size_dip` | `float` | 16.0 | 需要同时传给 window 度量和 renderer |
| `font_fallbacks` | 有序 family 列表 | 固定数组 | 需要定义“显式 fallback + 系统 fallback”的边界 |
| `font_family` | 自定义字符串 | 枚举 | 需要 DirectWrite family 名称和缺失处理 |
| `font_weight` / `italic` | 字体属性 | 默认 | 需要重新设计 text_engine 输入 |
| `contrast` | bool | true | 已在 text_options |
| `alpha_correction` | bool | true | 已在 text_options |

这些需要用户确定默认值和 API 形态后再接入；不要在 renderer 内偷偷读取全局配置。

## 其他可配置行为（候选）

| 行为 | 当前值 | 归属 | 备注 |
|---|---:|---|---|
| cursor style | profile default | console/window | block/underline/bar |
| cursor blink | 530ms | console/window | 0 表示不闪 |
| scrollbar width | `scrollbar::default_width` | window | 影响 grid 计算 |
| scrollbar auto hide | true | window | 失焦隐藏 |
| scrollbar animation frames | `expand_frames` | window | 与 DComp 时钟配合 |
| selection auto-scroll interval | 40ms | window | 拖动边缘 |
| selection auto-scroll edge | 1 row | window | 像素阈值 |
| copy line ending | CRLF | console | 剪贴板 |
| hyperlink modifier | control | window | 已接入 |
| UIA text/selection events | true | window | 需要产品开关 |
| UIA ScrollPattern | true | window | 需要产品开关 |
| tsf message pump | false | window/ime | 当前禁用，需硬件验收 |
| Sixel visible texture capacity | 1024 | renderer | 内部实现，不暴露 |
| frame latency | DXGI default | window | 见上文，未决定 |
| vsync | true | window | 见上文，未决定 |

## 设计规则

1. 配置对象只在 `window_entry` 构造一次，按 const 引用传递；
2. 默认值必须定义在 `console_config.hpp` / `window_config.hpp`（或对应
   `text_options.hpp`）的 `constexpr` 常量中；
3. 实现中不新增散落的魔法常量；
4. 配置文件加载不属于本轮；
5. 仍未决定的默认值先询问用户，不擅自选择。

## 仍未确认

- `frame_latency` / `vsync` 是否公开，以及默认值；
- `font_size_dip`、`font_fallbacks`、自定义 family 的 API；
- cursor 样式/闪烁、scrollbar 尺寸、selection 参数是否公开；
- `tsf_message_pump` 是否作为用户配置。
