# IME / TSF 实现

## 决策

`corehostw` 以 TSF 为主输入路径；为了兼容仍处理 `WM_IME_REQUEST`（反向转换/字符位置），
不维护完整的 IMM32 输入法路径。

## 当前实现

- `ITfThreadMgrEx` / `ITfDocumentMgr` / `ITfContext`；
- `ITfContextOwner`、`ITfContextOwnerCompositionSink`、`ITfTextEditSink`；
- `AssociateFocus` + `SetFocus`；
- 通过 `ITfReadOnlyProperty::EnumRanges` 枚举 context ranges；
- 按 `GUID_PROP_COMPOSING` 区分 finalized 与 composing 文本；
- finalized 文本提交到 console input queue，然后从 context 删除；
- composing 文本用于自绘；
- composition 文本通过 `console::set_ime_composition` 自绘在光标处；
- 先 advise `ITfContextOwner` 再 Push context；
- candidate/composition 位置来自 `ITfContextOwner::GetTextExt`；
- 为满足候选窗视觉位置，`GetTextExt` 的 `left/right` 统一左移；
- 左移量由 `window_config::candidate_offset_cells` 控制，默认 `2`，单位是 cell；
- 不再在 `tsf_host` 中硬编码 `2 * cell_width`。

## TSF message pump 决策

**不需要独立的 TSF message pump，标记为不需要。**

理由：

- corehostw 有标准 Win32 消息循环，`GetMessageW`/`DispatchMessageW` 会分发 TSF 的
  hidden window 消息；
- `ITfMessagePump` 面向没有标准消息循环或需要在模态循环中 pump 的宿主；
- 当前 composition/candidate 路径由窗口消息、`AssociateFocus` 和 edit session 驱动，
  微软拼音基础输入已验证；
- 增加 pump 不会解决第三方 TIP 的文本存储/重组问题，反而增加重入风险。

只有未来引入模态 TSF UI 或自建消息循环时才重新评估。

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
- 使用标准窗口消息循环，不需要 TSF message pump；
- OnEndComposition 只请求一次异步 edit session，由 OnEndEdit 完成 finalized 文本提交；
- 未实现 `ITextStoreACP`；
- 第三方 TIP/复杂重组行为仍未验收；
- 日文/韩文输入法仍需硬件验收。

## 是否实现 `ITextStoreACP`

**当前不实现，按已知限制记录。**

理由：

- 当前窗口是 TUI 宿主，文本编辑由 client 自己完成；corehost 只需要把 finalized 文本交给
  console input，并显示 composition；
- `ITfContextOwner` + `ITfContextOwnerCompositionSink` + `ITfTextEditSink` 已能完成
  Microsoft Pinyin 的基础 composition/candidate 路径；
- 引入 `ITextStoreACP` 会把 console input、selection 和 TSF 文本存储耦合起来，需要重新
  设计 selection/编辑语义，不能为了“接口完整”而添加。

只有在以下需求成立时才重新评估：

- 需要第三方 TIP 直接编辑/重组 console 文本；
- 需要日文/韩文候选、reconversion、复杂 composition；
- 需要 TSF 客户端把窗口当作可编辑 text store，而不是 TUI 屏幕。

## 所有权与生命周期

- COM apartment 由 `window_entry` 持有，TSF 模块不调用 `CoInitializeEx/CoUninitialize`；
- `tsf_host` 是 `window_host` 的直接成员，不使用 unique_ptr/optional；
- 构造时完成 TSF 初始化；失败时把自身 HWND 置空，析构只检查这个空状态；
- 非析构路径使用断言；
- hresult 只在 COM 回调和 edit session 边界捕获；
- 回调使用函数指针 + `void* context`，不使用 `std::function`；
- COM 调用使用 `winrt::check_hresult` / `winrt::hresult_error`；
- `winrt::com_ptr` 析构自动释放，析构函数不手工置空成员。