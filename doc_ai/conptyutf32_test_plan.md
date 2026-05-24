# conpty 测试计划

> 版本 1.0 — 2026-05-19

---

## 一、测试目标

对 conpty（char32_t 内部化 ConPTY）进行全覆盖自动化测试，验证：
1. 每个 VT 序列的解析和输出正确性
2. 每个控制台 API handler 的功能正确性
3. 屏幕缓冲区操作的正确性
4. 编码转换的正确性（UTF-8/UTF-16/UTF-32，含 simdutf）
5. 控制台状态机的完整性
6. libconpty 公共 API 的端到端行为
7. 边界条件和故障恢复

---

## 二、测试架构

```
┌──────────────────────────────────────────┐
│          测试二进制 (test_*)              │
│                                          │
│  ┌──────────┐  ┌──────────┐  ┌────────┐ │
│  │ Unit     │  │Component │  │ E2E    │ │
│  │ Tests    │  │ Tests    │  │ Tests  │ │
│  └────┬─────┘  └────┬─────┘  └───┬────┘ │
│       |              |            |      │
│       v              v            v      │
│  vt_parser.hpp  screen_buffer  libconpty│
│  char_convert   vt_msg_dispatch.conpty  │
│  char_width     pipe_bridge   _static   │
│  input_buffer   api_handlers            │
│  console_state  (mock io_msg)           │
└──────────────────────────────────────────┘
```

### 层级定义

| 层级 | 说明 | 依赖 | 速度 |
|------|------|------|------|
| **Unit** | 单个头文件/类的纯函数测试 | 仅头文件 | 微秒级 |
| **Component** | 多个模块交互，mock io_msg | 头文件 + mock | 毫秒级 |
| **E2E** | 真实 ConPTY 进程 + 管道 I/O | conpty_static + corehost.exe | 秒级 |

---

## 三、测试覆盖清单

### 3.1 VT 解析器 — ~86 测试 (已有 test_vt_parser_utf32)

| 类别 | 测试项 | 数量 |
|------|--------|------|
| 光标移动 | CUU/CUD/CUF/CUB/CNL/CPL/CHA/VPA/CUP/HVP | 10 |
| 编辑 | ICH/DCH/ECH/IL/DL | 5 |
| 擦除 | ED(0/1/2/3)/EL(0/1/2) | 7 |
| SGR | 标准16色/256色/RGB/组合/复位 | 12 |
| 模式 | DECSET/DECRST | 8 |
| 光标 | DECTCEM/DECSCUSR/DECSC/DECRC | 5 |
| 键盘 | 方向键/Home/End/F1-F12/修饰键 | 16 |
| 滚动 | SU/SD/RI | 3 |
| Tab | CHT/CBT/HTS/TBC | 4 |
| 标题 | OSC 0/OSC 2 | 2 |
| 其他 | DSR/DA/DECSTR/调色板 | 6 |
| 边界 | 空输入/非法序列/截断序列/UTF-8错误 | 8 |
| **小计** | | **~86** |

### 3.2 编码转换 (char_convert.hpp) — ~22 测试

| 类别 | 测试项 | 数量 |
|------|--------|------|
| UTF-16→UTF-32 | BMP/代理对/空/大输入 | 4 |
| UTF-32→UTF-16 | BMP/非BMP/空 | 3 |
| UTF-8→UTF-32 | ASCII/2字节/3字节/4字节/空 | 5 |
| UTF-32→UTF-8 | 1-4字节输出/空 | 3 |
| 流式解码 | 逐字节/截断/非法字节 | 4 |
| ANSI→UTF-32 | 不同代码页/空 | 3 |
| **小计** | | **~22** |

### 3.3 屏幕缓冲区 (screen_buffer.hpp) — ~26 测试

| 类别 | 测试项 | 数量 |
|------|--------|------|
| 基本读写 | set_u32/at_u32/glyph_at/glyph_width/attr_at/set_attr | 6 |
| 填充 | fill_char/fill_attr | 4 |
| 写入序列 | write_char32/write_attr_seq | 2 |
| 读取序列 | read_wchars/read_attrs | 2 |
| 滚动 | scroll 上/下/裁剪 | 4 |
| 清除 | clear/clear_cell | 2 |
| 调整大小 | resize 扩大/缩小 | 2 |
| 边界 | 坐标裁剪/空操作/负坐标 | 4 |
| **小计** | | **~26** |

### 3.4 控制台状态 (console_state.hpp) — ~13 测试

| 类别 | 测试项 | 数量 |
|------|--------|------|
| 光标 | 位置/可见性/形状/保存恢复 | 4 |
| Tab | 初始化/设置/清除/查找 | 4 |
| 模式 | 输入/输出模式位 | 2 |
| 标题 | 设置/获取/原始标题 | 3 |
| **小计** | | **~13** |

### 3.5 VT 消息分发 (vt_msg_dispatch.hpp) — ~29 测试

| 类别 | 测试项 | 数量 |
|------|--------|------|
| 光标操作 | CUP/CHA/VPA/CUU/CUD/CUF/CUB/CNL/CPL/DECSC/DECRC | 11 |
| SGR | 颜色/样式/复位/RGB | 4 |
| 文本写入 | 单字符/多字符/换行/滚动触发 | 4 |
| 擦除 | ED_0/ED_1/ED_2/EL_0/EL_1/EL_2 | 6 |
| 标题 | 设置窗口标题 | 1 |
| 杂项 | 行绘制/Tab/HTS/TBC | 3 |
| **小计** | | **~29** |

### 3.6 API Handlers (api_handlers.hpp) — ~46 测试

| 类别 | 测试项 | 数量 |
|------|--------|------|
| L1 Write | WriteConsole UTF-16/ANSI/空 | 4 |
| L1 Read | ReadConsole/Peek/GetNumInput | 4 |
| L1 其他 | GetCP/SetCP/GetMode/SetMode/GetLangId | 5 |
| L2 填充 | FillOutput char/attr/全屏 | 3 |
| L2 光标 | GetCursor/SetCursor/SetCursorPos | 3 |
| L2 SB | GetSBInfo/SetSBInfo/SetSBSize | 3 |
| L2 滚动 | ScrollSB 上/下/裁剪 | 3 |
| L2 输出 | WriteOutput/ReadOutput/WriteOutputString/ReadOutputString | 4 |
| L2 其他 | SetTextAttr/SetWindowInfo/FlushInput/CtrlEvent | 4 |
| L3 字体 | GetFontSize/GetCurrentFont/SetCurrentFont | 3 |
| L3 别名 | AddAlias/GetAlias/GetAliases | 3 |
| L3 历史 | GetHistory/ExpungeHistory/SetNumCommands | 3 |
| L3 其他 | MouseInfo/DisplayMode/ConsoleWindow/SelectionInfo/ProcessList | 5 |
| **小计** | | **~46** |

### 3.7 管道桥接 (pipe_bridge.hpp) — ~15 测试

| 类别 | 测试项 | 数量 |
|------|--------|------|
| VT输出 | vt_msg_send 各 case 序列化为正确 UTF-8 | 6 |
| raw_write | UTF-16/ANSI→UTF-8 | 3 |
| 回显 | echo 字节保留 | 2 |
| 缓冲刷新 | 满缓冲触发flush | 2 |
| EOF | 管道断开 | 2 |
| **小计** | | **~15** |

### 3.8 端到端 (E2E via libconpty) — ~22 测试

| 类别 | 测试项 | 数量 |
|------|--------|------|
| 创建 | CreatePseudoConsole/Resize/Close | 3 |
| 文本I/O | Write→VT/UTF-8→VT→INPUT_RECORD | 4 |
| 控制序列 | CUP/SGR/ED/EL/光标 | 5 |
| API调用 | WriteConsole/ReadConsole/FillOutput/SetTitle | 5 |
| 信号 | PTY_SIGNAL_RESIZE_WINDOW | 1 |
| 多进程 | 两进程共享ConPTY | 1 |
| 错误处理 | 无效参数/已关闭句柄 | 3 |
| **小计** | | **~22** |

### 总计: ~259 个测试

---

## 四、自动化测试机制

### 方案 A: 管道对 (E2E 集成测试)

```
Test Process                        ConPTY (corehost.exe)
    |                                      |
    |-- WriteFile(vt_in, text) ----------->|
    |                                      | 解析VT -> 更新SB + 生成INPUT_RECORD
    |<- ReadFile(vt_out) -----------------| VT序列化输出 (UTF-8)
    |                                      |
    |-- 比较 vt_out 与预期 VT 序列 -------|
```

**核心辅助函数**:
- `create_conpty()` — 调用 ConptyCreatePseudoConsole 创建实例
- `close_conpty()` — 关闭 ConPTY 句柄
- `write_conpty_input(hInput, bytes, len)` — 写入 ConPTY 输入
- `read_conpty_output(hOutput, buf, timeout_ms)` — 读取 ConPTY VT 输出
- `expect_output_timeout(hOutput, expected_utf8, ms)` — 等待并比较输出
- `send_vt_sequence(hOutput, vt_bytes, len)` — 从终端侧发送 VT 序列

### 方案 B: 内存 mock (Component)

直接构造 `io_msg` 结构体，实例化 pipe_bridge/screen_buffer/api_router。

### 方案 C: 纯函数 (Unit)

直接调用头文件函数（parser/convert/width），验证输入输出。

---

## 五、文件组织

```
tests/
├── CMakeLists.txt              # 测试注册
├── test_common.hpp             # 公共宏 (ASSERT, RUN_TEST)
├── conpty_test_helpers.hpp     # E2E 辅助 (创建/读写 ConPTY)
├── test_vt_parser_utf32.cpp    # [已有] VT解析器测试
├── test_char_convert.cpp       # 编码转换测试
├── test_screen_buffer.cpp      # 屏幕缓冲区测试
├── test_console_state.cpp      # 控制台状态测试
├── test_vt_msg_dispatch.cpp    # VT消息分发测试
├── test_api_handlers.cpp       # API handler测试
├── test_pipe_bridge.cpp        # 管道桥接测试
└── test_conpty_e2e.cpp         # 端到端测试
```

---

## 七、实现进展

### Iter 1 (2026-05-19) — ✅ 完成
- ✅ `tests/conpty_test_helpers.hpp` — E2E 辅助函数库
- ✅ `tests/test_char_convert.cpp` — 27 个编码转换测试 (全部通过)
- ✅ `tests/test_console_state.cpp` — 24 个控制台状态测试 (全部通过)
- ✅ `tests/CMakeLists.txt` — 新增 test_simdutf 静态库 + 3 个测试目标
- 📄 `doc/conpty_test_plan.md` — 本文档

### Iter 2 (2026-05-19) — ✅ 完成
- ✅ `tests/test_screen_buffer.cpp` — 21 屏幕缓冲区测试 (读写/填充/序列/滚动/清除/调整/边界)
- ✅ `tests/test_vt_msg_dispatch.cpp` — 25 VT消息分发测试 (光标/SGR/文本/擦除/滚动/标题/行绘制/Tab)
- ✅ 构建修复: libunicode include 路径, test_screen_buffer 编码问题
- ✅ 链接修复: test_vt_msg_dispatch/test_screen_buffer → unicode::unicode

**当前总计: 6 个测试目标, 98 个测试用例, 100% 通过**

### Iter 3 (2026-05-20) — ✅ 完成
- ✅ `tests/test_conpty_keyboard.cpp` — 16 键盘输入测试
- ✅ `tests/test_conpty_e2e.cpp` — 7 E2E 测试 (启用)
- ✅ `tests/test_conpty_newline.cpp` — 3 换行跟踪测试
- ✅ **Bug修复**: `conpty.cpp` — `-Embedding` 模式 `width=0,height=0` → `screen_buffer_size={0,0}` → 光标永远不移动
  - 修复: `width > 0 ? width : 80`, `height > 0 ? height : 25`
- ✅ **Bug修复**: `libconpty.cpp` `CreateSignalPipe` — `bInheritHandle=FALSE` → `CreateProcessAsUserW` 失败 (ERROR_INVALID_PARAMETER)
  - 修复: `bInheritHandle=TRUE`

### 当前状态
**9 个测试目标, ~120 个测试用例, 100% 通过**

| 测试 | 测试数 | 结果 |
|------|--------|------|
| VT.Parser.Utf32 | ~18 | ✅ |
| Convert.Char | 27 | ✅ |
| ScreenBuffer | 21 | ✅ |
| ConsoleState | 24 | ✅ |
| VtMsgDispatch | 25 | ✅ |
| ConPTY.Keyboard | 16 | ✅ |
| CLI.CommandLine | ~5 | ✅ |

## 八、迭代计划

| 迭代 | 内容 | 测试数 | 状态 |
|------|------|--------|------|
| Iter 1 | char_convert + console_state + 测试框架 | 51 | ✅ 完成 |
| Iter 2 | screen_buffer + vt_msg_dispatch | 46 | ✅ 完成 |
| Iter 3 | conpty_keyboard + E2E框架 + GetModulePath修复 | 16 + 修复 | ✅ 完成 |
| Iter 4 | api_handlers + pipe_bridge + E2E完善 | ~83 | 📋 计划 |
| **总计** | | **~196** | 114 已完成 |
