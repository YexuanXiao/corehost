# Window Mode 配置项清单

目的：把散落在 `window_host`、`render_worker`、`console`、`tsf_host` 中的策略常量
收敛到 `libconfig` 的配置对象。

当前状态：滚动条上限、启动/最小 grid、候选偏移、hyperlink modifier、字号和字体 fallback 列表已接入；
配置文件加载仍未实现。

## 完整 schema

新增 `libconfig/config.hpp`（文档路径以仓库根为准），提供
`corehost_config`/`config`，按 `配置.txt` 合并现有对象：

- `console` / `window`：现有兼容对象；
- `color`、`background`、`cursor`、`font`、`text`；
- `window_behavior`、`mouse`、`clipboard`、`hyperlink`、`title`；
- `scrollback`、`bell`、`notification`、`progress`；
- `term`、`graphics`、`render`、`ime`、`accessibility`。

“先不实现”的字段也已经建入 schema，但不会改变当前运行行为；后续按行为逐项接入。

`Config.Defaults` 自动验证 schema 的默认值和颜色/字体契约。

完整 schema 已由 `window_entry` 接收；当前行为读取 `config.console` 和 `config.window` 兼容子集。

默认颜色：

- dark：Campbell；
- light：One Half Light，indexed white 改为 `B3B3B3`，bright white 改为 `DBDBDB`；
- 主题模式默认 `auto_mode`。

默认字体：只包含 `Cascadia Mono` 一项，其他字符由 DirectWrite 系统 fallback 处理。

## 已接入的默认值

所有默认值在各自配置头文件中用 `inline constexpr` 定义，不散落在实现里。

### console_config.hpp

```cpp
enum class copy_line_ending : unsigned char
{
    crlf,
    lf,
};

inline constexpr std::size_t default_scrollback_lines = 3000;
inline constexpr copy_line_ending default_copy_line_ending = copy_line_ending::crlf;

struct console_config final
{
    std::size_t scrollback_lines{default_scrollback_lines};
    copy_line_ending copy_ending{default_copy_line_ending};
};
```

### window_config.hpp

```cpp
inline constexpr std::size_t default_initial_columns = 120;
inline constexpr std::size_t default_initial_rows = 30;
inline constexpr std::size_t default_min_columns = 36;
inline constexpr std::size_t default_min_rows = 2;
inline constexpr float default_font_size_dip = 16.0f;
inline constexpr std::uint32_t default_cursor_blink_ms = 530;
inline constexpr std::uint32_t default_selection_auto_scroll_interval_ms = 40;
inline constexpr std::size_t default_selection_auto_scroll_edge_rows = 1;
inline constexpr bool default_uia_text_changed_events = true;
inline constexpr bool default_uia_selection_events = true;
inline constexpr std::size_t default_mouse_wheel_lines = 3;
inline constexpr right_click_action default_right_click_action = right_click_action::automatic;
inline constexpr bool default_uia_enabled = true;
inline constexpr bool default_tsf_enabled = true;
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
    float font_size_dip{default_font_size_dip};
    font_fallback_list font_fallbacks{default_font_fallbacks(text_options.font)};
    std::uint32_t cursor_blink_ms{default_cursor_blink_ms};
    std::uint32_t selection_auto_scroll_interval_ms{default_selection_auto_scroll_interval_ms};
    std::size_t selection_auto_scroll_edge_rows{default_selection_auto_scroll_edge_rows};
    bool uia_text_changed_events{default_uia_text_changed_events};
    bool uia_selection_events{default_uia_selection_events};
    bool uia_enabled{default_uia_enabled};
    bool tsf_enabled{default_tsf_enabled};
    std::size_t mouse_wheel_lines{default_mouse_wheel_lines};
    right_click_action right_click{default_right_click_action};
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
| `font_size_dip` | `window_config` | 16.0 | 同时用于窗口度量和 renderer |
| `font_fallbacks` | `window_config` | 1 项 Cascadia Mono | 最多 12 项，owning wstring + Unicode range |
| `cursor_blink_ms` | `window_config` | 530 | 0 表示不闪 |
| `selection_auto_scroll_interval_ms` | `window_config` | 40 | 边缘自动滚动 |
| `selection_auto_scroll_edge_rows` | `window_config` | 1 | 边缘行数 |
| `uia_text_changed_events` | `window_config` | true | 文本变化事件开关 |
| `uia_selection_events` | `window_config` | true | 选择变化事件开关 |
| `uia_enabled` | `window_config` | true | UIA provider 总开关 |
| `tsf_enabled` | `window_config` | true | TSF 总开关 |
| `mouse_wheel_lines` | `window_config` | 3 | 滚轮行数 |
| `right_click` | `window_config` | automatic | automatic/copy/paste/menu/none |
| `cursor_shape` | `window_config` | automatic | 覆盖 VT/应用；automatic 保留应用控制；double/hollow/vintage 当前近似映射 |
| `scrollbar` | `window_config` | auto_hide | always/auto_hide |
| `copy_ending` | `console_config` | CRLF | 复制行尾 CRLF/LF |
| `color` | `corehost_config.color` | dark/Campbell | 调色板、cursor、selection 颜色已接入；light 已定义 |\n| `background.opacity` | `corehost_config.background` | 1.0 | 已接入 layered opacity |\n| `window.shadow/always_on_top/launch/allow_move_resize` | `corehost_config.window_behavior` | true/false/automatic/true | shadow、topmost、maximized、禁止移动 |
| `title.force_corehost` | `corehost_config.title` | false | 强制窗口标题 |\n| `accessibility.reduce_motion` | `corehost_config.accessibility` | false | 禁用光标/滚动条动画 |
| `clipboard.read_policy` | `corehost_config.clipboard` | deny | 粘贴读取策略，已接入 |
| `clipboard.write_policy` | `corehost_config.clipboard` | deny | 复制写入策略，已接入 |
| `clipboard.selection_policy` | `corehost_config.clipboard` | deny | PRIMARY selection，占位 |
| `bell` | `window_config` | visual | `off` 不闪；`visual` 用 `FlashWindowEx`，已实际接入 |
| `palette` | `window_config` | Campbell | 当前默认调色板 |
| `backdrop/opacity/dark_mode` | `window_config` | none/255/true | DWM 外观 |
| `exit_policy` | `window_config` | always_exit | client 退出行为 |
| `word_separators` | 不配置 | 现有 word_segmenter | 用户确认使用现有默认 |
| 滚动键语义 | 不配置 | 现有行为 | 用户确认不改 |
| Sixel 最大尺寸 | 不配置 | 不设限 | 不增加宽高/像素上限 |
| Sixel 纹理缓存 | 不配置 | 仅屏幕内 | 内容 generation 不变不重传；屏幕外释放 |

## TSF message pump

不需要，也不作为配置项。corehostw 使用标准 Win32 消息循环，TSF hidden window 消息由
`GetMessageW`/`DispatchMessageW` 分发；`ITfMessagePump` 只用于没有标准消息循环或需要
在模态循环中 pump 的宿主。只有在未来引入模态 TSF UI 或自建消息循环时才重新评估。

## frame_latency / vsync 说明

这两个是 DXGI 呈现策略，不是终端语义：

- `frame_latency`：swapchain 允许排队的最大帧数。值越小，输入到显示延迟越低，
  但更容易受 GPU 抖动影响；值越大吞吐更平滑但延迟更高。当前 renderer 没有设置
  `SetMaximumFrameLatency`，使用 DXGI 默认。
- `vsync`：`Present` 的 sync interval。当前固定：
  ```cpp
  Present1(1, 0, &params);
  ```
  即开启 vsync，按显示器刷新率节流；若设为 0 可降低延迟/允许 tearing，但会改变
  呈现节奏和功耗。

决定：不增加 `SetMaximumFrameLatency`/vsync 配置；使用 `Present1(1, 0, &params)`，sync interval 和 flags 固定 1/0。

## 字体 fallback 列表

已接入：

```cpp
inline constexpr std::size_t max_font_fallbacks = 12;

struct unicode_range final
{
    std::uint32_t first{};
    std::uint32_t last{};
};

struct font_fallback final
{
    std::wstring family;
    unicode_range range{0, 0x10FFFF};
};

struct font_fallback_list final
{
    std::array<font_fallback, max_font_fallbacks> entries{};
    std::size_t count{};
};
```

- `font_fallbacks` 位于 `window_config`；
- 最多 12 项；
- `std::wstring` 由 config 拥有，`text_engine` 只借用 `wcstring_view`；
- `font_size_dip` 同时用于窗口度量和 renderer；
- 默认列表只有一项 Cascadia Mono；后续 fallback 由 DirectWrite 系统 fallback 处理；
- 空 family 不参与 fallback；`count` 至少为 1；
- 系统 fallback 仍由 DirectWrite 追加。

仍待决定：

- 是否允许自定义 family 字符串作为 primary；
- 是否公开 font weight/italic；
- fallback 列表的 API 是直接改数组还是提供增删函数。
## 其他可配置行为（候选）

| 行为 | 当前值 | 归属 | 备注 |
|---|---:|---|---|
| cursor style | profile default | console/window | block/underline/bar |
| scrollbar width | `scrollbar::default_width` | window | 影响 grid 计算 |

| scrollbar animation frames | `expand_frames` | window | 与 DComp 时钟配合 |
| UIA ScrollPattern | true | window | 需要产品开关 |
| Sixel visible texture capacity | 1024 | renderer | 内部实现，不暴露 |
| frame latency | DXGI default | window | 固定不暴露 |
| vsync | true | window | Present1(1,0) 固定 |
## 设计规则

1. 配置对象只在 `window_entry` 构造一次，按 const 引用传递；
2. 默认值必须定义在 `console_config.hpp` / `window_config.hpp`（或对应
   `text_options.hpp`）中；能 constexpr 的用 constexpr，字体 fallback 因为拥有
   `std::wstring` 用同文件的 `default_font_fallbacks()` 构造函数；
3. 实现中不新增散落的魔法常量；
4. 配置文件加载不属于本轮；
5. 仍未决定的默认值先询问用户，不擅自选择。

## 仍未确认

- 自定义字体 family / weight / italic 的 API；
- cursor 样式/闪烁、scrollbar 尺寸、selection 参数是否公开；
- TSF message pump 已决定不需要；不要再加入配置。
