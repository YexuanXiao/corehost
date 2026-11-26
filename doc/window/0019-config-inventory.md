# Window Mode 配置项清单

目的：把散落在 `window_host`、`render_worker`、`console`、`tsf_host` 中的策略常量
收敛到 `libconfig` 的配置对象。

当前状态：**只做清单和设计，尚未把新字段接入代码**。所有 `TBD` 都是需要用户确认的
默认值，不能擅自实现。

## 现有配置

```cpp
struct console_config
{
};
```

```cpp
struct window_config
{
    console_palette palette{console_palette::campbell()};
    bell_mode bell{bell_mode::visual};
    uitext::text_options text_options{};
    backdrop_mode backdrop{backdrop_mode::none};
    std::uint8_t opacity{255};
    bool dark_mode{true};
    client_exit_policy exit_policy{client_exit_policy::always_exit};
};
```

```cpp
struct text_options
{
    font_family font{font_family::cascadia_mono};
    bool contrast{true};
    bool alpha_correction{true};
};
```

## 建议纳入 console_config

| 字段 | 类型 | 建议默认 | 当前来源 | 备注 |
|---|---|---:|---|---|
| `scrollback_lines` | `std::size_t` | 1000 | `console` 构造/`history_limit` | 需要与内存上限一起设计 |
| `initial_columns` | `std::size_t` | 120 | `window_host::initial_columns` | 初始 grid |
| `initial_rows` | `std::size_t` | 30 | `window_host::initial_rows` | 初始 grid |
| `cursor_style` | enum | profile default | `screen::cursor_state` | block/underline/bar |
| `cursor_blink_ms` | `std::uint32_t` | 530 | `render_worker::blink_interval_ms` | 0 表示不闪 |
| `word_separators` | string | TBD | `select_word`/word_segmenter | 双击选词边界 |
| `copy_line_ending` | enum CRLF/LF | CRLF | `selected_text_into` | 剪贴板格式 |
| `bell_mode` | `bell_mode` | visual | `window_config` | 可移到 console 或 window；需定 owner |
| `auto_scroll_interval_ms` | `std::uint32_t` | 40 | `window_host` timer | 选择自动滚动 |
| `auto_scroll_edge_px` | `std::uint32_t` | 1 row | `window_host` | 边缘触发阈值 |

## 建议纳入 window_config

| 字段 | 类型 | 建议默认 | 当前来源 | 备注 |
|---|---|---:|---|---|
| `initial_client_columns/rows` | `std::size_t` | 120/30 | `initial_client_size` | 和 console 初始尺寸同步 |
| `min_columns/min_rows` | `std::size_t` | TBD | `WM_GETMINMAXINFO` | 最小窗口 |
| `scrollbar_width` | `float` | `scrollbar::default_width` | `libscrollbar` | 滚动条宽度 |
| `scrollbar_auto_hide` | `bool` | true | `libscrollbar`/host | 失焦隐藏 |
| `scrollbar_animation_frames` | `std::uint32_t` | `expand_frames` | `libscrollbar` | 动画帧数 |
| `text_options` | `text_options` | 现有 | `window_config` | 字体/抗锯齿 |
| `palette` | `console_palette` | Campbell | `window_config` | 主题色 |
| `dark_mode` | `bool` | true | `window_config` | 标题栏/主题 |
| `high_contrast` | `bool` | false | 无 | 系统高对比度策略 |
| `backdrop` | `backdrop_mode` | none | `window_config` | Mica/Acrylic |
| `opacity` | `std::uint8_t` | 255 | `window_config` | 0..255 |
| `exit_policy` | `client_exit_policy` | always_exit | `window_config` | client 退出行为 |
| `hyperlink_modifier` | enum | Ctrl | `window_host` | 打开 hyperlink |
| `selection_shift_forces_local` | `bool` | true | `window_host` | Shift 强制本地选择 |
| `scroll_keys` | enum | TBD | `window_host` | Home/End/PgUp/PgDn 语义 |
| `frame_latency` | `std::uint32_t` | 1 | `libd3d12ui` | DXGI frame latency |
| `prefer_warp` | `bool` | false | `libd3d12ui` | 测试/兼容 |
| `device_lost_policy` | enum | recreate | 无 | device lost 恢复策略 |
| `vsync` | `bool` | true | `libd3d12ui` | 合成器时钟/呈现 |

## 建议纳入 sixel/渲染配置

| 字段 | 类型 | 建议默认 | 备注 |
|---|---|---:|---|
| `sixel_max_width/height` | `std::uint32_t` | TBD | 防止恶意大图 |
| `sixel_max_bytes` | `std::size_t` | TBD | 像素内存上限 |
| `sixel_cache_bytes` | `std::size_t` | TBD | 持久图像缓存预算 |
| `sixel_palette_mode` | enum | RGB/HLS | 当前支持 |
| `atlas_size` | `std::uint32_t` | 1024 | glyph/color atlas |
| `idle_render_policy` | enum | event | 事件阻塞，不是合成器轮询 |
| `text_antialias` | enum | grayscale | DirectWrite 设置 |

## 建议纳入 UIA 配置

| 字段 | 类型 | 建议默认 | 备注 |
|---|---|---:|---|
| `uia_enabled` | `bool` | true | 是否注册 provider |
| `uia_text_changed_events` | `bool` | true | 是否发 TextChanged |
| `uia_selection_events` | `bool` | true | 是否发 SelectionChanged |
| `uia_expose_scroll` | `bool` | true | ScrollPattern |

## 建议纳入 IME 配置

| 字段 | 类型 | 建议默认 | 备注 |
|---|---|---:|---|
| `tsf_enabled` | `bool` | true | 只走 TSF |
| `candidate_offset_cells` | `std::int32_t` | 2 | 当前 `GetTextExt` 左移常量，必须配置化 |
| `tsf_message_pump` | `bool` | false | 当前禁用，需硬件验收 |
| `ime_composition_style` | enum | underline | 自绘样式 |

## 暂不纳入配置

- 由 Windows/显示器决定的 DPI、颜色空间；
- DirectWrite 内部 fallback 列表（已有字体配置，完整 fallback 需要单独设计）；
- console API 的协议行为；
- 仅用于测试的日志级别。

## 设计规则

1. 配置对象只在 `window_entry` 构造一次，按 const 引用传递；
2. 不在 `render_worker`/`window_host` 内散落新的魔法常量；
3. 默认值必须与当前行为一致，除非用户明确改变；
4. 配置文件加载不属于本轮；先完成对象和代码接入；
5. 无法确定的默认值标 `TBD`，先询问用户。

## 待确认

- `bell_mode` 应归 console 还是 window；
- `scrollback_lines` 的内存上限；
- `scroll_keys` 是否保留“滚动后才拦截”的当前语义；
- `candidate_offset_cells` 是否应依赖字体/DPI 计算而不是固定 2；
- `sixel_*` 的上限默认值。
