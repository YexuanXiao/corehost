# ConPTY 分层模块架构设计

> 基于 `terminal/src/` 原始架构分析与现有 corehost 代码。
> 日期: 2026-05-18

---

## 一、设计目标

将当前单体的 `pty_forward_handler`（一个 struct 处理 8 种 ConDrv 消息 + 全部 L1/L2/L3 API + ReadConsole pending + 手动 echo）重构为分层模块架构：

1. **每层有明确的接口和职责边界**
2. **不同层通过接口通信，隔离无关状态**
3. **模块可独立测试、替换**
4. **对标原始 OpenConsole 的分层结构，但保持 corehost 的轻量级定位**

---

## 二、分层总览

```
┌─────────────────────────────────────────────────────────────┐
│                    Layer 0: miniio                           │
│  ConDrv IOCTL 原语、双缓冲 io_msg、事件循环模板              │
│  (io_thread.hpp / io_loop.hpp — 已存在, 保持不动)            │
└────────────────────┬────────────────────────────────────────┘
                     │ io_msg&, Handler 接口
┌────────────────────▼────────────────────────────────────────┐
│                 Layer 1: Message Router                      │
│  按 Function 值 (0x01~0x08) 路由到对应的子系统               │
│  (新增: conpty_message_router.hpp)                           │
└──┬──────┬──────┬──────┬──────┬──────┬──────┬──────┬────────┘
   │      │      │      │      │      │      │      │
   ▼      ▼      ▼      ▼      ▼      ▼      ▼      ▼
┌──────┐┌────┐┌────┐┌────┐┌────┐┌────┐┌──────┐┌──────┐
│CONNECT││DISC││CREAT││CLOSE││RAW_ ││RAW_ ││USER_  ││RAW_  │
│      ││    ││E_OBJ││_OBJ ││WRITE││READ ││DEFINED││FLUSH │
└──┬───┘└──┬─┘└──┬──┘└──┬──┘└──┬──┘└──┬──┘└───┬───┘└──┬───┘
   │       │     │     │     │     │      │        │
   ▼       ▼     ▼     ▼     ▼     ▼      ▼        ▼
┌──────────────────────────────────────────────────────────────┐
│              Layer 2: Console Subsystems                     │
│                                                              │
│  ┌─────────────────┐  ┌────────────────┐  ┌───────────────┐ │
│  │ I/O State        │  │ Pipe Bridge     │  │ API Dispatcher│ │
│  │ (io_state)       │  │ (pipe_bridge)   │  │ (api_router)  │ │
│  │                  │  │                 │  │               │ │
│  │ \Input/\Output   │  │ vt_out 写入     │  │ L1/L2/L3      │ │
│  │ 句柄管理         │  │ vt_in 读取      │  │ 分发表        │ │
│  │ DISCONNECT 清理  │  │ ReadConsole     │  │               │ │
│  └─────────────────┘  │ pending/echo    │  └───────┬───────┘ │
│                       └────────────────┘          │         │
└───────────────────────────────────────────────────┼─────────┘
                                                    │
                     ┌──────────────────────────────┘
                     ▼
┌──────────────────────────────────────────────────────────────┐
│              Layer 3: API Handlers                           │
│                                                              │
│  每个活跃 API 一个 handler:                                   │
│  ┌──────────┐ ┌──────────┐ ┌──────────┐ ┌──────────────┐   │
│  │ GetCP    │ │ GetMode  │ │ SetMode  │ │ ReadConsole  │   │
│  │ handler  │ │ handler  │ │ handler  │ │ handler      │   │
│  └──────────┘ └──────────┘ └──────────┘ └──────────────┘   │
│  ┌──────────┐ ┌──────────┐ ┌──────────┐ ┌──────────────┐   │
│  │ WriteCon │ │ FillOut  │ │ CtrlEvt  │ │ GetSBInfo    │   │
│  │ handler  │ │ handler  │ │ handler  │ │ handler      │   │
│  └──────────┘ └──────────┘ └──────────┘ └──────────────┘   │
│  ... (共约 15 个活跃 handler)                                 │
└──────────────────────────────────────────────────────────────┘
                     │
                     ▼
┌──────────────────────────────────────────────────────────────┐
│              Layer 4: Console State                          │
│                                                              │
│  ┌──────────────────────────────────────────────────────┐   │
│  │ console_state (轻量级状态, 无 screen buffer)           │   │
│  │                                                       │   │
│  │ input_mode, output_mode, CP, OutputCP                 │   │
│  │ cursor_size, cursor_visible, cursor_pos               │   │
│  │ screen_buffer_size, window_size, max_window_size      │   │
│  │ title, popup_attributes, default_attributes           │   │
│  │ color_table[16]                                       │   │
│  │ lang_id                                               │   │
│  └──────────────────────────────────────────────────────┘   │
└──────────────────────────────────────────────────────────────┘
```

---

## 三、模块接口定义

### 3.1 Layer 0: miniio（已存在，无需修改）

```
当前文件:
  src/miniio/io_thread.hpp  — io_msg, io_handles, read_io, complete_io, accept_connection
  src/miniio/io_loop.hpp    — run_io_loop, run_io_loop_no_setup (模板)
  src/miniio/signal.hpp     — signal_thread_params, signal_thread_proc
  src/miniio/signal.cpp     — 信号管道线程实现

Handler 接口要求 (由 run_io_loop 模板制定):
  bool on_connect(io_msg &msg)
  bool on_message(io_msg &msg)  // true=已完成, false=挂起
  void on_idle()
  bool has_pending() const
  bool should_exit() const
```

### 3.2 Layer 1: Message Router

```cpp
// ── conpty_message_router.hpp ────────────────────────────
// 职责: 按 Function 值路由 io_msg 到对应的子系统。
// 不处理任何业务逻辑, 不持有业务状态。

namespace conpty {

struct conpty_message_router {
    // ── 子系统引用 (由调用方注入) ──
    io_state*        io;         // I/O 句柄管理
    pipe_bridge*     bridge;     // PTY 管道桥接
    api_router*      api;        // Console API 分派

    // ── io_loop handler 接口 ──
    bool on_connect(io_msg &msg);
    bool on_message(io_msg &msg);   // → dispatch()
    void on_idle();                  // → bridge->on_idle()
    bool has_pending() const;        // → bridge->has_pending()
    bool should_exit() const;        // → bridge->should_exit() && !io->has_active_connection()

private:
    bool dispatch(io_msg &msg);      // 核心路由
};

// ── 路由表 ──
// dispatch() 实现:
bool conpty_message_router::dispatch(miniio::io_msg &msg) {
    switch (msg.descriptor.Function) {
    case CONSOLE_IO_CONNECT:      return io->handle_connect(msg);
    case CONSOLE_IO_DISCONNECT:   return io->handle_disconnect(msg);
    case CONSOLE_IO_CREATE_OBJECT: return io->handle_create_object(msg);
    case CONSOLE_IO_CLOSE_OBJECT:  return io->handle_close_object(msg);
    case CONSOLE_IO_RAW_WRITE:    return bridge->handle_raw_write(msg);
    case CONSOLE_IO_RAW_READ:     return bridge->handle_raw_read(msg);
    case CONSOLE_IO_USER_DEFINED: return api->handle_user_defined(msg);
    case CONSOLE_IO_RAW_FLUSH:    return true;  // minio::prepare_completion
    default: std::unreachable();
    }
}

} // namespace conpty
```

### 3.3 Layer 2: I/O State

```cpp
// ── conpty_io_state.hpp ──────────────────────────────────
// 职责: 管理 \Input/\Output 客户句柄, 处理 CONNECT/DISCONNECT/
//       CREATE_OBJECT/CLOSE_OBJECT。
// 对标原始: IoDispatchers.cpp 的部分功能

namespace conpty {

struct io_state {
    win32::handle_view server;     // ConDrv \Server 句柄 (非拥有)
    miniio::io_handles handles;    // \Input / \Output 句柄

    // ── CONNECT ───────────────────────────────────────
    bool handle_connect(miniio::io_msg &msg);
    // → accept_connection(server, msg)
    // → 填充 handles
    // → 返回 true

    // ── DISCONNECT ────────────────────────────────────
    bool handle_disconnect(miniio::io_msg &msg);
    // → handles.input.clear()
    // → handles.output.clear()
    // → miniio::prepare_completion(msg)
    // → 返回 true

    // ── CREATE_OBJECT ─────────────────────────────────
    bool handle_create_object(miniio::io_msg &msg);
    // → 读取 CD_CREATE_OBJECT_INFORMATION
    // → GENERIC 映射: GENERIC_READ→CURRENT_INPUT, GENERIC_WRITE→CURRENT_OUTPUT
    // → condrv::create_client_handle(server, L"\\Input" | L"\\Output")
    // → miniio::prepare_completion(msg, 0, handle_value)
    // → 返回 true

    // ── CLOSE_OBJECT ──────────────────────────────────
    bool handle_close_object(miniio::io_msg &msg);
    // → miniio::prepare_completion(msg)
    // → 返回 true

    // ── 查询 ──────────────────────────────────────────
    bool has_active_connection() const;
    // → handles.input.valid()
};

} // namespace conpty
```

### 3.4 Layer 2: Pipe Bridge

```cpp
// ── conpty_pipe_bridge.hpp ───────────────────────────────
// 职责: PTY 管道桥接 — VT 输出写入和 VT 输入读取。
// 对标原始: VtIo + VtInputThread (合并为单线程非阻塞模型)
//
// 核心状态机:
//   - _vt_eof: VT 管道已断开
//   - _pending_active: ReadConsole 挂起中
//   - _raw_read_total: 跨 idle 周期累积的原始字节数
//
// 关键约束:
//   - IOCTL_READ_IO 在 ConDrv 有 pending ReadConsole 时阻塞
//   - 因此 pending 期间必须在 on_idle 中自旋, 不能回到 read_io

namespace conpty {

// ── 接口: IReadConsoleSink ────────────────────────────
// pipe_bridge 在累积到完整行后通过此接口完成 ReadConsole。
// 从 pipe_bridge 中分离出 completion 逻辑, 解耦对 CD_IO_COMPLETE 的依赖。
struct IReadConsoleSink {
    virtual ~IReadConsoleSink() = default;
    virtual void on_line_ready(const void* data, DWORD bytes, bool is_unicode) = 0;
    virtual void on_eof() = 0;
};

struct pipe_bridge {
    // ── 管道句柄 ──
    win32::handle_view vt_in;    // ReadFile ← WT writes
    win32::handle_view vt_out;   // WriteFile → WT reads

    // ── 依赖注入 ──
    IReadConsoleSink* read_sink = nullptr;  // ReadConsole 完成回调

    // ── RAW_WRITE ─────────────────────────────────────
    // 对标: VtIo::Writer (简化版, 纯字节转码)
    bool handle_raw_write(miniio::io_msg &msg);
    // → 读取 CONSOLE_WRITECONSOLE_MSG
    // → ANSI: MultiByteToWideChar(CP_ACP) → WideCharToMultiByte(CP_UTF8) → WriteFile(vt_out)
    // → UTF-16: WideCharToMultiByte(CP_UTF8) → WriteFile(vt_out)
    // → miniio::prepare_completion
    // → 返回 true

    // ── RAW_READ ──────────────────────────────────────
    // 非阻塞: PeekNamedPipe → 无数据立即返回 0
    bool handle_raw_read(miniio::io_msg &msg);
    // → 读取 CONSOLE_READCONSOLE_MSG
    // → 非阻塞 PeekNamedPipe → 读至换行 → finalize_read
    // → miniio::prepare_completion
    // → 返回 true

    // ── on_idle (ReadConsole pending 累积) ────────────
    // 对标: VtInputThread::_Run (非阻塞等效)
    void on_idle();
    // 完整逻辑见下节 "ReadConsole 挂起路径"

    // ── pending 管理 ──
    bool has_pending() const;
    bool should_exit() const;

    // ── 启动 ReadConsole pending ──
    void start_read_console(DWORD exe_name_length, bool unicode);
    // → 保存 exe_name_length / unicode
    // → _pending_active = true
    // → _raw_read_total = 0

    void flush_accumulated();  // → 调用 read_sink->on_line_ready()

    // ── 文字编码 ──
    static DWORD finalize_read(bool uni, BYTE* dst, DWORD maxd,
                                const BYTE* src, DWORD src_len);

private:
    // ── 读取缓冲区 ──
    BYTE _readbuf[4096];
    DWORD _raw_read_total = 0;       // 当前累积字节数

    // ── ReadConsole pending 状态 ──
    bool _pending_active = false;
    bool _pending_unicode = false;
    DWORD _pending_exe_name_length = 0;

    // ── VT pipe 状态 ──
    bool _vt_eof = false;
};

} // namespace conpty
```

### 3.5 Layer 2: API Router

```cpp
// ── conpty_api_router.hpp ────────────────────────────────
// 职责: 解析 CONSOLE_MSG_HEADER.ApiNumber, 分派到对应的 API handler。
// 对标原始: ApiSorter.cpp (api 表驱动分派)
//
// 设计:
//   使用分发表 (同原始 ApiSorter), 每个活跃 API 一个 handler 函数。
//   废弃 API (deprecated) 统一返回空成功。
//   持有对 console_state 的引用, 供 handler 读写。

namespace conpty {

// ── API handler 函数签名 ──
using api_handler_fn = bool (*)(miniio::io_msg &msg, console_state &state);

// ── API 描述符 (对标 CONSOLE_API_DESCRIPTOR) ──
struct api_descriptor {
    api_handler_fn handler;
    DWORD required_size;   // 结构体最小大小 (用于验证)
};

// ── API 分发表 (对标 ConsoleApiLayerTable) ──
struct api_layer_table {
    const api_descriptor* entries;
    DWORD count;
};

struct api_router {
    console_state* state;  // 注入状态

    // ── USER_DEFINED 入口 ──
    bool handle_user_defined(miniio::io_msg &msg);
    // → 读取 msg.body 中的 CONSOLE_MSG_HEADER
    // → Layer = ApiNumber >> 24
    // → Index = ApiNumber & 0xFFFFFF
    // → 查表 → entries[Index].handler(msg, *state)
    // → 若 handler 返回 false → 挂起 (目前仅 ReadConsole)
    // → 若 handler 返回 true → 完成

    // ── 分发表 ──
    static const api_layer_table L1_table;  // 10 entries
    static const api_layer_table L2_table;  // 22 entries
    static const api_layer_table L3_table;  // 45 entries

    static const api_layer_table* tables[3];  // {&L1, &L2, &L3}
};

} // namespace conpty
```

### 3.6 Layer 3: API Handler

每个 API handler 是独立函数, 只访问 `io_msg` 和 `console_state`:

```cpp
// ── conpty_api_handlers.hpp ─────────────────────────────

namespace conpty {

// L1 handlers
bool api_get_cp(miniio::io_msg &msg, console_state &state);
bool api_get_mode(miniio::io_msg &msg, console_state &state);
bool api_set_mode(miniio::io_msg &msg, console_state &state);
bool api_get_num_input(miniio::io_msg &msg, console_state &state);
bool api_get_console_input(miniio::io_msg &msg, console_state &state);
bool api_read_console(miniio::io_msg &msg, console_state &state);  // ★ 挂起路径
bool api_write_console(miniio::io_msg &msg, console_state &state);
bool api_get_langid(miniio::io_msg &msg, console_state &state);

// L2 handlers
bool api_fill_output(miniio::io_msg &msg, console_state &state);
bool api_ctrl_event(miniio::io_msg &msg, console_state &state);
bool api_set_active_sb(miniio::io_msg &msg, console_state &state);
bool api_flush_input(miniio::io_msg &msg, console_state &state);
bool api_set_cp(miniio::io_msg &msg, console_state &state);
bool api_get_cursor(miniio::io_msg &msg, console_state &state);
bool api_set_cursor(miniio::io_msg &msg, console_state &state);
bool api_get_sb_info(miniio::io_msg &msg, console_state &state);
bool api_set_sb_info(miniio::io_msg &msg, console_state &state);
bool api_set_sb_size(miniio::io_msg &msg, console_state &state);
bool api_set_cursor_pos(miniio::io_msg &msg, console_state &state);
bool api_largest_window(miniio::io_msg &msg, console_state &state);
bool api_scroll_sb(miniio::io_msg &msg, console_state &state);
bool api_set_text_attr(miniio::io_msg &msg, console_state &state);
bool api_set_window_info(miniio::io_msg &msg, console_state &state);
bool api_read_output_string(miniio::io_msg &msg, console_state &state);
bool api_write_input(miniio::io_msg &msg, console_state &state);
bool api_write_output(miniio::io_msg &msg, console_state &state);
bool api_write_output_string(miniio::io_msg &msg, console_state &state);
bool api_read_output(miniio::io_msg &msg, console_state &state);
bool api_get_title(miniio::io_msg &msg, console_state &state);
bool api_set_title(miniio::io_msg &msg, console_state &state);

// L3 handlers (仅活跃部分)
bool api_get_mouse_info(miniio::io_msg &msg, console_state &state);
bool api_get_font_size(miniio::io_msg &msg, console_state &state);
bool api_get_current_font(miniio::io_msg &msg, console_state &state);
bool api_set_display_mode(miniio::io_msg &msg, console_state &state);
bool api_get_display_mode(miniio::io_msg &msg, console_state &state);
bool api_add_alias(miniio::io_msg &msg, console_state &state);
bool api_get_alias(miniio::io_msg &msg, console_state &state);
bool api_get_aliases_length(miniio::io_msg &msg, console_state &state);
bool api_get_alias_exes_length(miniio::io_msg &msg, console_state &state);
bool api_get_aliases(miniio::io_msg &msg, console_state &state);
bool api_get_alias_exes(miniio::io_msg &msg, console_state &state);
bool api_expunge_history(miniio::io_msg &msg, console_state &state);
bool api_set_num_commands(miniio::io_msg &msg, console_state &state);
bool api_get_history_length(miniio::io_msg &msg, console_state &state);
bool api_get_history(miniio::io_msg &msg, console_state &state);
bool api_get_console_window(miniio::io_msg &msg, console_state &state);
bool api_get_selection_info(miniio::io_msg &msg, console_state &state);
bool api_get_process_list(miniio::io_msg &msg, console_state &state);
bool api_set_current_font(miniio::io_msg &msg, console_state &state);

// deprecated handler: 完成但返回空成功
bool api_deprecated(miniio::io_msg &msg, console_state &state);

} // namespace conpty
```

### 3.7 Layer 4: Console State

```cpp
// ── conpty_console_state.hpp ─────────────────────────────
// 职责: 维护控制台状态 (对标 CONSOLE_INFORMATION + SCREEN_INFORMATION 中
//        corehost 需要的子集)。
//
// 对标原始:
//   input_mode / output_mode  → CONSOLE_INFORMATION / InputBuffer::InputMode
//   CP / OutputCP             → CONSOLE_INFORMATION
//   cursor_*                  → SCREEN_INFORMATION / Cursor
//   screen_buffer_*           → SCREEN_INFORMATION / Viewport / TextBuffer::GetSize
//   title                     → CONSOLE_INFORMATION::_Title
//   popup_attributes          → SCREEN_INFORMATION::_PopupAttributes
//   color_table               → SCREEN_INFORMATION / TextAttribute
//
// 注意: corehost 不维护 TextBuffer / ScreenBuffer / 渲染器。
//       仅维护 API 查询所需的最小状态。

namespace conpty {

struct cursor_state {
    ULONG  size = 25;          // 1-100 (百分比)
    bool   visible = true;
    COORD  position{0, 0};
};

struct console_state {
    // ── 模式 (对标 GetConsoleMode / SetConsoleMode) ──
    DWORD input_mode  = ENABLE_PROCESSED_INPUT | ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT;
    DWORD output_mode = ENABLE_PROCESSED_OUTPUT | ENABLE_WRAP_AT_EOL_OUTPUT;

    // ── 代码页 (对标 GetConsoleCP / SetConsoleCP) ──
    UINT input_code_page  = 0;   // GetACP()
    UINT output_code_page = 0;   // GetOEMCP()

    // ── 光标 ──
    cursor_state cursor;

    // ── Screen Buffer 信息 (对标 GetConsoleScreenBufferInfo) ──
    COORD screen_buffer_size{80, 25};
    COORD current_window_size{80, 25};
    COORD max_window_size{80, 25};
    WORD  default_attributes = 0x07;  // FOREGROUND_RED|GREEN|BLUE
    WORD  popup_attributes = 0x07;
    COLORREF color_table[16] = {}; // 标准 16 色

    // ── 标题 ──
    wchar_t title[256] = {};

    // ── 语言 ID ──
    LANGID lang_id = 0;  // GetSystemDefaultLangID()

    // ── 初始化 ──
    console_state();
};

} // namespace conpty
```

---

## 四、ReadConsole 挂起路径（跨模块协作）

这是整个架构中最复杂的跨层协作场景, 需要 `api_router` → `pipe_bridge` → `completion bridge` 三部分协作。

### 4.1 协作图

```
api_read_console(msg, state)
  │
  │ (1) 保存 CONSOLE_READCONSOLE_MSG 参数到 pipe_bridge
  │ (2) pipe_bridge::start_read_console(exe_name_length, unicode)
  │ (3) 保存 Identifier 到 _pending_id
  │ (4) return false → io_loop 进入自旋
  │
  ▼
io_loop: while(has_pending()) on_idle()
  │
  ▼
pipe_bridge::on_idle()
  │
  ├─ PeekNamedPipe(vt_in) 失败 → _vt_eof=true → read_sink->on_eof()
  │
  ├─ PeekNamedPipe → avail > 0 → ReadFile → 累积到 _readbuf
  │   ├─ 扫描到 \r/\n → 手动 echo 到 vt_out → read_sink->on_line_ready()
  │   └─ 未找到换行 → echo 新字节 → 返回 (下轮 idle 继续)
  │
  └─ avail == 0 → 返回
```

### 4.2 Completion Bridge（连接 pipe_bridge 和 api_router）

```cpp
// ── conpty_completion_bridge.hpp ─────────────────────────
// 职责: pipe_bridge 的 IReadConsoleSink 实现。
//       pipe_bridge 累积到完整行后通过此桥完成 CD_IO_COMPLETE。
//
// 为什么需要这个桥:
//   - pipe_bridge 不依赖 CD_IO_COMPLETE / io_msg 结构
//   - api_router 不依赖 PTY 管道
//   - 桥接两者, 保持各层独立可测试

namespace conpty {

struct completion_bridge : IReadConsoleSink {
    win32::handle_view server;   // 用于 complete_io
    ULONG64           pending_id = 0;
    BYTE              pending_outbuf[sizeof(CONSOLE_READCONSOLE_MSG) + 8192];

    // IReadConsoleSink 实现
    void on_line_ready(const void* data, DWORD bytes, bool is_unicode) override;
    void on_eof() override;

private:
    void do_complete(ULONG data_bytes);
};

void completion_bridge::on_line_ready(const void* data, DWORD bytes, bool is_unicode) {
    auto* req = reinterpret_cast<CONSOLE_READCONSOLE_MSG*>(pending_outbuf);
    // 从 data 复制数据到 pending_outbuf + sizeof(CONSOLE_READCONSOLE_MSG)
    // 设置 req->NumBytes
    do_complete(bytes);
}

void completion_bridge::on_eof() {
    auto* req = reinterpret_cast<CONSOLE_READCONSOLE_MSG*>(pending_outbuf);
    req->NumBytes = 0;
    do_complete(0);
}

void completion_bridge::do_complete(ULONG data_bytes) {
    auto sz = sizeof(CONSOLE_READCONSOLE_MSG) + data_bytes;
    CD_IO_COMPLETE comp{};
    comp.Identifier.LowPart  = static_cast<ULONG>(pending_id);
    comp.Identifier.HighPart = static_cast<LONG>(pending_id >> 32);
    comp.IoStatus.Status = 0;
    comp.IoStatus.Information = sz;
    comp.Write.Data = pending_outbuf;
    comp.Write.Size = sz;
    miniio::complete_io(server, comp);
}

} // namespace conpty
```

### 4.3 ReadConsole API handler

```cpp
// api_read_console 在 api_router 的 L1 分发表中 (index 5)

bool api_read_console(miniio::io_msg &msg, console_state &state) {
    auto* req = reinterpret_cast<CONSOLE_READCONSOLE_MSG*>(
        msg.body + sizeof(CONSOLE_MSG_HEADER));

    bool uni = req->Unicode != 0;

    // ★ VT pipe 已断开: 立即返回 0 字节
    if (state.pipe_bridge && state.pipe_bridge->should_exit()) {
        req->NumBytes = 0;
        miniio::prepare_completion(msg, 0, 0);
        msg.complete.Write.Data = msg.body + sizeof(CONSOLE_MSG_HEADER);
        msg.complete.Write.Size = sizeof(CONSOLE_READCONSOLE_MSG);
        return true;
    }

    // ★ 保存 Identifier 到 completion_bridge
    ULONG64 id = static_cast<ULONG64>(msg.descriptor.Identifier.LowPart) |
                 (static_cast<ULONG64>(msg.descriptor.Identifier.HighPart) << 32);
    state.completion->pending_id = id;
    std::memcpy(state.completion->pending_outbuf, req, sizeof(CONSOLE_READCONSOLE_MSG));

    // ★ 启动 pending
    state.pipe_bridge->start_read_console(req->ExeNameLength, uni);

    return false;  // 挂起
}
```

---

## 五、文件组织结构

```
src/conpty/
├── conpty_message_router.hpp   # Layer 1: 消息路由器
├── conpty_io_state.hpp         # Layer 2: I/O 句柄管理
├── conpty_pipe_bridge.hpp      # Layer 2: PTY 管道桥接
├── conpty_api_router.hpp       # Layer 2: API 分发表
├── conpty_completion_bridge.hpp # Layer 2/3: ReadConsole completion 桥
├── conpty_api_handlers.hpp     # Layer 3: L1/L2/L3 API handler 函数
├── conpty_console_state.hpp    # Layer 4: 控制台状态
└── conpty_entry.hpp            # 入口: conpty_entry() 组装所有模块

src/miniio/                      # Layer 0 (保持不变)
├── io_thread.hpp
├── io_loop.hpp
├── signal.hpp
└── signal.cpp
```

---

## 六、conpty_entry 组装

```cpp
// ── conpty_entry.hpp (重构后) ──
// 对标: srvinit.cpp 中的 ConsoleCreateIoThread + VtIo::Initialize

inline void conpty_entry(win32::handle server, win32::handle vt_in,
                         win32::handle vt_out, win32::handle event,
                         short width, short height, bool inherit_cursor,
                         text_measurement_mode text_measurement,
                         miniio::io_handles handles = {})
{
    // ── 构造模块 ──
    console_state state;
    state.screen_buffer_size = {width, height};
    state.cursor.position = {0, 0};

    io_state io{server.view()};
    io.handles = std::move(handles);

    completion_bridge completion{server.view()};

    pipe_bridge bridge{vt_in.view(), vt_out.view(), &completion};
    bridge.set_console_state(&state); // 用于 api_write_console 等需要编码信息

    api_router api{&state, &bridge, &completion};

    conpty_message_router router{&io, &bridge, &api};

    // ── 进入 I/O 循环 ──
    miniio::run_io_loop_no_setup(server.view(), event.view(), router);
}
```

---

## 七、对比: 原始 vs 当前 vs 新设计

| 关注点 | 原始 OpenConsole | 当前 corehost | 新设计 |
|--------|-----------------|--------------|--------|
| **状态管理** | CONSOLE_INFORMATION (30+ 字段) + SCREEN_INFORMATION + TextBuffer | pty_forward_handler (散落字段) | console_state (集中, 仅必要字段) |
| **消息路由** | IoSorter + switch | pty_forward_handler::on_message switch | conpty_message_router::dispatch switch |
| **API 分派** | ApiSorter (表驱动) | 内联 if-else 链 | api_router (表驱动, 对标原始) |
| **ReadConsole** | ApiDispatchers + COOKED_READ_DATA + VtInputThread | on_idle() 内联累积 | pipe_bridge + completion_bridge 分离 |
| **PTY 输出** | VtIo::Writer (VT 序列生成) | raw_write() 纯转码 | pipe_bridge::handle_raw_write |
| **信号线程** | PtySignalInputThread | signal_thread_proc | signal_thread_proc (不变) |
| **测试能力** | 困难 (全局状态) | 困难 (单体 struct) | ★ 每层可独立注入依赖测试 |

---

## 八、实施阶段

### Phase 1: 提取 console_state
- 创建 `conpty_console_state.hpp`
- 将所有分散在 `pty_forward_handler` 中的状态字段集中

### Phase 2: 分离 io_state
- 创建 `conpty_io_state.hpp`
- 移动 CONNECT/DISCONNECT/CREATE_OBJECT/CLOSE_OBJECT 处理

### Phase 3: 分离 pipe_bridge
- 创建 `conpty_pipe_bridge.hpp` + `conpty_completion_bridge.hpp`
- 移动 on_idle / raw_write / raw_read / ReadConsole pending

### Phase 4: 分离 api_router + handlers
- 创建 `conpty_api_router.hpp` + `conpty_api_handlers.hpp`
- 将 L1/L2/L3 处理提取为独立函数 + 表驱动

### Phase 5: 组装
- 重构 `conpty_entry.hpp` 为组装器
- 删除旧的 `pty_forward_handler`

---

## 九、API Handler 实现指南

每个 handler 的模式:

```cpp
// ── 模式 1: 简单查询 (立即完成, 返回 true) ──
bool api_get_cp(miniio::io_msg &msg, console_state &state) {
    auto* r = reinterpret_cast<CONSOLE_GETCP_MSG*>(
        msg.body + sizeof(CONSOLE_MSG_HEADER));
    r->CodePage = state.input_code_page;
    ucomplete_sz(msg, sizeof(CONSOLE_GETCP_MSG));
    return true;
}

// ── 模式 2: 状态修改 (立即完成, 返回 true) ──
bool api_set_mode(miniio::io_msg &msg, console_state &state) {
    auto* r = reinterpret_cast<CONSOLE_MODE_MSG*>(
        msg.body + sizeof(CONSOLE_MSG_HEADER));
    state.input_mode = r->Mode;
    state.output_mode = r->Mode;
    ucomplete(msg);
    return true;
}

// ── 模式 3: 输出转发 (立即完成, 返回 true) ──
bool api_write_console(miniio::io_msg &msg, console_state &state) {
    auto* req = reinterpret_cast<CONSOLE_WRITECONSOLE_MSG*>(
        msg.body + sizeof(CONSOLE_MSG_HEADER));
    // ... 转码 → WriteFile(vt_out) → ...
    ucomplete_sz(msg, sizeof(CONSOLE_WRITECONSOLE_MSG));
    return true;
}

// ── 模式 4: 挂起 (返回 false, 由 io_loop 自旋) ──
bool api_read_console(miniio::io_msg &msg, console_state &state) {
    // ... 保存状态, 启动 pending ...
    return false;  // ★ 唯一返回 false 的 handler
}
```

---

*本文档定义了从单体 handler 到分层模块架构的完整设计。每层有明确接口、单一职责、可独立测试。*
