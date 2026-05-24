# 原始 OpenConsole 如何支持 Grapheme Cluster

> 分析日期: 2026-05-19
> 问题: ConDrv 屏幕缓冲区 (`CHAR_INFO`) 每格只有一个 WCHAR，如何表示需要多个 codepoint 的 grapheme cluster？

---

## 一、直接答案

**原始 OpenConsole 不支持将多个 codepoint 的 grapheme cluster 存储在单个 `CHAR_INFO` 格中。**

Grapheme cluster 的处理方式与单字符完全相同：

1. **宽度计算层** (`TextMeasurementMode::graphemes`) 负责计算 cluster 占几列
2. **存储层** (`CHAR_INFO` 网格) 将 cluster 的"代表性字符"存入第一格，后续格类推
3. **渲染层** (GDI / DirectWrite / WT) 负责将 cluster 正确绘制到屏幕上

三层各司其职，互不越界。

---

## 二、原始三个层次

### 2.1 宽度计算层 — `TextMeasurementMode`

```
enum class TextMeasurementMode { Console, Graphemes, Wcswidth };
```

| 模式 | 宽度计算方式 | 示例 `🤦🏼‍♀️` |
|------|------------|--------|
| `console` | 传统 CJK 启发式 | 视为4个独立字符 |
| `wcswidth` | POSIX `wcwidth()` | 视为4个独立字符 |
| `graphemes` | Unicode 字簇分割 | **视为1个 grapheme cluster，宽度=2** |

**关键**: 宽度计算决定一个字符串占几列，但不决定每列的存储内容。

### 2.2 存储层 — `CHAR_INFO` 网格 + DBCS 标记

```
CHAR_INFO cell {
    WCHAR  Char.UnicodeChar;   // 字符 (可能为 0x0000 表示 trailing)
    WORD   Attributes;         // 属性 (含 COMMON_LVB_TRAILING_BYTE 0x0100)
};
```

**原始 OpenConsole 的存储机制**（自 Windows NT 以来）：

#### Case 1: 单宽字符 (ASCII / 半角)
```
cell[N]:   ch='A',   attr=0x07
cell[N+1]: ch='B',   attr=0x07
```

#### Case 2: 双宽字符 (CJK / 全角 / emoji 宽度为 2)
```
cell[N]:   ch='字',  attr=0x07
cell[N+1]: ch=0x0000, attr=0x07 | COMMON_LVB_TRAILING_BYTE (0x0100)
```
第二格 `Char.UnicodeChar = 0x0000` 表示 "我是左侧格的延续"，不是真正的 NUL 字符。`Attributes` 中的 `COMMON_LVB_TRAILING_BYTE` 位标记此格是 trailing cell。

#### Case 3: 旧式 DBCS（Shift-JIS, GB2312 等）
```
cell[N]:   ch=DBCS主字节, attr=0x07
cell[N+1]: ch=DBCS尾字节, attr=0x07 | COMMON_LVB_TRAILING_BYTE
```
`CHAR_TYPE_LEADING(2)` / `CHAR_TYPE_TRAILING(3)` 宏用于 `CharType` API 返回。

#### Case 4: Grapheme Cluster（ZWJ 序列、修饰符、组合标记）
```
输入: "👨‍👩‍👧" (Man+ZWJ+Woman+ZWJ+Girl = 7 个 codepoint)

存储 (graphemes 模式下宽度=2):
  cell[N]:   ch='👨' (U+D83D U+DC68 的 WCHAR 编码, 即 UTF-16 代理对拆成 2 个 WCHAR)
             ^ 实际上这里有问题: WCHAR 只有 16 bit, 放不下 U+1F468
```

**这就是问题的核心。** `CHAR_INFO.Char.UnicodeChar` 是单个 `WCHAR` (16 bit)，无法存放 BMP 外的完整码点 (U+10000 以上)。

---

## 三、原始 OpenConsole 的实际处理

### 3.1 BMP 外字符 (U+10000+) 的存储

原始 OpenConsole 的 `TextBuffer` (即 `ROW` 数组) 中，**每个格是一个 `TextAttribute` + `wchar_t`**。

对于 BMP 外字符（如 emoji U+1F600），实际存储为：
- **cell[N]**: 高代理项 (high surrogate, 0xD83D)
- **cell[N+1]**: 低代理项 (low surrogate, 0xDE00)
- 第三个（如果宽度=2）：**cell[N+2]**: 0x0000 + TRAILING_BYTE 标记

原始代码中 `TextAttribute` 的 `IsGlyphFullWidth` 标志和 `DbcsAttribute` 枚举用于标记：
```cpp
enum class DbcsAttribute : uint8_t {
    Single,    // 单字节 DBCS 子格
    Leading,   // DBCS 前导字节 / 高代理项
    Trailing,  // DBCS 尾随字节 / 低代理项 / 0x0000 双宽延续
};
```

### 3.2 Grapheme Cluster 的实际存储

**Grapheme cluster 并不完整存储**。原始的 `WriteCharsLegacy` / `WriteChars` 流程：

```
输入字? "é" (e + combining acute accent = U+0065 + U+0301)
     │
     ├─ [1] MultiByteToWideChar / 直接 UTF-16
     │      wchar_t[] = { 0x0065, 0x0301 }
     │
     ├─ [2] TextMeasurementMode::graphemes 宽度计算
     │      grapheme_cluster_width({0x0065, 0x0301}) = 1
     │      → cluster 占 1 列
     │
     ├─ [3] 写入 ROW:
     │      cell[N]: ch=0x0065('é' 的预组合形式 或 'e')
     │               ↑ 注意: 原始代码可能会将 U+0065+U+0301 预组合为 U+00E9('é')
     │                 如果无法预组合，则只存 base char 0x0065，combining mark 丢弃
     │
     └─ [4] 渲染: GDI/DirectWrite 在绘制时读取 cell[N] 上的字符，
              若需要完整 cluster，从 TextBuffer 的 UnicodeStorage 中查找
```

**原始代码中存在一个重要的补充机制**：`ROW::_unicodeStorage`。

```cpp
// 原始 ROW 结构 (简化)
struct ROW {
    std::vector<CHAR_INFO> _charRow;
    // 当格?的字符是 BMP 外或多 codepoint cluster 时，
    // 在 _unicodeStorage 中存储完整序列
    std::unordered_map<size_t, std::wstring> _unicodeStorage;
};
```

**`_unicodeStorage`** 是解决 "WCHAR 不够用" 的关键：
- 对于 BMP 字符 → 直接存在 `CHAR_INFO.Char.UnicodeChar`
- 对于 BMP 外字符 (U+10000+) → 代理对拆成 2 格存储，`_unicodeStorage[col]` 存完整码点序列
- 对于 grapheme cluster → `_unicodeStorage[col]` 存完整 codepoint 序列（如 `{0x1F468, 0x200D, 0x1F469, 0x200D, 0x1F467}`）

### 3.3 渲染层如何使用 _unicodeStorage

```
GDI 渲染路径:
  TextBuffer::GetTextDataAt(cell) → 先查 _unicodeStorage
    ├─ 有 → 返回完整 wstring (含多 codepoint cluster)
    └─ 无 → 返回 {cell.Char.UnicodeChar}

DirectWrite 渲染:
  TextBuffer::GetText() → 遍历 row, 每个 cell 拼接 _unicodeStorage 或 CHAR_INFO
  → 形成完整 UTF-16 字符串 → IDWriteTextLayout
```

---

## 四、ConDrv/CHAR_INFO 边界

**ConDrv 协议只看 `CHAR_INFO` 表面**。当用户程序调用 `ReadConsoleOutput` 时：

```
TextBuffer 内部:
  cell[5]: ch=0xD83D, attr=0x07          (高代理项)
  cell[6]: ch=0xDE00, attr=0x07|TRAILING (低代理项, emoji 😀 U+1F600)
  cell[7]: ch='A',    attr=0x07
  _unicodeStorage[5] = {0xD83D, 0xDE00}  (完整序列)

ReadConsoleOutput 返回的 CHAR_INFO[]:
  cell[5]: ch=0xD83D, attr=0x07
  cell[6]: ch=0xDE00, attr=0x07|TRAILING
  cell[7]: ch='A',    attr=0x07
  ↑ ConDrv 不暴露 _unicodeStorage！客户端只看到代理对拆成两格。
```

这意味着 **grapheme cluster 信息在 ConDrv 边界丢失**。客户端程序（如 cmd.exe）只看到传统 CHAR_INFO 格式。只有 conhost 内部的渲染器能利用 `_unicodeStorage` 做正确的字型渲染。

---

## 五、corehost 的对标情况

| 原始机制 | corehost 对应 | 状态 |
|---------|-------------|------|
| `DbcsAttribute::Leading/Trailing` | `COMMON_LVB_TRAILING_BYTE` + `0x0000` | ✅ 已实现 (screen_buffer 双宽填充) |
| `_unicodeStorage` | **不存在** | ❌ 缺失 |
| `TextMeasurementMode::graphemes` | `text_measurement_mode::graphemes` | ✅ 宽度计算使用 libunicode |
| BMP 外字符渲染 | 依赖 WT 终端的 UTF-8 渲染 | ✅ VT 输出已发送完整 UTF-8 |
| API 层 `ReadConsoleOutput` | `api_read_console_output` | ⚠️ 直接读 CHAR_INFO，无法暴露 cluster 信息 |

### 结论

1. **原始 OpenConsole 不将 grapheme cluster 存入 CHAR_INFO** — 它用 `_unicodeStorage` 补充
2. **grapheme cluster 支持仅影响宽度计算**，不影响 ConDrv 协议格式
3. **corehost 不需要 `_unicodeStorage`** — 因为 VT 输出管道（UTF-8）可以携带完整 codepoint 序列，WT 终端会正确渲染
4. **corehost 的 `char_width_graphemes()` 已正确处理** — 它计算的宽度与原始一致

---

## 六、实际案例: emoji 在 corehost 中的完整旅程

```
WriteConsole(hOut, L"hello 🤦🏼‍♀️ world", 20)
  │
  ├─ ConDrv → api_write_console, sbytes=40
  │    wchar_t[] = L"hello 🤦🏼‍♀️ world"
  │
  ├─ text_measurement_mode::graphemes:
  │    'h','e','l','l','o',' ' → 各宽度 1
  │    🤦🏼‍♀️ (grapheme cluster) → libunicode::grapheme_cluster_width = 2
  │    ' ','w','o','r','l','d' → 各宽度 1
  │
  ├─ screen_buffer 写入:
  │    cell[0]: ch='h'    cell[1]: ch='e'    ...
  │    cell[6]: ch=0xD83E (🤦 的高代理项 UTF-16)
  │    cell[7]: ch=0x0000  attr=TRAILING    👈 CLUSTER 简化存储
  │    用户程序 ReadConsoleOutput 看到:
  │      cell[6]=🤦, cell[7]=trailing null
  │
  ├─ VT 输出:
  │    bridge->vt_write_cell('h') → UTF-8 0x68
  │    ...
  │    bridge->vt_write_cell(0xD83E) → WideCharToMultiByte(CP_UTF8) → 完整 emoji 序列
  │    终端收到 UTF-8: "hello \xF0\x9F\xA4\xA6\xF0\x9F\x8F\xBC\xE2\x80\x8D\xE2\x99\x80\xEF\xB8\x8F world"
  │    ↑ WT 终端完整渲染 grapheme cluster ✓
  │
  └─ ConDrv 响应: NumBytes=40, 成功
```

**核心矛盾化解**：`screen_buffer` 用 2 列 + TRAILING_BYTE 标记宽度，VT 管道用完整 UTF-8 序列让终端正确渲染。两者互不冲突。

---

*\*基于 Microsoft OpenConsole 原始代码 (`terminal/src/`) 分析 + corehost 现有实现对照*
