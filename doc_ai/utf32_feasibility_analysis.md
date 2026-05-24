# UTF-32 (char32_t) 内部化可行性分析

> 分析日期: 2026-05-19

---

## 一、当前内部表示

| 子系统 | 当前类型 | 说明 |
|--------|---------|------|
| `screen_buffer` | `CHAR_INFO` (WCHAR + WORD) | Windows 固定结构体，网格单元 |
| `input_buffer` | `INPUT_RECORD` (含 `KEY_EVENT.uChar.UnicodeChar` WCHAR) | Windows 固定结构体 |
| `console_state::title` | `wchar_t[256]` | 对标 `GetConsoleTitleW` |
| `console_state::face_name` | `WCHAR[32]` | 对标 `CONSOLE_CURRENTFONT_MSG` |
| `console_state::command_history` | `std::vector<std::wstring>` | UTF-16 LE |
| `console_state::aliases` | `std::unordered_map<std::wstring,std::wstring>` | UTF-16 LE |
| `api_write_console` 输入 | `wchar_t[]` (Unicode=1) 或 `char[]` ANSI (Unicode=0) | ConDrv 协议 |
| `api_read_console` 输出 | `wchar_t[]` (Unicode=1) 或 `char[]` raw bytes (Unicode=0) | ConDrv 协议 |
| `pipe_bridge::_text_buf` | `BYTE[4096]` (UTF-8) | 键盘输入文本累积 |
| `pipe_bridge::_readbuf` | `BYTE[4096]` (UTF-8) | 管道原始字节 |
| `vt_parser` 输入 | `char` (UTF-8 字节) | VT 序列解析 |
| `vt_input_engine::convert_text` | `BYTE[]` (UTF-8) → `wchar_t` | 手动 UTF-8 解码 |
| `vt_write_cell` 输入 | `wchar_t` | 转 UTF-8 后写入 PTY |

---

## 二、不可变更的接口边界

以下边界由操作系统或协议强制要求，**无法改为 UTF-32**：

### 2.1 Windows 固定结构体

```
CHAR_INFO {
    WCHAR  Char.UnicodeChar;  // ← 固定为 UTF-16 LE
    WORD   Attributes;
}

INPUT_RECORD {
    WORD  EventType;          // KEY_EVENT=1, MOUSE_EVENT=2, etc.
    union {
        KEY_EVENT_RECORD {
            WCHAR uChar;      // ← 固定为 UTF-16 LE
            WORD  wVirtualKeyCode;
            ...
        } KeyEvent;
        ...
    } Event;
}

CONSOLE_READCONSOLE_MSG {
    BOOL  Unicode;            // ← 控制输出格式
    DWORD NumBytes;           // ← 字节数 (UTF-16时是 wchar_t*2)
    ...
}
```

这些都是 Windows SDK 的 `typedef`，无法修改。corehost 必须产出/消费这些结构体。

### 2.2 ConDrv 协议

```
ConDrv 消息体:
  Unicode=1 → 数据为 UTF-16 LE 字节流 (wchar_t[])
  Unicode=0 → 数据为 ANSI 字节流 (CP_ACP 或 CP_OEMCP)
```

ConDrv 驱动内核代码决定了字节序和编码。corehost 作为用户态服务端，只能接受这种输入/输出格式。

### 2.3 PTY 管道

```
corehost ←─ReadFile── UTF-8 ── WT 终端
corehost ──WriteFile─→ UTF-8 ── WT 终端
```

终端的 VT 解析器期待标准 UTF-8，不可更改。

### 2.4 Windows API 调用点

| API | UTF-16 依赖 |
|-----|-----------|
| `MultiByteToWideChar` | 产出 `wchar_t[]` |
| `WideCharToMultiByte` | 接受 `wchar_t[]` |
| `GetACP()` `GetOEMCP()` | Windows 代码页系统 |
| `IsDBCSLeadByteEx` | 接受 `char` |
| `SetConsoleTitleW` `GetConsoleTitleW` | 接受/产出 `WCHAR[]` |
| `GenerateConsoleCtrlEvent` | 无文本参数 |

---

## 三、需要变更的内部组件及开销

假设在不可变边界处做 `wchar_t ↔ char32_t` 的即时转换：

### 3.1 screen_buffer

**当前**: `CHAR_INFO` 网格 (WCHAR 字段为 UTF-16 LE)

**要改的话**: 保留 `CHAR_INFO` 结构体不变（ConDrv 边界需求），但内部用 `char32_t[]` 做副本。

**开销**:
- 读: `char32_t → CHAR_INFO` (每格一次，遍历全网格)
- 写: `CHAR_INFO → char32_t` (每格一次)
- `vt_write_screen_snapshot()` 中遍历整个 80×25=2000 格逐格转换 → 额外 2000 次 `WideCharToMultiByte` 调用

**结论**: 高开销，因为每次 `api_read_console_output` 都要全矩形同步回到 CHAR_INFO 格式给 ConDrv。

### 3.2 input_buffer

**当前**: `std::vector<INPUT_RECORD>` (WCHAR 字段)

**要改的话**: 保留 INPUT_RECORD 不变，内部用 `char32_t` 副本。

**开销**:
- `write()`: `INPUT_RECORD → char32_t` 再存
- `read()`: `char32_t → INPUT_RECORD` 每记录转换
- 代理对处理: BMP 内无问题，非 BMP 需在 `read()` 时从 `char32_t` 拆成 `wchar_t` 代理对

**结论**: 中等开销，每次 API 调用多一轮转换。GetConsoleInput / ReadConsole 调用频次高。

### 3.3 console_state 字符串字段

**当前**: `wchar_t title[256]`, `WCHAR face_name[32]`, `std::vector<std::wstring>` command_history, `std::unordered_map<std::wstring, std::wstring>` aliases

**要改的话**: `char32_t` 容器 + 边界转换

**开销**:
- `api_get_title`: `char32_t[] → wchar_t[]` 转换后放入 ConDrv 响应体
- `api_set_title`: `wchar_t[] → char32_t[]` 转换后存储
- `api_l3_get_aliases`: 每次遍历整个 map 做 key/value 转换
- `api_l3_get_command_history`: 每条历史记录转换

**结论**: 低开销（字符串操作本身不频繁，每次转换只需遍历字符串长度）。

### 3.4 vt_input_engine::convert_text

**当前**: 手动 UTF-8 → wchar_t 解码（2/3/4 字节解析）

**要改的话**: 使用 `unicode::decoder<char32_t>` 直接解码为 `char32_t`，然后在 emit_record 时转换为 `wchar_t`（因为 `INPUT_RECORD.K.uChar` 只能是 WCHAR）。

**开销**: 优于当前——`unicode::decoder<char>` 是 constexpr 状态机，比当前手写逻辑更可靠，且自动处理 4 字节序列（当前代码 4 字节序列被跳过）。

**结论**: **这是唯一有明显收益的换成点**——不是换成 char32_t 存储，而是在 UTF-8→字符 这一跳使用 libunicode 的 decoder。

### 3.5 api_write_console 游程

**当前**: 遍历 wchar_t[]，每字符做 char_width 判断，然后 CUP+SGR+vt_write_cell

**要改的话**: 遍历 char32_t[]，char_width_for_mode 可直接接受 char32_t（当前已有 to_char32 转换），vt_write_cell 再转回 UTF-8

**开销**: `wchar_t[] → char32_t[]` 整段预处理 + `char32_t → UTF-8` 逐格，与当前 `wchar_t → UTF-8` 相比并无优势。

---

## 四、转换开销汇总

```
            wchar_t ↔ char32_t 转换点边界图:

  ┌─────────────────────────────────────────────────────┐
  │                    ConDrv 协议                        │
  │  wchar_t[] (UTF-16) / char[] (ANSI)                 │
  └──────┬──────────────────────────────────┬───────────┘
         │ CONVERSION 1                     │ CONVERSION 4
         ▼                                  ▼
  ┌──────────────┐                  ┌──────────────┐
  │ screen_buffer │                  │ input_buffer  │
  │ CHAR_INFO     │                  │ INPUT_RECORD │
  │ (WCHAR 字段)  │                  │ (WCHAR 字段)  │
  └──────────────┘                  └──────────────┘
         ▲                                  ▲
         │ CONVERSION 2                     │ CONVERSION 3
         ▼                                  ▼
  ┌──────────────────────────────────────────────────────┐
  │            假设的 char32_t 内部层                       │
  │  char32_t[] grid   char32_t[] events                  │
  │  u32string title   vector<u32string> history          │
  └───────┬───────────────────────────────────┬──────────┘
          │ CONVERSION 5                      │
          ▼                                   │
  ┌──────────────┐                            │
  │  PTY 管道     │←──── UTF-8 ──── WT 终端    │
  │  vt_write    │                            │
  └──────────────┘                            │
          ▲                                   │
          │ CONVERSION 6                      │
  ┌───────┴────────────────────────────────────┘
  │  PTY 管道 → vt_parser (UTF-8 字节)
  │           → decoder<char32_t> (替代手写 UTF-8 解码)
  └────────────────────────────────────────────┘
```

每个 CONVERSION 箭头都是一次 O(n) 遍历 + MultiByteToWideChar/WideCharToMultiByte 调用。

---

## 五、结论

### 5.1 全量切换 → **不可行**

原因：
1. **Windows 固定结构体** (`CHAR_INFO`, `INPUT_RECORD`, `CONSOLE_READCONSOLE_MSG`) 是核心数据承运体，必须保持 WCHAR
2. **ConDrv 协议** 强制 UTF-16 或 ANSI，改不了
3. 全量切换意味着在 **6 个边界** 上各增加一层转换，总计 O(n) 额外开销，无性能收益

### 5.2 部分切换 → **不值得**

`console_state` 的字符串容器 (`wstring`, `WCHAR[]`) 换成 `u32string`/`char32_t[]` 需要增加 `api_get_title`/`api_set_title`/`api_get_aliases`/`api_get_history` 处的转换代码。

这些 API 调用频率低（标题一次、别名/历史基本为零），收益几乎为零，但代码复杂度上升。

### 5.3 有收益的改进 → **局部优化 convert_text**

当前 `convert_text` 中有手写的 UTF-8→wchar_t 解码器（3 字节截断 BMP，4 字节序列被跳过）。可以用 libunicode 的 `decoder<char>` 替换：

```cpp
// 当前代码 (conpty_vt_input_engine.hpp L174-202):
//   手动位操作解码 UTF-8→wchar_t, 只支持 BMP

// 改为:
unicode::decoder<char> dec;
for (DWORD i = 0; i < len; ++i) {
    auto cp = dec(static_cast<char>(utf8[i]));
    if (!cp) continue;
    wchar_t wch = static_cast<wchar_t>(*cp);  // BMP direct
    // ... emit wch as INPUT_RECORD
}
```

这个改进：
- 消除手写位操作 bug（当前 4 字节序列静默跳过）
- 正确处理非 BMP 字符（4 字节序列）
- 不需要改变任何存储格式
- 不引入任何额外转换开销（`decoder<char>` 是零开销 constexpr 状态机）

### 5.4 建议

| 操作 | 建议 |
|------|------|
| 全量切换内部缓冲为 UTF-32 | ❌ 不可行，Windows 结构体边界太多 |
| 字符串容器换成 `u32string` | ❌ 不值得，API 频率低 |
| `convert_text` 用 `unicode::decoder<char>` | ✅ 推荐，消除 bug 且无性能损失 |
| `char_width_*` 参数升级为 `char32_t` | ✅ 已通过 `to_char32()` 实现，无需改变调用方 |
| DEC 映射表改为 `char32_t` | ⚠️ 可选，当前 `wchar_t` 已足够（框线字符全在 BMP） |
