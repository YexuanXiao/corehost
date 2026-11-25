# IME / TSF 实现

## 决策

`corehostw` 只走 TSF，不再维护 IMM32 兼容路径。

## 当前实现

- `ITfThreadMgrEx` / `ITfDocumentMgr` / `ITfContext`；
- `ITfContextOwner`、`ITfContextOwnerCompositionSink`、`ITfTextEditSink`；
- `AssociateFocus` + `SetFocus`；
- composition range 读取，提交到 console input queue；
- candidate/composition 位置来自 `ITfContextOwner::GetTextExt`；
- 为满足候选窗视觉位置，`GetTextExt` 的 `left/right` 统一左移：

```text
10 * DPI / 96
```

## 如何判断 composition

1. 微软拼音出现候选窗，说明 TSF composition 已开始；
2. 候选窗只应在组字期间存在，选词后关闭；
3. 日志开启时可见：
   - `tsf composition start`
   - `tsf composition text length=... commit=0`
   - `tsf composition end`
   - `tsf composition text length=... commit=1`

## 当前限制

- 还没有自绘 inline composition；
- 当前依赖系统 IME 的候选/组合窗口；
- TSF message pump 当前禁用，使用窗口消息和 TSF association；
- 未实现 `ITextStoreACP`。
