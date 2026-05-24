# conpty — UTF-32 内部化 ConPTY 实现计划

> 创建日期: 2026-05-19

---

## 一、目标

将 `conpty/` 下的所有模块逐步迁移到 `conpty/`，以 **char32_t (UTF-32) VT 序列**作为内部实现的核心数据格式。

### 核心原则

1. **输入时转换**: 所有外部输入（ConDrv UTF-16/ANSI、PTY UTF-8）在入口处立即转为 char32_t
2. **输出时转换**: 内部 char32_t 在边界处按需转回 UTF-8（PTY）或 UTF-16（ConDrv）
3. **VT 消息驱动**: 控制台 API 翻译为 VT 消息（`vt_message_id`），再通过 VT 消息分发到终端同步和内部状态更新
4. **不动 Windows 固定结构体**: `CHAR_INFO`、`INPUT_RECORD`、`CONSOLE_READCONSOLE_MSG` 保持不变，边界处即时转换

---

## 二、现有资产

### conpty/ 已有文件

| 文件 | 状态 | 说明 |
|------|------|------|
| `conpty_vt_parser.hpp` | ✅ 完成 | char32_t VT/CSI/OSC 解析器，18 测试全通过 |
| `text_measurement_mode.hpp` | ✅ 完成 | 三模式枚举 |
| `conpty.hpp` | ❌ 空 | 待实现：组装入口 |
| `conpty.cpp` | ❌ 空 | 待实现 |

### conpty/ 待迁移文件（15 个模块）

| 文件 | 行数(估) | 复杂度 | 说明 |
|------|---------|--------|------|
| `conpty_console_state.hpp` | ~250 | 中 | 控制台状态，含 title/face_name 等 wchar_t 字段 |
| `conpty_screen_buffer.hpp` | ~250 | 中 | CHAR_INFO 网格 |
| `conpty_input_buffer.hpp` | ~120 | 低 | INPUT_RECORD 缓冲 |
| `conpty_io_state.hpp` | ~200 | 中 | I/O 句柄 + ProcessList |
| `conpty_pipe_bridge.hpp` | ~600 | **高** | PTY 管道 I/O + VT 输入解析 + echo |
| `conpty_api_handlers.hpp` | ~900 | **高** | 77 个 API handler |
| `conpty_api_router.hpp` | ~200 | 中 | API 分派 + 双缓冲切换 |
| `conpty_message_router.hpp` | ~120 | 低 | ConDrv 消息路由 |
| `conpty_char_width.hpp` | ~100 | 低 | 字符宽度（需改为 char32_t 输入） |
| `conpty_completion_bridge.hpp` | ~80 | 低 | ReadConsole 完成桥 |
| `conpty_signal.hpp` | ~50 | 低 | PtySignal 线程声明 |
| `conpty_signal.cpp` | ~100 | 低 | PtySignal 线程实现 |
| `conpty_vt_parser.hpp` | ~600 | — | **不迁移**（已有 char32_t 版本） |
| `conpty_vt_input_engine.hpp` | ~300 | 中 | VT 消息→INPUT_RECORD（需适配新 parser） |

---

## 三、编码边界图

```
                    ┌──────────────────────────────────┐
                    │        conpty 内部           │
                    │       (全部 char32_t)             │
                    │                                  │
  ConDrv            │  ┌──────────┐    ┌────────────┐  │         PTY pipe
  ──────→           │  │  UTF-16  │    │  API       │  │         ──────→
  UTF-16/ANSI ──────→  │  →UTF-32 │───→│  Handlers  │──┼──→ VT msg
                    │  │  (convert)│    │            │  │    (char32_t)
                    │  └──────────┘    └─────┬──────┘  │
                    │                       │         │
  ConDrv            │  ┌──────────┐   ┌─────┴──────┐  │         PTY pipe
  ←──────           │  │  UTF-32  │   │  VT msg    │  │         ←──────
  UTF-16 ←─────────────│  →UTF-16 │←──│  Dispatch  │  │    UTF-8
                    │  │  (convert)│   │  (to PTY)  │──┼──→ WideCharToMultiByte
                    │  └──────────┘   └────────────┘  │
                    └──────────────────────────────────┘
```

### 关键转换点

| # | 转换点 | 方向 | 位置 |
|---|--------|------|------|
| 1 | ConDrv UTF-16 → char32_t | 入 | `api_write_console`, `api_fill_output` 等 |
| 2 | ConDrv ANSI → char32_t | 入 | `api_write_console` (Unicode=0) |
| 3 | PTY UTF-8 → char32_t | 入 | `pipe_bridge::accumulate_from_pipe` |
| 4 | char32_t → UTF-8 (VT 序列) | 出 | `pipe_bridge::vt_write_*` |
| 5 | char32_t → UTF-16 (ConDrv 完成) | 出 | `api_read_console`, `api_get_title` 等 |
| 6 | CHAR_INFO (UTF-16) ↔ char32_t | 双向 | `screen_buffer` 边界 |

---

## 四、分阶段实施计划

### 阶段 0: 基础设施（预计 1-2 轮）

**目标**: 建立 conpty 的基础类型和转换工具，不涉及业务逻辑。

#### 0.1 `conpty/char_convert.hpp` — 编码转换工具 ✅ 已完成 (simdutf)
- `to_char32(wchar_t)` — UTF-16 → char32_t（处理代理对）
- `utf8_stream_decoder` — 逐字节 UTF-8 → char32_t 流式解码器 (状态机+simdutf验证)
- `to_utf8(char32_t, OutputIterator)` — char32_t → UTF-8 字节序列
- `to_utf8_bytes(char32_t, char(&)[8])` — 单码点 UTF-8 编码
- `to_wchar(char32_t, wchar_t*)` — char32_t → wchar_t[]（必要时拆分代理对）
- `to_u32string(wstring_view)` — UTF-16 → std::u32string (simdutf::convert_utf16_to_utf32)
- `to_wstring(u32string_view)` — std::u32string → UTF-16 (simdutf::convert_utf32_to_utf16)
- `ansi_to_u32string(const char*, size_t, UINT cp)` — ANSI → u32string
- `utf8_to_u32string(string_view/size_t)` — UTF-8 → std::u32string (simdutf::convert_utf8_to_utf32)
- `u32string_to_utf8(u32string_view)` — std::u32string → UTF-8 (simdutf::convert_utf32_to_utf8)
- `is_combining(char32_t)` — 组合字符判定
- **依赖**: simdutf v9.0.0 (singleheader) — 替换 libunicode convert.h

#### 0.2 `conpty/char_width.hpp` — 字符宽度（char32_t 版本）
- 从 `conpty/conpty_char_width.hpp` 迁移，改为接受 `char32_t`
- 保留三种模式：console / wcswidth / graphemes

---

### 阶段 1: 状态层（预计 2-3 轮）

**目标**: 迁移控制台状态和缓冲区，保持 CHAR_INFO/INPUT_RECORD 边界不变。

#### 1.1 `conpty/console_state.hpp` — 控制台状态
- 从 `conpty/` 迁移，`namespace conpty`
- `title[256]` / `original_title[256]` → `std::u32string`（或 `char32_t[256]`）
- `face_name[32]` → 保留 WCHAR（Windows API 边界）
- `command_history` → `std::vector<std::u32string>`
- `aliases` → `std::unordered_map<std::u32string, std::u32string>`
- `dec_to_unicode()` → 返回 `char32_t`

#### 1.2 `conpty/screen_buffer.hpp` — 屏幕缓冲区 ✅ 已完成 (Row-based char32_t)
- 纯 char32_t 内部存储 via `vector<screen_buffer_row> _rows`
- `screen_buffer_row`: u32string _text + vector<uint16_t> _columns (0x8000 trailing flag) + vector<text_attribute> _attrs
- 公共 API: `set_u32()`, `at_u32()`, `glyph_at()`, `glyph_width()`, `attr_at()`, `set_attr()`, `fill_char()`, `fill_attr()`
- `write_char32()`, `write_attr_seq()`, `read_wchars()`, `read_attrs()`, `clear()`, `scroll(char32_t,WORD)`, `clear_cell()`
- 边界方法: `row_to_ci()`/`row_from_ci()` (CHAR_INFO 转换仅用于 api_handlers 边界)
- 已移除: 所有 CHAR_INFO 公共 API (at/set/read_rect/write_rect/fill_character/fill_attribute 等)

#### 1.3 `conpty/input_buffer.hpp` — 输入缓冲区
- 从 `conpty/` 迁移，基本不变
- INPUT_RECORD 不可改，仅在填充时做 char32_t → WCHAR 转换

---

### 阶段 2: I/O 层（预计 3-4 轮）

**目标**: 迁移 I/O 管理、管道桥接，将 VT 输入路径改为 char32_t。

#### 2.1 `conpty/io_state.hpp` — I/O 句柄管理
- 从 `conpty/` 迁移，基本不变（无文本处理）

#### 2.2 `conpty/pipe_bridge.hpp` — PTY 管道桥接（核心变更）
- 从 `conpty/` 迁移
- **关键变更**: 用 `conpty::vt_parser`（char32_t 版本）替换 `conpty::vt_parser`
- `process_input()`: UTF-8 bytes → `unicode::decoder<char>` → char32_t → vt_parser::parse()
- `accumulate_from_pipe()`: 同上
- `vt_write_*()`: 内部 char32_t VT 序列 → UTF-8 → WriteFile
- `vt_write_cell()`: 接受 char32_t → UTF-8
- echo: 保留原始字节回显（终端需要 UTF-8）

#### 2.3 `conpty/vt_input_engine.hpp` — VT 消息→INPUT_RECORD
- 从 `conpty/` 迁移，适配 `conpty::vt_message`
- `convert_text()`: UTF-8 → char32_t → INPUT_RECORD

---

### 阶段 3: API 层（预计 4-5 轮）

**目标**: 迁移所有 77 个 API handler，将 VT 同步改为 VT 消息驱动。

#### 3.1 `conpty/api_handlers.hpp` — API 处理函数
- 从 `conpty/` 迁移
- `api_write_console()`: UTF-16/ANSI → char32_t → screen_buffer + VT msg 输出
- `api_fill_output()`: 同上
- `api_set_title()`: wchar_t → char32_t → state.title
- `api_get_title()`: state.title (char32_t) → wchar_t
- 其余 API: 同步迁移，VT 输出部分改为基于 VT 消息

#### 3.2 `conpty/api_router.hpp` — API 分派
- 从 `conpty/` 迁移，基本不变
- `switch_active_screen_buffer()` 中的 VT sync 改用 VT 消息

#### 3.3 `conpty/message_router.hpp` — ConDrv 消息路由
- 从 `conpty/` 迁移，基本不变

---

### 阶段 4: 组装与信号（预计 1-2 轮）

#### 4.1 `conpty/conpty.hpp` — 入口组装
- 从 `conpty/conpty.hpp` 迁移
- 将所有模块组装为 conpty 版本

#### 4.2 `conpty/signal.hpp` + `signal.cpp` — PtySignal
- 从 `conpty/` 迁移，基本不变

#### 4.3 `conpty/completion_bridge.hpp` — 完成桥
- 从 `conpty/` 迁移，适配 char32_t

---

### 阶段 5: 测试与验证（预计 2-3 轮）

- 新增 `tests/test_conpty_*.cpp` 测试
- 端到端集成测试
- 对照 `conpty/` 旧实现验证行为一致性

---

## 五、文件结构规划

```
src/conpty/
├── conpty.hpp                  # 入口组装（阶段 4）
├── conpty.cpp                  # 入口实现（阶段 4）
├── conpty_vt_parser.hpp        # ✅ 已有 char32_t VT 解析器
├── text_measurement_mode.hpp   # ✅ 已有 三模式枚举
├── char_convert.hpp            # 阶段 0: 编码转换工具
├── char_width.hpp              # 阶段 0: 字符宽度 (char32_t)
├── console_state.hpp           # 阶段 1: 控制台状态
├── screen_buffer.hpp           # 阶段 1: 屏幕缓冲区
├── input_buffer.hpp            # 阶段 1: 输入缓冲区
├── io_state.hpp                # 阶段 2: I/O 句柄管理
├── pipe_bridge.hpp             # 阶段 2: PTY 管道桥接
├── vt_input_engine.hpp         # 阶段 2: VT消息→INPUT_RECORD
├── api_handlers.hpp            # 阶段 3: API 处理函数
├── api_router.hpp              # 阶段 3: API 分派
├── message_router.hpp          # 阶段 3: ConDrv 消息路由
├── signal.hpp                  # 阶段 4: PtySignal 线程
├── signal.cpp                  # 阶段 4: PtySignal 实现
└── completion_bridge.hpp       # 阶段 4: 完成桥
```

---

## 六、风险与注意事项

1. **CHAR_INFO 边界**: 屏幕缓冲区必须保留 CHAR_INFO（Windows 固定结构体），在入口/出口做 char32_t ↔ WCHAR 转换
2. **INPUT_RECORD 边界**: 同上，输入事件结构体不可改
3. **代理对**: UTF-16 代理对在 char32_t 中是单个码点，但在 WCHAR 中是两个。需要正确处理
4. **性能**: 每条消息在边界处做一次编码转换，影响可控（非热路径）
5. **VT 序列生成**: 原来直接拼接 `"\x1b[...m"` 等 UTF-8 字符串，改为生成 char32_t 序列后再统一转 UTF-8
6. **echo 路径**: 终端回显仍需原始 UTF-8 字节，不经过 char32_t 往返

---

## 七、与 conpty/ 的关系

- `conpty/` 继续作为 byte-level 参考实现存在
- `conpty/` 是独立的完整实现
- 编译时两个目标共存（不同的 namespace `conpty` vs `conpty`）
- 测试目录 `tests/` 同时包含两者的测试


---

## 八、实施状态 (2026-05-19)

### 阶段 0-4: ✅ 全部完成

| 文件 | 状态 | 关键设计 |
|------|------|---------|
| `char_convert.hpp` | ✅ | UTF-8/UTF-16/ANSI ↔ char32_t, 代理对处理, `unicode::decoder<char>` |
| `char_width.hpp` | ✅ | 三种模式 (console/wcswidth/graphemes), char32_t 输入, grapheme_text_width |
| `console_state.hpp` | ✅ | title→u32string, command_history→vector<u32string>, aliases→map<u32string>, dec_to_unicode→char32_t |
| `screen_buffer.hpp` | ✅ | CHAR_INFO 保留, 新增 set_u32/at_u32/write_character(char32_t*)/fill_character(char32_t) |
| `input_buffer.hpp` | ✅ | 与 conpty/ 相同 (INPUT_RECORD 不可改) |
| `io_state.hpp` | ✅ | 与 conpty/ 相同 (无文本编码) |
| `pipe_bridge.hpp` | ✅ | char32_t VT 解析器, UTF-8→char32_t 解码, 无 snprintf VT 输出, u32string 文本累积 |
| `conpty_vt_input_engine.hpp` | ✅ | convert_text() 接受 u32string_view, char32_t→WCHAR 边界转换 |
| `api_handlers.hpp` | ✅ | 22 个 L1/L2 handler, WriteConsole→char32_t, SetTitle/GetTitle→u32string↔wchar_t |
| `api_router.hpp` | ✅ | L1/L2 分发表, switch_active_screen_buffer, vt_write_screen_snapshot (char32_t) |
| `message_router.hpp` | ✅ | ConDrv 消息路由 (纯调度) |
| `signal.hpp` / `signal.cpp` | ✅ | PtySignal 线程 |
| `completion_bridge.hpp` | ✅ | ReadConsole 完成桥 |
| `conpty.hpp` | ✅ | 组装入口, 所有模块 conpty 版本 |

### 设计决策记录

1. **VT 输出无 snprintf**: 所有 VT 序列通过 `vt_append_str`/`vt_append_int`/`vt_append_char` 直接构建 char 缓冲, 最终 `vt_flush()` 写入管道
2. **screen_buffer 单 cell 转换**: `set_u32`/`at_u32` 做 char32_t↔WCHAR 转换; 批量 write_rect/read_rect 保持 CHAR_INFO 级别不变
3. **grapheme 支持**: `char_width.hpp` 提供 `grapheme_text_width()` 在段级别计算总宽度; `console_state::text_measurement` 使用强类型枚举
4. **title 内部化为 u32string**: GetTitle/SetTitle 在 api_handlers 边界做 wchar_t↔char32_t 转换
5. **pipe_bridge 文本累积**: `_text32` (u32string) 替代旧的 `_text_buf` (BYTE[]), VT 解析器的 text 消息直接追加

### 待完成: 阶段 5

- 新增 `tests/test_conpty_*.cpp` 编译并测试新模块
- 将 conpty 集成到 CMake 构建目标
- 端到端集成测试
- 对照 conpty/ 验证行为一致性
