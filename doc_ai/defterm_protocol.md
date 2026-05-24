# 默认终端移交协议 — 完整执行轨迹

## 触发条件

CSRSS 启动 `corehost 0x<HANDLE>`。命令行解析：跳过程序名（argv[0]），第一个以 `0x` 或 `0X` 开头的 token 解析为 `uintptr_t` 类型的十六进制数值。句柄值在 x64/ARM64 上为 64 位。`wWinMain` 中 `args.condrv_handle()` 获得该值后调用 `defterm_entry(ch)`。

---

## defterm_entry 完整执行

```c
void defterm_entry(std::uintptr_t condrv_handle) {
    // 步骤 D1 — 将 uintptr_t 转为 handle_view（非拥有引用）
    auto server = win32::handle_view::from_uintptr(condrv_handle);

    // 步骤 D2 — 创建事件
    auto ev = win32::event{win32::create_tag, true, false};
    // → CreateEventW(nullptr, TRUE, FALSE, nullptr)
    //   bManualReset = TRUE  (SetEvent 后保持有信号，多客户端并发不丢唤醒)
    //   bInitialState = FALSE (初始无信号)

    // 步骤 D3 — 向驱动注册事件
    connect_handler handler{};
    handler.server = server;
    handler.ev = ev.view();

    // 步骤 D4 — 进入双缓冲 I/O 循环
    miniio::run_io_loop(server, ev.view(), handler);
    // 阻塞直到所有客户端断开或移交成功
}
```

---

## ConDrv IOCTL 完整定义

所有 IOCTL 代码通过 `CTL_CODE(FILE_DEVICE_CONSOLE, function, method, access)` 计算。

```
FILE_DEVICE_CONSOLE = 0x00000032
FILE_ANY_ACCESS     = 0x00000000

IOCTL_READ_IO      = CTL_CODE(0x32, 1, METHOD_OUT_DIRECT,   0) = 0x500004
IOCTL_COMPLETE_IO  = CTL_CODE(0x32, 2, METHOD_NEITHER,      0) = 0x500008
IOCTL_SET_SERVER   = CTL_CODE(0x32, 7, METHOD_NEITHER,      0) = 0x50001C
```

### io_msg 内存布局

```c
struct io_msg {
    CD_IO_COMPLETE   complete;       // 偏移 0,  大小由驱动定义
    CD_IO_DESCRIPTOR descriptor;     // 偏移 N,  36 字节 (x64)
    BYTE             body[4096];     // 偏移 N+36, 4096 字节
};
```

双缓冲：循环使用两个 `io_msg`（`msgA`/`msgB`），交替作为当前消息和上一轮完成消息。这保证 `prev->complete.Write.Data` 不会与当前消息的 `descriptor`/`body` 重叠。

---

## 执行轨迹：set_server_info（步骤 D3 内部）

`run_io_loop` 在进入循环前调用：

```c
void set_server_info(win32::handle_view server, win32::handle_view event) {
    CD_IO_SERVER_INFORMATION info{};
    info.InputAvailableEvent = event.get();  // ManualReset 事件句柄

    DWORD r = 0;
    DeviceIoControl(
        server.get(),       // ConDrv Server 句柄
        IOCTL_SET_SERVER,   // 0x50001C
        &info,              // lpInBuffer = &{event}
        sizeof(info),       // nInBufferSize = 8
        nullptr,            // lpOutBuffer
        0,                  // nOutBufferSize
        &r,                 // lpBytesReturned
        nullptr             // lpOverlapped
    );
    // 失败不致命 — COM marshal 句柄可能不支持(err=22)或已注册(err=183)
}
```

---

## 执行轨迹：run_io_loop 主循环

```c
template <typename Handler>
void run_io_loop(win32::handle_view server, win32::handle_view ev, Handler &handler) {
    set_server_info(server, ev);     // 见上节

    io_msg msgA{}, msgB{};           // 两个 4KB+ 缓冲区零初始化
    io_msg *cur = &msgA;             // 当前消息缓冲区
    io_msg *prev_msg = nullptr;      // 上一轮消息指针

    for (;;) {
        // ── 读消息 ──
        CD_IO_COMPLETE *prev = prev_msg ? &prev_msg->complete : nullptr;
        // 首次迭代: prev = nullptr
        // 后续迭代: prev = 指向上一轮的 complete 字段

        if (!read_io(server, ev, prev, *cur))
            break;  // 客户端断开

        // ── 分支 ──
        if (cur->descriptor.Function != CONSOLE_IO_CONNECT) {  // != 0x01
            handler.on_message(*cur);       // → dispatch_non_connect
            prev_msg = cur;                 // 标记此消息为下一轮的 prev
            cur = (cur == &msgA) ? &msgB : &msgA;  // 交换缓冲区
            continue;
        }

        // CONNECT 消息 (Function == 0x01)
        if (!handler.on_connect(*cur))      // → handle_connect
            return;                         // false = 移交成功，退出循环

        prev_msg = nullptr;                  // CONNECT 后清除 prev
        cur = (cur == &msgA) ? &msgB : &msgA;
    }
}
```

**关键**：CONNECT 消息后 `prev_msg = nullptr` — 因为 `accept_connection` 内部已完成 `complete_io`（见下），下一轮不需要再提交完成消息。

---

## 执行轨迹：read_io

```c
bool read_io(win32::handle_view server, win32::handle_view event,
             CD_IO_COMPLETE *prev, io_msg &msg) {

    memset(&msg.descriptor, 0, sizeof(msg.descriptor));  // 只清 descriptor
    // 不清 body — 若 prev->Write.Data 指向上一轮的 msg.body，
    // 驱动可能仍在使用其内容

    DWORD r = 0;
    BOOL ok = DeviceIoControl(
        server.get(),
        IOCTL_READ_IO,                  // 0x500004
        prev,                           // lpInBuffer — 上一轮的完成消息 或 NULL
        prev ? sizeof(CD_IO_COMPLETE) : 0,  // nInBufferSize
        &msg.descriptor,                // lpOutBuffer — 驱动将描述符+body写入此处
        sizeof(msg.descriptor) + sizeof(msg.body),  // nOutBufferSize = 36+4096
        &r,                             // lpBytesReturned
        nullptr                         // lpOverlapped
    );

    if (!ok) {
        auto err = GetLastError();
        if (err == ERROR_IO_PENDING) {       // 997
            WaitForSingleObject(event.get(), INFINITE);
            return true;                     // 下轮重试
        }
        if (err == ERROR_PIPE_NOT_CONNECTED  // 233
            || err == ERROR_BROKEN_PIPE       // 109
            || err == ERROR_NO_DATA) {        // 232
            return false;                    // 退出循环
        }
        throw win32::error(err);             // 意外错误
    }
    return true;
}
```

### 异步完成模型

非 CONNECT 消息不立即 `complete_io` — 而是填充 `msg.complete`，在下一轮 `read_io` 中作为 `lpInBuffer` 提交。驱动在读取新消息时同时处理上一轮的完成消息，用一个 `DeviceIoControl` 完成两个操作。

---

## 执行轨迹：dispatch_non_connect（defterm 路径中 handler.on_message）

`connect_handler::on_message` 委托到 `dispatch_non_connect`：

```c
void dispatch_non_connect(win32::handle_view server, io_msg &msg, io_handles &handles) {
    switch (msg.descriptor.Function) {

    case CONSOLE_IO_DISCONNECT:  // 0x02
        handles.input.clear();     // CloseHandle(Input) → 驱动释放客户端
        handles.output.clear();    // CloseHandle(Output)
        prepare_completion(msg);   // status=0, info=0, Write 清空
        break;

    case CONSOLE_IO_CREATE_OBJECT: {  // 0x03
        auto *req = reinterpret_cast<CD_CREATE_OBJECT_INFORMATION*>(msg.body);
        auto type = req->ObjectType;
        // GENERIC (0x04) — CreateFile("CONIN$")/CreateFile("CONOUT$")
        // 由内核映射为 CD_IO_OBJECT_TYPE_GENERIC，DesiredAccess 保留客户端原始意图
        if (type == CD_IO_OBJECT_TYPE_GENERIC) {  // 0x04
            if ((req->DesiredAccess & (GENERIC_READ|GENERIC_WRITE)) == GENERIC_READ)
                type = CD_IO_OBJECT_TYPE_CURRENT_INPUT;   // 0x01
            else if ((req->DesiredAccess & (GENERIC_READ|GENERIC_WRITE)) == GENERIC_WRITE)
                type = CD_IO_OBJECT_TYPE_CURRENT_OUTPUT;  // 0x02
        }

        win32::handle new_handle;
        switch (type) {
        case CD_IO_OBJECT_TYPE_CURRENT_INPUT:  // 0x01
            new_handle = condrv::create_client_handle(server, L"\\Input");
            // → NtOpenFile(RootDirectory=server, ObjectName=\Input, ...)
            break;
        case CD_IO_OBJECT_TYPE_CURRENT_OUTPUT: // 0x02
        case CD_IO_OBJECT_TYPE_NEW_OUTPUT:     // 0x03
            new_handle = condrv::create_client_handle(server, L"\\Output");
            break;
        default:
            prepare_completion(msg, 0xC0000001);  // STATUS_UNSUCCESSFUL
            return;
        }

        // 返回句柄值给客户端,客户端现在拥有此句柄
        prepare_completion(msg, 0, reinterpret_cast<ULONG_PTR>(new_handle.release()));
        break;
    }

    case CONSOLE_IO_CLOSE_OBJECT:  // 0x04
        prepare_completion(msg);   // 确认成功，无数据
        break;

    case CONSOLE_IO_RAW_WRITE:     // 0x05
        // mini console: 丢弃数据但确认写入，防止客户端阻塞
        prepare_completion(msg, 0, msg.descriptor.InputSize);
        break;

    case CONSOLE_IO_RAW_READ:      // 0x06
        // mini console: 返回 0 字节 EOF
        prepare_completion(msg);
        break;

    case CONSOLE_IO_USER_DEFINED:  // 0x07
        // mini console: 不支持任何 Win32 Console API
        prepare_completion(msg, 0xC0000001);  // STATUS_UNSUCCESSFUL
        break;

    case CONSOLE_IO_RAW_FLUSH:     // 0x08
        prepare_completion(msg);
        break;

    default:
        std::unreachable();
    }
    // 不调用 complete_io — 下一轮 read_io 将 msg.complete 作为输入提交
}
```

---

## 执行轨迹：prepare_completion

```c
CD_IO_COMPLETE &prepare_completion(io_msg &msg, LONG status = 0, ULONG_PTR info = 0) {
    msg.complete.Identifier           = msg.descriptor.Identifier;
    msg.complete.IoStatus.Status      = status;      // 0=成功, 0xC0000001=STATUS_UNSUCCESSFUL
    msg.complete.IoStatus.Information = info;        // 字节数或句柄值
    msg.complete.Write.Data           = nullptr;
    msg.complete.Write.Size           = 0;
    msg.complete.Write.Offset         = 0;
    return msg.complete;
}
```

---

## 执行轨迹：accept_connection

仅 CONNECT 消息调用。创建客户端句柄对并立即完成 I/O：

```c
io_handles accept_connection(win32::handle_view server, io_msg &msg) {
    // ── 创建客户端句柄对 ──
    auto in_h  = condrv::create_client_handle(server, L"\\Input");
    auto out_h = condrv::create_client_handle(server, L"\\Output");
    // 每个调用 → NtOpenFile(RootDirectory=server, ObjectName=\Input|\Output,
    //                         GENERIC_READ|GENERIC_WRITE|SYNCHRONIZE,
    //                         FILE_SHARE_READ|WRITE|DELETE,
    //                         FILE_SYNCHRONOUS_IO_NONALERT)

    // ── 填充连接信息 ──
    CD_CONNECTION_INFORMATION conn{};
    conn.Process = reinterpret_cast<ULONG_PTR>(in_h.get());   // 复用 Input
    conn.Input   = reinterpret_cast<ULONG_PTR>(in_h.get());   // 相同值
    conn.Output  = reinterpret_cast<ULONG_PTR>(out_h.get());

    // ── 准备并立即完成 ──
    prepare_completion(msg);
    msg.complete.IoStatus.Information = sizeof(CD_CONNECTION_INFORMATION);
    msg.complete.Write.Data = &conn;
    msg.complete.Write.Size = sizeof(CD_CONNECTION_INFORMATION);

    complete_io(server, msg.complete);
    // → DeviceIoControl(server, IOCTL_COMPLETE_IO, &msg.complete, sizeof(msg.complete),
    //                    nullptr, 0, &r, nullptr)

    return {std::move(in_h), std::move(out_h)};
}
```

**注意**：CONNECT 调用 `complete_io` 后立即返回，不通过异步模型延迟。因此事件循环中 `prev_msg = nullptr`。

---

## 执行轨迹：handle_connect 决策核心

这是 defterm 最关键的函数——决定走 COM 移交还是 mini console。

```c
bool handle_connect(win32::handle_view server, win32::handle_view ev,
                    io_msg &msg, bool &initialized, io_handles &handles) {

    // ── 提取客户端 PID ──
    DWORD client_pid = static_cast<DWORD>(msg.descriptor.Process);
    // descriptor.Process 为 ULONG_PTR (64位)，低 32 位为进程 ID

    // ── 决策 ──
    bool need_gui = !initialized
                 && should_attempt_handoff(
                        *reinterpret_cast<const CONSOLE_SERVER_MSG*>(msg.body))
                 && is_interactive_user_session();
    initialized = true;  // 首条 CONNECT 后置 true — 阻止 AttachConsole 重入移交

    // ── 分支 1: 非 GUI 路径（不可见/非交互/已初始化） ──
    if (!need_gui) {
        handles = accept_connection(server, msg);
        return false;  // false = 继续循环
    }

    // ── 分支 2: UAC 提升检测 ──
    if (env::is_elevated()) {
        // 逻辑:
        //   OpenProcessToken → TokenElevation 确认主令牌已提升
        //   TokenLinkedToken 确认链接令牌（原始 Medium IL）存在
        //   两者均成立 → High IL 进程
        //   UIPI 阻止向 Medium IL 终端传递句柄和 COM 激活
        env::show_elevated_message();  // MessageBox(L"请求被安全策略阻止")
        handles = accept_connection(server, msg);
        GenerateConsoleCtrlEvent(CTRL_BREAK_EVENT, client_pid);
        return false;
    }

    // ── 分支 3: GUI 路径 — 尝试 COM 移交 ──
    auto portable = make_portable_attach_msg(msg);
    // → 填充 CONSOLE_PORTABLE_ATTACH_MSG 的 6 个字段

    if (try_handoff_all(server, ev, portable, client_pid))
        return true;  // true = 退出循环，corehost 退出

    // ── 分支 4: 全部候选失败 ──
    env::show_not_found_message();  // MessageBox(L"缺少Windows终端")
    handles = accept_connection(server, msg);
    GenerateConsoleCtrlEvent(CTRL_BREAK_EVENT, client_pid);
    return false;
}
```

**返回值语义**：`true` → 退出事件循环（移交成功，corehost 退出）；`false` → 继续循环。

---

## 执行轨迹：should_attempt_handoff 过滤

```c
bool should_attempt_handoff(const CONSOLE_SERVER_MSG &msg) noexcept {
    // 检查 1: WindowVisible
    if (!msg.WindowVisible)          // 偏移 33, BOOLEAN
        return false;                // CREATE_NO_WINDOW — 进程不需要窗口

    // 检查 2: STARTF_USESHOWWINDOW + 隐藏/最小化
    if (msg.StartupFlags & STARTF_USESHOWWINDOW) {  // 偏移 8, ULONG, 位 0x0001
        int show = msg.ShowWindow;   // 偏移 14, USHORT
        if (show == SW_HIDE           // 0
            || show == SW_SHOWMINIMIZED  // 2
            || show == SW_MINIMIZE       // 6
            || show == SW_SHOWMINNOACTIVE // 7
            || show == SW_FORCEMINIMIZE)  // 11
            return false;            // 进程明确要求隐藏/最小化
    }

    return true;
}
```

**不再检查 `ConsoleApp`**（偏移 32）。`AllocConsole` 设置 `ConsoleApp=FALSE` 但应触发终端。

---

## 执行轨迹：is_interactive_user_session 过滤

```c
bool is_interactive_user_session() noexcept {
    // 检查 1: 会话 ID
    DWORD session_id = 0;
    if (!ProcessIdToSessionId(GetCurrentProcessId(), &session_id))
        return false;
    if (session_id == 0)             // Session 0 = 服务/驱动
        return false;

    // 检查 2: 窗口站可见性
    auto winsta = GetProcessWindowStation();
    if (!winsta)
        return false;

    USEROBJECTFLAGS flags{};
    if (!GetUserObjectInformationW(winsta, UOI_FLAGS, &flags, sizeof(flags), nullptr))
        return false;
    if (!(flags.dwFlags & WSF_VISIBLE))  // 0x0001 — 窗口站不可见
        return false;

    return true;
}
```

---

## 执行轨迹：try_handoff_all 候选枚举

```c
bool try_handoff_all(win32::handle_view server, win32::handle_view ev,
                     const CONSOLE_PORTABLE_ATTACH_MSG &portable, DWORD client_pid) {

    auto candidates = std::array{
        clsid::default_clsid(clsid::delegation_step::console),  // 注册表
        clsid::wt_console,       // {2EACA947-...}  WT 稳定版
        clsid::wt_console_pre,   // {06EC847C-...}  WT Preview
        clsid::wt_console_can,   // {A854D02A-...}  WT Canary
        clsid::wt_console_dev,   // {1F9F2BF5-...}  WT Dev
    };
    auto marker = std::array{false, true, true, true, true};
    // 注册表结果不需要 marker 验证（用户主动选择）
    // WT 四个通道需要 marker 验证（终端必须声明自己是默认终端）

    auto valid = std::views::zip(candidates, marker)
               | std::views::filter([](const auto &p) {
                     return !need_skip(std::get<0>(p));
                     // need_skip: clsid == zero 或 clsid == conhost → true (跳过)
                 });

    auto it = std::ranges::find_if(valid, [&](const auto &p) {
        return attempt_handoff(std::get<0>(p), std::get<1>(p),
                               server, ev, portable, client_pid);
    });

    return it != std::ranges::end(valid);  // 找到第一个成功的候选
}
```

---

## 执行轨迹：attempt_handoff 完整 COM 移交

```c
bool attempt_handoff(const CLSID &console_clsid, bool marker_check_required,
                     win32::handle_view server_handle, win32::handle_view input_event,
                     const CONSOLE_PORTABLE_ATTACH_MSG &portable_msg, DWORD client_pid) {

    // ── H1: COM 初始化 ──
    auto apt = win32::com_apartment{COINIT_MULTITHREADED};
    // CoInitializeEx(nullptr, COINIT_MULTITHREADED)
    // 析构时 CoUninitialize()

    // ── H2: 创建 COM 对象 ──
    winrt::com_ptr<IConsoleHandoff> hnd;
    try {
        hnd = winrt::create_instance<IConsoleHandoff>(
            console_clsid,         // 候选 CLSID
            CLSCTX_LOCAL_SERVER    // = 4
        );
        // → CoCreateInstance(console_clsid, nullptr, CLSCTX_LOCAL_SERVER,
        //                    IID_IConsoleHandoff, &raw)
        // 失败 → REGDB_E_CLASSNOTREG → throw → catch → return false
    } catch (...) {
        return false;  // CLSID 未安装
    }

    // ── H3: 可选 marker 验证 ──
    if (marker_check_required) {
        try {
            hnd.as<IDefaultTerminalMarker>();
            // → QueryInterface(IID_IDefaultTerminalMarker)
            // 失败 → E_NOINTERFACE → throw
        } catch (...) {
            return false;  // 终端未声明默认终端
        }
    }

    // ── H4: 创建信号管道 ──
    win32::handle sr;      // 读端 — 信号线程
    win32::handle sw;      // 写端 — 传给 WT（所有权转移）
    if (!CreatePipe(sr.put(), sw.put(), nullptr, 0))
        win32::throw_last_error();

    // ── H5: 复制自身进程句柄 ──
    auto our_proc = win32::duplicate_self();
    // → DuplicateHandle(GetCurrentProcess(), GetCurrentProcess(),
    //                    GetCurrentProcess(), &h, SYNCHRONIZE, FALSE, 0)

    // ── H6: EstablishHandoff ──
    win32::handle client;  // WT 返回的进程句柄
    auto hr = hresult(static_cast<unsigned long>(hnd->EstablishHandoff(
        server_handle.get(),   // [in]  ConDrv Server（非拥有引用）
        input_event.get(),     // [in]  事件（非拥有引用）
        &portable_msg,         // [in]  便携消息（非拥有引用）
        sw.get(),              // [in]  信号管道写端（所有权转移给 WT）
        our_proc.get(),        // [in]  进程句柄（所有权转移给 WT）
        client.put()           // [out] ← WT 进程句柄
    )));
    if (FAILED(hr)) throw hr;

    // ── H7: 关闭已转移所有权的句柄 ──
    sw.clear();        // → CloseHandle — WT 已在 H6 中通过 DuplicateHandle 获取副本
    our_proc.clear();  // → CloseHandle

    // ── H8: 信号线程 ──
    // 第一跳无 PTY 管道，vt_in 为空。管道断开时信号线程自然退出。
    auto tp = std::make_unique<miniio::signal_thread_params>(
        signal_thread_params{std::move(sr), win32::handle{} /* no vt_in for first hop */}
    );
    auto sig_thread = win32::basic_thread{signal_thread_proc, tp.get()};
    tp.release();  // 线程入口接管

    // ── H9: 阻塞等待 WT 退出 ──
    if (WaitForSingleObject(client.get(), INFINITE) == WAIT_FAILED)
        win32::throw_last_error();
    client.clear();  // → CloseHandle

    return true;  // 移交成功
}
```

**句柄所有权转移**：

| 句柄 | 创建方式 | H6 前持有者 | H6 后持有者 | conhost 侧何时关闭 |
|------|---------|-----------|-----------|------------------|
| sw | CreatePipe | attempt_handoff | WT | H7 — sw.clear() |
| our_proc | duplicate_self | attempt_handoff | WT | H7 — our_proc.clear() |
| sr | CreatePipe | attempt_handoff | 信号线程 | 线程退出时 RAII |
| client | WT 通过 COM 输出 | — | attempt_handoff | H9 — client.clear() |

---

## make_portable_attach_msg / make_connect_msg 转换

```c
// io_msg → CONSOLE_PORTABLE_ATTACH_MSG (用于 COM 传递)
CONSOLE_PORTABLE_ATTACH_MSG make_portable_attach_msg(const io_msg &msg) {
    CONSOLE_PORTABLE_ATTACH_MSG p{};
    p.IdLowPart  = msg.descriptor.Identifier.LowPart;
    p.IdHighPart = msg.descriptor.Identifier.HighPart;
    p.Process    = msg.descriptor.Process;    // ULONG_PTR → ULONG64
    p.Object     = msg.descriptor.Object;
    p.Function   = msg.descriptor.Function;   // = 0x01
    p.InputSize  = msg.descriptor.InputSize;
    p.OutputSize = msg.descriptor.OutputSize;
    return p;
}

// CONSOLE_PORTABLE_ATTACH_MSG → io_msg (用于 accept_connection)
io_msg make_connect_msg(PCCONSOLE_PORTABLE_ATTACH_MSG msg) {
    io_msg m{};
    m.descriptor.Identifier.LowPart  = msg->IdLowPart;
    m.descriptor.Identifier.HighPart = msg->IdHighPart;
    m.descriptor.Process  = static_cast<ULONG_PTR>(msg->Process);
    m.descriptor.Object   = static_cast<ULONG_PTR>(msg->Object);
    m.descriptor.Function = msg->Function;
    m.descriptor.InputSize  = msg->InputSize;
    m.descriptor.OutputSize = msg->OutputSize;
    return m;
}
```

---

## 信号管道协议

### 线格式

每条消息 = 1 字节信号码 `uint8_t` + N 字节数据包。无分隔符，无填充。`ReadFile` 精确按字节数读取。

### 信号码

| code | 名称 | 数据 | 大小 | 操作 |
|------|------|------|------|------|
| 1 | ConsoleNotifyConsoleApplication | CONSOLENOTIFYAPPDATA | ≥8 | 调用 ConsoleControl(1, &cpi, 8) |
| 5 | ConsoleSetForeground | 无 | 0 | 忽略 |
| 7 | ConsoleEndTask | CONSOLEENDTASKDATA | ≥16 | 调用 ConsoleControl(7, &c, 24) |
| 其他 | — | — | — | std::abort() — 线程退出 |

### 数据结构

**CONSOLENOTIFYAPPDATA**（8+ 字节）：

| 偏移 | 字段 | 类型 |
|------|------|------|
| 0 | dwSize | UINT32 (≥8) |
| 4 | dwProcessID | UINT32 |

转发：`ConsoleControl(1, {dwProcessID, CPI_NEWPROCESSWINDOW}, 8)`

**CONSOLEENDTASKDATA**（16+ 字节）：

| 偏移 | 字段 | 类型 |
|------|------|------|
| 0 | dwSize | UINT32 (≥16) |
| 4 | ProcessId | UINT32 |
| 8 | ConsoleEventCode | UINT32 |
| 12 | ConsoleFlags | UINT32 |

转发：`ConsoleControl(7, {ProcessId, nullptr, ConsoleEventCode, ConsoleFlags}, 24)`

**向前兼容**：若 `dwSize > sizeof(packet)`，跳过超出部分。

---

## 句柄/资源生命周期

| 资源 | 创建 | 持有者 | 释放 |
|------|------|--------|------|
| server (ConDrv) | OS | handle_view（非拥有） | OS 在 corehost 退出后回收 |
| input_event | CreateEventW | defterm_entry | defterm_entry 返回时 RAII |
| COM apartment | CoInitializeEx | attempt_handoff | 返回时 CoUninitialize |
| IConsoleHandoff 指针 | create_instance | attempt_handoff (com_ptr) | 返回时 Release |
| sr (管道读端) | CreatePipe | 信号线程 | 线程退出时 CloseHandle |
| sw (管道写端) | CreatePipe | WT | conhost 在 H7 中 CloseHandle |
| our_proc | duplicate_self | WT | conhost 在 H7 中 CloseHandle |
| client (WT 进程) | WT 返回 | attempt_handoff | Wait 返回后 CloseHandle |
| sig_thread 句柄 | CreateThread | attempt_handoff (RAII) | 析构 CloseHandle |
| \Input/\Output 句柄 | NtOpenFile | accept_connection 返回的 io_handles | 持有者析构时 CloseHandle |

