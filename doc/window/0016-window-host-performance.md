# window_host 性能与提前返回

## 缓冲区重用

`window_host` 增加 UI 线程 scratch buffer：

```cpp
std::u16string ime_text_buffer_;
std::u16string copy_buffer_;
std::wstring window_title_buffer_;
```

用途：

- IME `IMR_RECONVERTSTRING` / `IMR_DOCUMENTFEED` 不再每次构造 `std::u16string`；
- 复制选区不再返回新字符串，直接复用 `copy_buffer_`；
- `client exited` 标题复用 `window_title_buffer_`。

`console` 增加：

```cpp
void selected_text_into(std::u16string &result);
```

`selected_text()` 没有调用方，已删除；只保留 `selected_text_into`，复用调用方容量，并在行选择模式复用 `selection_line_buffer_`，避免逐行分配。

## 提前返回

`window_proc` 改为：

```cpp
if (message == WM_NCCREATE) { ...; return ...; }
if (self == nullptr) { return DefWindowProcW(...); }
```

之后每个 case 直接执行，不再使用：

```cpp
if (self != nullptr)
{
    ...
}
```

热路径同时改为提前返回：

- `WM_MOUSEWHEEL`：delta 为 0、转发鼠标、滚动三种情况直接 return；
- `WM_MOUSEMOVE`：scrollbar 拖动、选区拖动、mouse reporting 分别提前返回；
- `WM_LBUTTONDOWN`：scrollbar 命中、mouse reporting、开始选择直接返回；
- `WM_RBUTTONUP`：mouse tracking、复制选区、粘贴分别返回；
- `WM_CHAR`：过滤条件先返回，合法字符再入队。

## 验证

```text
100% tests passed, 0 tests failed out of 12
```

右键粘贴、键盘输入和窗口消息循环已实测。
