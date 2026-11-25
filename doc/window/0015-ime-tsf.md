# IME / TSF 实现

## 决策

`corehostw` 只走 TSF，不再维护 IMM32 兼容路径。

## 当前实现

- `ITfThreadMgrEx` / `ITfDocumentMgr` / `ITfContext`；
- `ITfContextOwner`、`ITfContextOwnerCompositionSink`、`ITfTextEditSink`；
- `AssociateFocus` + `SetFocus`；
- 通过 `ITfReadOnlyProperty::EnumRanges` 枚举 context ranges；
- 按 `GUID_PROP_COMPOSING` 区分 finalized 与 composing 文本；
- finalized 文本提交到 console input queue，composing 文本用于自绘；
- composition 文本通过 `console::set_ime_composition` 自绘在光标处；
- 先 advise `ITfContextOwner` 再 Push context；
- candidate/composition 位置来自 `ITfContextOwner::GetTextExt`；
- 为满足候选窗视觉位置，`GetTextExt` 的 `left/right` 统一左移：

```text
2 * cell_width（约一个汉字/两个 cell）
```

## 如何判断 composition

1. 微软拼音出现候选窗，说明 TSF composition 已开始；
2. 候选窗只应在组字期间存在，选词后关闭；
3. 日志开启时可见：
   - `tsf composition start`
   - `tsf context finalized=... composing=...`
   - `tsf composition end`
   - `tsf end edit`

## 当前限制

- 当前依赖系统 IME 的候选/组合窗口；
- TSF message pump 当前禁用，使用窗口消息和 TSF association；
- OnEndComposition 只请求一次异步 edit session，由 OnEndEdit 完成 finalized 文本提交；
- 未实现 `ITextStoreACP`。
