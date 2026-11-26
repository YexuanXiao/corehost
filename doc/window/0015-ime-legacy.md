# TSF 下保留 IMM32 消息（Chromium 参考）

## Chromium 源码结论

参考：

- `chromium_ime_src/ui/base/ime/win/input_method_win_base.cc`
- `chromium_ime_src/ui/base/ime/win/input_method_win_tsf.cc`

### 1. TSF 不是“只处理 TSF 消息”

`InputMethodWinTSF::OnUntranslatedIMEMessage` 在 TSF 已启用时仍处理：

```text
WM_IME_REQUEST
WM_CHAR
WM_SYSCHAR
```

原因：

- 部分 TSF-native TIP（如 ATOK、Mozc）仍依赖 `WM_IME_REQUEST` 做 reverse conversion；
- TIP 未激活或当前语言为英文时，按键最终变成 `WM_CHAR`；
- TSF TIP 不消费的 `WM_KEYDOWN` 经 `TranslateMessage` 后也是 `WM_CHAR`。

### 2. WM_IME_REQUEST 的三类请求

`InputMethodWinBase::OnImeRequest` 处理：

```text
IMR_DOCUMENTFEED
IMR_RECONVERTSTRING
IMR_QUERYCHARPOSITION
```

- `IMR_DOCUMENTFEED`：为 reverse conversion 提供文档上下文；Chromium 会在选区/composition 前后额外取 20 个字符；
- `IMR_RECONVERTSTRING`：返回 `RECONVERTSTRING`，描述字符串、composition 和 target 的偏移/长度；
- `IMR_QUERYCHARPOSITION`：返回 `IMECHARPOSITION`，提供候选窗/IME UI 需要的字符屏幕位置、行高和文档区域。

`RECONVERTSTRING` 的 `dwCompStrOffset` / `dwTargetStrOffset` 是**相对于字符串起点**的字节偏移，不是相对于结构体。

## 本项目实现

`window_host` 增加：

```text
WM_IME_REQUEST
  IMR_RECONVERTSTRING
  IMR_DOCUMENTFEED
  IMR_QUERYCHARPOSITION
```

文本来源优先级：

1. TSF composition（`console_.ime_composition()`）；
2. 当前选择；
3. 光标所在 console 行。

`IMR_QUERYCHARPOSITION` 使用 TSF caret 的屏幕矩形；`rcDocument` 使用 client 的屏幕矩形。

同时把 `WM_SYSCHAR` 与 `WM_CHAR` 统一到同一条文本输入路径，和 Chromium 的 `OnChar` 对齐。

### 注释

代码注释直接标注了 Chromium 参考文件和原因：

```cpp
// Chromium's InputMethodWinBase deliberately keeps handling these IMM32
// requests even when TSF is the active input path...
```

## 与 Chromium 的差异

- 终端没有完整文档模型，`IMR_DOCUMENTFEED` 只提供当前行/选择/composition，不做前后 20 字符的文档扩展；
- `IMR_QUERYCHARPOSITION` 目前不根据 `dwCharPos` 精确定位到 composition 内第 N 个字符，只返回 caret 起点；
- 没有实现 Chromium 的 `ITextStoreACP`；TSF composition 由 `libime/tsf_host.hpp` 的透明上下文处理；
- 输入作用域（InputScope）未实现。

## 验证

```text
100% tests passed, 0 tests failed out of 12
```
