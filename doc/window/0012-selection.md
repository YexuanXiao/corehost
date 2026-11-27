# Selection / Copy / Mouse Reporting

## Selection

- `selection_mode::text` 线性文本选择，默认不是矩形块；
- 字符级拖选；
- Shift + 单击扩展选择；
- 双击选词；
- 三击选行；
- 选择坐标按 grapheme cluster 的 leading cell 对齐；
- 宽字符由 leading cell 的 2-cell 高亮覆盖；
- 选择高亮使用主题前景/背景混合色。

### 双击选词

按用户指定的算法：

1. 将目标行的 cell/grapheme 追加到 UTF-32 缓冲区；
2. 记录每个 codepoint 对应的 cell 列；
3. 用 `unicode::word_segmenter` 逐段断词；
4. 丢弃不包含双击 cell 的段；
5. 使用包含目标 cell 的段的起止 cell 作为选择范围。

## Clipboard

- 复制：`Ctrl+Shift+C`；默认 `clipboard.write_policy=deny`，需显式改为 allow；
- 粘贴：`Ctrl+Shift+V`；默认 `clipboard.read_policy=deny`，需显式改为 allow；
- 纯文本 `CF_UNICODETEXT`；
- 粘贴时 `LF`/`CRLF` 统一为 `CR`；
- 输入后清空选择。

## 右键

- 无 VT mouse tracking 时，右键粘贴剪贴板纯文本；
- 有选区时，右键复制选区并清除选择；
- 应用启用 VT mouse tracking（如 vim `set mouse=a`）时，右键转发给应用，不触发本地粘贴；
- 默认 IMM32 输入法路径同样遵循。

## Mouse reporting

- 应用启用 mouse tracking 时，鼠标事件优先转发；
- Shift + 拖动强制本地选择；
- 左键、右键、中键、移动、滚轮都会生成 console mouse input event；
- 本地选择优先于移动事件，不会同时转发。

## 当前限制

- 选择坐标使用 virtual row，可跨 history + active page；
- 拖到窗口顶部/底部边缘时由 `WM_TIMER` 自动滚动；
- 行尾空白不参与高亮/复制，复制按行裁剪并用 CRLF 连接；
- `console_state` 仍保留 block/line 模式，窗口默认使用 text；
- 只有 VT mouse tracking 时左键才转发给应用，普通 shell 保留本地选择；
- 未实现双击选词的自定义词边界配置。
