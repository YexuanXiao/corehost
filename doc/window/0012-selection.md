# Selection / Copy / Mouse Reporting

## Selection

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

- 复制：`Ctrl+Shift+C`；
- 粘贴：`Ctrl+Shift+V`；
- 纯文本 `CF_UNICODETEXT`；
- 粘贴时 `LF`/`CRLF` 统一为 `CR`；
- 输入后清空选择。

## Mouse reporting

- 应用启用 mouse tracking 时，鼠标事件优先转发；
- Shift + 拖动强制本地选择；
- 左键、右键、中键、移动、滚轮都会生成 console mouse input event；
- 本地选择优先于移动事件，不会同时转发。

## 当前限制

- 选择坐标使用 virtual row，可跨 history + active page；
- 尚未实现拖动到窗口边缘时自动滚动；
- VT SGR mouse 输入编码仍依赖应用读取 console input event 的路径；
- 未实现双击选词的自定义词边界配置。
