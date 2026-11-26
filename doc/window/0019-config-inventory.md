# Window Mode 配置项清单

目的：把散落在 `window_host`、`render_worker`、`console`、`tsf_host` 中的策略常量
收敛到 `libconfig` 的配置对象。

当前状态：**只做清单和设计，尚未把新字段接入代码**。已确认项不再标 `TBD`。

## 已确认

| 配置 | 归属 | 默认 | 说明 |
|---|---|---:|---|
| `scrollback_lines` | `console_config` | 3000 | 可配置的 scrollback 上限 |
| `bell_mode` | `window_config` | visual | 没有 window 就没有铃，归 window |
| `candidate_offset_cells` | `window_config` | 2 | 按 cell 计算，用于 TSF 候选位置 |
| 滚动键语义 | 不配置 | 当前行为 | Home/End/PgUp/PgDn/方向键保持现状，不纳入配置 |
| Sixel 最大尺寸 | 不配置 | 不设限 | 不增加宽高/像素上限 |
| Sixel 纹理缓存范围 | 不配置 | 仅屏幕内 | 只缓存当前屏幕内可见图像，不缓存屏幕外图像 |

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

## 纳入 console_config

| 字段 | 类型 | 默认 | 当前来源 | 备注 |
|---|---|---:|---|---|
| `scrollback_lines` | `std::size_t` | **3000** | `console` 构造/`history_limit` | 已确认；当前硬编码 1000 |
| `initial_columns` | `std::size_t` | 120 | `window_host::initial_columns` | 初始 grid |
| `initial_rows` | `std::size_t` | 30 | `window_host::initial_rows` | 初始 grid |
| `cursor_style` | enum | profile default | `screen::cursor_state` | block/underline/bar |
| `cursor_blink_ms` | `std::uint32_t` | 530 | `render_worker::blink_interval_ms` | 0 表示不闪 |
| `word_separators` | string | TBD | `select_word`/word_segmenter | 双击选词边界 |
| `copy_line_ending` | enum CRLF/LF | CRLF | `selected_text_into` | 剪贴板格式 |
| `auto_scroll_interval_ms` | `std::uint32_t` | 40 | `window_host` timer | 选择自动滚动 |
| `auto_scroll_edge_px` | `std::uint32_t` | 1 row | `window_host` | 边缘触发阈值 |

## 纳入 window_config

| 字段 | 类型 | 默认 | 当前来源 | 备注 |
|---|---|---:|---|---|
| `bell` | `bell_mode` | visual | `window_config` | 已确认归 window |
| `candidate_offset_cells` | `std::int32_t` | **2** | `tsf_host::GetTextExt` | 已确认；按 cell 计算 |
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
| `frame_latency` | `std::uint32_t` | 1 | `libd3d12ui` | DXGI frame latency |
| `prefer_warp` | `bool` | false | `libd3d12ui` | 测试/兼容 |
| `device_lost_policy` | enum | recreate | 无 | device lost 恢复策略 |
| `vsync` | `bool` | true | `libd3d12ui` | 合成器时钟/呈现 |

## Sixel / 渲染

| 配置 | 默认 | 说明 |
|---|---:|---|
| `sixel_max_width/height` | 不设限 | 已确认不设尺寸上限 |
| `sixel_max_bytes` | 不设限 | 已确认不设像素内存上限 |
| `sixel_cache_bytes` | 仅屏幕内 | 不缓存屏幕外图像；无离屏缓存预算 |
| `sixel_palette_mode` | RGB/HLS | 当前支持 |
| `atlas_size` | 1024 | glyph/color atlas |
| `idle_render_policy` | event | 事件阻塞；失焦或光标不闪烁时 CPU 0，聚焦闪烁允许 530ms timer；动画用合成器时钟 |
| `text_antialias` | grayscale | DirectWrite 设置 |

Sixel 缓存规则：

- 只缓存当前屏幕内容中可见的 Sixel 图像；
- 图像区域未变化时，不重新绘制、不重新上传 RGBA 纹理；
- 缓存键为 image id + pixel generation + source size + placement；
- 图像离开屏幕即从缓存移除；不保留离屏图像。

## UIA

| 字段 | 类型 | 默认 | 备注 |
|---|---|---:|---|
| `uia_enabled` | `bool` | true | 是否注册 provider |
| `uia_text_changed_events` | `bool` | true | 是否发 TextChanged |
| `uia_selection_events` | `bool` | true | 是否发 SelectionChanged |
| `uia_expose_scroll` | `bool` | true | ScrollPattern |

## IME

| 字段 | 类型 | 默认 | 备注 |
|---|---|---:|---|
| `tsf_enabled` | `bool` | true | 只走 TSF |
| `candidate_offset_cells` | `std::int32_t` | 2 | 在 `window_config`，按 cell 计算 |
| `tsf_message_pump` | `bool` | false | 当前禁用，需硬件验收 |
| `ime_composition_style` | enum | underline | 自绘样式 |

## 暂不纳入配置

- 由 Windows/显示器决定的 DPI、颜色空间；
- DirectWrite 内部 fallback 列表；
- console API 的协议行为；
- 滚动键语义（保持当前行为）；
- Sixel 最大尺寸/像素上限；
- 仅用于测试的日志级别。

## 设计规则

1. 配置对象只在 `window_entry` 构造一次，按 const 引用传递；
2. 不在 `render_worker`/`window_host` 内散落新的魔法常量；
3. 默认值必须与当前行为一致，除非用户明确改变；
4. 配置文件加载不属于本轮；先完成对象和代码接入；
5. 仍未决定的默认值标 `TBD`，先询问用户。

## 仍未确认

- `word_separators` 的默认集合；
- `min_columns/min_rows`；
- `hyperlink_modifier` 是否固定 Ctrl；
- `frame_latency`、`vsync` 是否需要暴露给用户。
