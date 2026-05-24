# COM 服务器模式（-Embedding）—— 完整执行轨迹

## 触发条件

命令行中出现 `-Embedding` token。该 token 独占一个参数位置，不带值。

在 `wWinMain` 中判断顺序：`args.com_server()` 检查第一个。若为真，所有其他命令行参数被忽略。

---

## 涉及的系统组件

| 组件 | 角色 | 关键行为 |
|------|------|---------|
| CSRSS | Windows 内核子系统 | 创建控制台进程，启动 conhost.exe |
| ConDrv (`\Device\ConDrv`) | 内核控制台驱动 | 处理 IOCTL_READ_IO / IOCTL_COMPLETE_IO |
| 收件箱 conhost | 系统自带 conhost.exe | 读取 `DelegationConsole`，调用 `CoCreateInstance`，然后 `IConsoleHandoff::EstablishHandoff` |
| corehost (`-Embedding`) | 本进程 | `CoRegisterClassObject`，等待被调用 |
| Windows Terminal | 终端 UI 进程 | 实现 `ITerminalHandoff3` |

---

## COM 接口完整定义

### IConsoleHandoff

**IID**: `{E686C757-9A35-4A1C-B3CE-0BCC8B5C69F4}`

```
HRESULT EstablishHandoff(
    [in]  HANDLE server,                      // H1 — ConDrv 驱动 Server 句柄
    [in]  HANDLE inputEvent,                  // H2 — ManualReset 事件句柄
    [in]  const CONSOLE_PORTABLE_ATTACH_MSG* msg,  // msg  — 携连接消息指针
    [in]  HANDLE signalPipe,                  // H3 — 收件箱原始信号管道
    [in]  HANDLE inboxProcess,                // H4 — 收件箱进程句柄
    [out] HANDLE* process                     // &out — corehost 自身进程句柄
);
```

调用方（收件箱 conhost）在函数返回后可能立即关闭 H1~H4。COM 运行时也可能自动关闭它们。因此 corehost 必须通过 `DuplicateHandle` 为每个参数创建本进程副本。

### CONSOLE_PORTABLE_ATTACH_MSG

COM ABI 安全结构体（扁平，无嵌套 union，无 body buffer）：

| 偏移 | 字段 | 类型 | 值/来源 |
|------|------|------|--------|
| 0 | `IdLowPart` | DWORD | `descriptor.Identifier.LowPart` |
| 4 | `IdHighPart` | LONG | `descriptor.Identifier.HighPart` |
| 8 | `Process` | ULONG64 | 客户端进程 ID（`descriptor.Process`） |
| 16 | `Object` | ULONG64 | 客户端线程 ID（`descriptor.Object`） |
| 24 | `Function` | ULONG | `0x01`（CONNECT） |
| 28 | `InputSize` | ULONG | 输入缓冲区大小 |
| 32 | `OutputSize` | ULONG | 输出缓冲区大小 |

### ITerminalHandoff3

**IID**: `{6F23DA90-15C5-4203-9DB0-64E73F1B1B00}`

```
HRESULT EstablishPtyHandoff(
    [out] HANDLE* in,                         // ← 终端创建：标准输入读端
    [out] HANDLE* out,                        // ← 终端创建：标准输出写端
    [in]  HANDLE signal,                      // → 新建信号管道写端
    [in]  HANDLE reference,                   // → ConDrv \Reference 句柄
    [in]  HANDLE server,                      // → corehost 进程句柄（SYNCHRONIZE）
    [in]  HANDLE client,                      // → 客户端进程句柄
    [in]  TERMINAL_STARTUP_INFO* startupInfo  // → 全零
);
```

### TERMINAL_STARTUP_INFO

传递给终端但全部零初始化：`pszTitle=NULL`，窗口中点，字符尺寸默认，`wShowWindow=SW_SHOWDEFAULT(10)`。

### IDefaultTerminalMarker

**IID**: `{746E6BC0-AB05-4E38-AB14-71E86763141F}`。空标记接口（仅 IUnknown）。corehost 在 `-Embedding` 模式下不实现。

### corehost 的 COM 类

- CLSID: `{47A3A1A0-2D3C-4F5E-8B1A-9C3D4E5F6A7B}`
- 实现: `IConsoleHandoff` + `IClassFactory`
- IUnknown 来源: `winrt::implements<console_handoff, IConsoleHandoff, IClassFactory>` — 自动生成 `QueryInterface`/`AddRef`/`Release`

---

## 执行轨迹：wWinMain → com_server_entry

### 步骤 0 — 入口前

`wWinMain` 调用 `suppress_crt_error_dialogs()` 禁止 CRT 弹窗。调用 `console::initialize_console_control()` 从 `user32.dll` 获取 `ConsoleControl` 函数指针（供信号线程使用）：`GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, "user32.dll", &hUser32)` → `GetProcAddress(hUser32, "ConsoleControl")`。

命令行 `corehost -Embedding` 经 `console_arguments` 解析，`args.com_server()` 返回 `true`。

### 步骤 1 — com_server_entry() 调用

控制流进入 `comserver::com_server_entry()`。返回类型：`handoff_result`（按值返回，C++17 保证 RVO）。

---

## 执行轨迹：com_server_entry 主线程

### 步骤 1.1 — COM 初始化

```c
win32::com_apartment apt{COINIT_MULTITHREADED};
// → CoInitializeEx(nullptr, COINIT_MULTITHREADED)
//   若返回 RPC_E_CHANGED_MODE 或其他失败 → throw win32::hresult
//   析构时自动 CoUninitialize()
```

线程进入 MTA。所有后续 COM 调用由系统 RPC 线程池分发。

### 步骤 1.2 — 创建同步事件

```c
win32::event done_ev{win32::create_tag, true, false};
// → CreateEventW(nullptr, TRUE, FALSE, nullptr)
//   bManualReset=TRUE → SetEvent 后保持有信号
//   bInitialState=FALSE → WaitForSingleObject 将阻塞
```

`done_ev` 是一个 RAII 事件对象，析构时自动 `CloseHandle`。此事件传递给 COM 实现类。主线程将在此事件上阻塞。

### 步骤 1.3 — 创建 COM 实现类

```c
handoff_result result{};  // 全部字段: server=null, vt_in=null, vt_out=null, event=null
auto handoff = winrt::make<console_handoff>(done_ev, result);
// → winrt::implements 构造函数:
//    保存 done_ev 和 result 的引用
//    基类初始化引用计数为 1
//    QueryInterface 自动路由: IUnknown / IConsoleHandoff / IClassFactory → this
//    AddRef → InterlockedIncrement
//    Release → InterlockedDecrement; 若为 0 → delete this
```

`handoff` 类型为 `winrt::com_ptr<console_handoff>`，拷贝时 `AddRef`，析构时 `Release`。COM 实现类的生命周期完全由引用计数管理。

### 步骤 1.4 — 注册类工厂

```c
struct cls_obj { DWORD cookie = 0; ~cls_obj() { if (cookie) CoRevokeClassObject(cookie); } };
cls_obj obj;
auto factory = handoff.as<IClassFactory>();
// → handoff 的 QueryInterface 返回 IClassFactory*
//   基类引用计数 +1（factory 持有引用）

auto hr = ::CoRegisterClassObject(
    clsid::corehost_console,  // {47A3A1A0-2D3C-...}
    factory.get(),            // IClassFactory* 指针
    CLSCTX_LOCAL_SERVER,      // = 4 — 独立进程外服务器
    REGCLS_SINGLEUSE,         // = 2 — 只接受一次激活
    &obj.cookie               // 输出注册令牌
);
// 失败 → throw win32::hresult(hr)
// 成功 → obj.cookie 被填入非零值，析构时 CoRevokeClassObject
```

关键标志：`REGCLS_SINGLEUSE` — COM 运行时对此 CLSID 的后续 `CoCreateInstance` 调用将启动新的 corehost 进程。`CLSCTX_LOCAL_SERVER` — 告诉 SCM 本进程是独立 .exe，进程退出时类工厂自动失效。

### 步骤 1.5 — 阻塞等待

```c
done_ev.wait();
// → WaitForSingleObject(done_ev.get(), INFINITE)
//   主线程在此阻塞，CPU 使用率为 0
//   仅当 EstablishHandoff 中调用 ev.set() 时唤醒
```

主线程暂停。系统 RPC 线程池接管所有 COM 请求分发。

---

## 执行轨迹：收件箱 conhost 侧（外部触发）

收件箱 conhost 的行为不在本进程中，此处仅描述与本进程交互的部分：

1. 收件箱 conhost 读取注册表 `HKCU\Console\%%Startup\DelegationConsole` 获取 CLSID（必须等于 corehost 的 CLSID）
2. 调用 `CoCreateInstance(corehost_console_CLSID, nullptr, CLSCTX_LOCAL_SERVER, IID_IConsoleHandoff, &ptr)`
3. COM SCM 找到已注册的类工厂（或启动新 corehost -Embedding）
4. SCM 在任意 RPC 线程上调用 `IClassFactory::CreateInstance(nullptr, IID_IConsoleHandoff, &obj)`
5. 收件箱 conhost 获得 `IConsoleHandoff*`
6. 收件箱调用 `EstablishHandoff(server, inputEvent, &msg, signalPipe, inboxProcess, &ourProcess)`
7. 收件箱调用 `WaitForSingleObject(ourProcess, INFINITE)` 阻塞等待 corehost 退出
8. corehost 退出后 `WaitForSingleObject` 返回，收件箱退出

---

## 执行轨迹：IClassFactory::CreateInstance（RPC 线程上）

```c
HRESULT CreateInstance(IUnknown *outer, REFIID riid, void **obj) noexcept override
{
    if (outer)                   // outer ≠ nullptr → 请求聚合
        return CLASS_E_NOAGGREGATION;  // 不支持聚合
    return QueryInterface(riid, obj);  // riid = IID_IConsoleHandoff
    // → winrt::implements 的 QueryInterface:
    //   检查 riid ∈ {IID_IUnknown, IID_IConsoleHandoff, IID_IClassFactory}
    //   匹配 → AddRef() → *obj = static_cast<IConsoleHandoff*>(this) → S_OK
    //   不匹配 → E_NOINTERFACE
}
```

返回的 `IConsoleHandoff*` 引用计数为 2（初始 1 + QueryInterface 加 1）。收件箱 conhost 持有此指针。

---

## 执行轨迹：EstablishHandoff 逐值执行（RPC 线程上）

此方法可能是整个系统中最重要的函数。每个参数、每个比较、每个返回值都必须精确。

### EstablishHandoff 完整签名

```
HRESULT STDMETHODCALLTYPE EstablishHandoff(
    HANDLE server,                        // = H1
    HANDLE inputEvent,                    // = H2
    PCCONSOLE_PORTABLE_ATTACH_MSG msg,    // = &msg
    HANDLE signalPipe,                    // = H3
    HANDLE inboxProcess,                  // = H4
    HANDLE *process                       // = &out
) noexcept override
```

实际实现中方法体由 `try { ... } catch (...) { return E_FAIL; }` 包裹。所有 Win32 错误通过 `throw` 转换为 HRESULT（外层的 catch(...) 统一返回 `E_FAIL`）。

### 执行步骤（共 8 步）

#### 步骤 E1 — RAII 守卫

```c
done_guard guard{done_ev};
// 构造：保存 done_ev 的引用
// 析构（在任意 return 路径执行）：
//   → done_ev.set()
//   → SetEvent(done_ev.get())
//   → 主线程从 done_ev.wait() 唤醒
```

这意味着无论 EstablishHandoff 成功、失败或异常，事件一定会被设置。主线程永远不会永久阻塞。

#### 步骤 E2 — 输出参数检查

```c
if (!process)           // process == nullptr ?
    return E_INVALIDARG; // 0x80070057
*process = nullptr;     // 调用方可能传入未初始化的 HANDLE
```

#### 步骤 E3 — DuplicateHandle ×4（创建传入句柄的副本）

COM 规范要求被调用方复制所有传入句柄。实现封装在 `win32::duplicate_handle(HANDLE)` 中：

```c
auto dup_server  = win32::duplicate_handle(server);       // H1 → 本进程副本
auto dup_event   = win32::duplicate_handle(inputEvent);   // H2 → 本进程副本
auto dup_signal  = win32::duplicate_handle(signalPipe);   // H3 → 本进程副本
auto dup_inbox   = win32::duplicate_handle(inboxProcess); // H4 → 本进程副本
```

`duplicate_handle` 的实现：

```c
handle duplicate_handle(HANDLE src, DWORD access = DUPLICATE_SAME_ACCESS) {
    handle h;
    BOOL ok = DuplicateHandle(
        GetCurrentProcess(),   // 源进程：当前进程（调用方已将句柄引入本进程空间）
        src,                   // 源句柄
        GetCurrentProcess(),   // 目标进程：当前进程
        h.put(),               // 输出句柄
        DUPLICATE_SAME_ACCESS, // = 2 — 保持与源句柄相同的访问掩码
        FALSE,                 // 非继承
        0                      // 无标志
    );
    if (!ok) throw get_last_error(); // EstablishHandoff catch → E_FAIL
    return h;
}
```

每个 `DuplicateHandle` 失败都单独抛异常，导致外层 catch 返回 `E_FAIL`。

此时 4 个副本句柄由 RAII 管理，析构时自动 `CloseHandle`。

`dup_server`  — 后续用于 `accept_connection` 和 `DeviceIoControl`
`dup_event`   — 用于 `run_io_loop` 中 `read_io` 的 `WaitForSingleObject(event, INFINITE)`
`dup_signal`  — 收件箱的原始信号管道副本。在 `-Embedding` 模式中未被消费（我们新建了自己的管道）
`dup_inbox`   — 收件箱进程句柄副本。可用于监控收件箱存活状态

#### 步骤 E4 — 创建 \Reference 句柄

```c
win32::handle ref_handle = condrv::create_client_handle(
    win32::handle_view(server),  // 以传入的原始 Server 句柄为 RootDirectory
    L"\\Reference"               // 对象名 — ConDrv 驱动命名空间中的引用对象
);
// 内部执行:
//   NtOpenFile(
//       &h,
//       GENERIC_READ | GENERIC_WRITE | SYNCHRONIZE,
//       &oa,                  // RootDirectory = server, ObjectName = \Reference
//       &iosb,
//       FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
//       FILE_SYNCHRONOUS_IO_NONALERT
//   )
// 失败 → throw → E_FAIL
```

`\Reference` 句柄保持 ConDrv 驱动设备对象的引用计数。终端持有此句柄可防止驱动过早卸载。

#### 步骤 E5 — 创建信号管道（替换收件箱的原始管道）

```c
win32::handle signal_read;   // 读端 — 信号线程从此读取
win32::handle signal_write;  // 写端 — 将在步骤 E6 传给终端
if (!CreatePipe(signal_read.put(), signal_write.put(), nullptr, 0))
    return HRESULT_FROM_WIN32(GetLastError());
    // nSize=0 → 系统默认缓冲区大小
    // 失败例: ERROR_NO_SYSTEM_RESOURCES (1450) → 0x800705AA
```

`signal_write` 将在第二跳中传给终端（终端通过它发送 Ctrl+C/Break/Close 信号）。`signal_read` 留给信号线程。

#### 步骤 E6 — 查询第二跳目标 CLSID

```c
auto terminal_clsid = clsid::default_clsid(clsid::delegation_step::terminal);
```

`clsid::default_clsid` 查询注册表 `DelegationTerminal`：

```c
win32::registry_key key{open_tag, hkcu, L"Console\\%%Startup"};
// 失败 → 返回 clsid::zero ({00000000-...})

RegGetValueW(key.get(), nullptr, L"DelegationTerminal",
             RRF_RT_REG_SZ,   // 只接受 REG_SZ — 若类型非 REG_SZ 则失败
             nullptr, buf, &size);
// 找不到值或类型不匹配 → 返回 clsid::zero

CLSIDFromString(buf, &clsid);
// 解析失败 → 返回 clsid::zero
```

#### 步骤 E7 — 分支：第二跳或有/无

```c
if (terminal_clsid != clsid::zero)   // {00000000-0000-0000-0000-000000000000}
{
    try {
        do_handoff(terminal_clsid, msg,
                   signal_write.view(), ref_handle.view(),
                   dup_server, dup_event,
                   std::move(signal_read), result);
    } catch (...) {
        fallback_passthrough(dup_server, dup_event, msg);
        return E_FAIL;
    }
}
```

若 `terminal_clsid == clsid::zero`（注册表未配置）→ 跳过整个第二跳。不启动终端，不调用信号线程，result 保持空句柄。主线程仍会被唤醒但 `handoff_result` 全空——调用方 `wWinMain` 将空句柄传给 `conpty_entry`，后者会立即因 I/O 循环失败而退出。

##### E7a — do_handoff 内部（第二跳成功路径）

```c
static void do_handoff(REFCLSID terminal_clsid,
                       PCCONSOLE_PORTABLE_ATTACH_MSG msg,
                       win32::handle_view signal_write,    // 信号管道写端
                       win32::handle_view ref_handle,      // \Reference
                       win32::handle &dup_server,          // ConDrv Server 副本
                       win32::handle &dup_event,           // 事件副本
                       win32::handle signal_read,          // 信号管道读端（移动）
                       handoff_result &result)
{
    // ── E7a-1: 打开客户端进程 ──
    auto pid = static_cast<DWORD>(msg->Process);
    // msg->Process 是 ULONG64（来自 CONSOLE_PORTABLE_ATTACH_MSG）
    // 低 32 位为客户端进程 ID

    win32::handle client = OpenProcess(
        PROCESS_QUERY_INFORMATION  |  // 0x0400
        PROCESS_SET_INFORMATION    |  // 0x0200
        PROCESS_VM_READ            |  // 0x0010
        SYNCHRONIZE,                  // 0x00100000
        TRUE,                         // bInheritHandle=TRUE（无影响，无子进程）
        pid
    );
    if (!client.valid()) throw get_last_error();

    // ── E7a-2: 复制自身进程句柄 ──
    auto server_h = win32::duplicate_self();
    // → DuplicateHandle(GetCurrentProcess(), GetCurrentProcess(), GetCurrentProcess(),
    //                    &h, SYNCHRONIZE, FALSE, 0)
    // 终端使用此句柄通过 WaitForSingleObject 监控 corehost 存活

    // ── E7a-3: 启动终端进程 ──
    auto terminal = winrt::create_instance<ITerminalHandoff3>(
        terminal_clsid,        // 注册表中读取的 DelegationTerminal CLSID
        CLSCTX_LOCAL_SERVER    // = 4 — 启动独立的 WindowsTerminal.exe
    );
    // 内部: CoCreateInstance(terminal_clsid, nullptr, CLSCTX_LOCAL_SERVER,
    //                        IID_ITerminalHandoff3, &raw)
    // 若终端未安装 → REGDB_E_CLASSNOTREG → throw → fallback_passthrough

    // ── E7a-4: EstablishPtyHandoff ──
    win32::handle wt_in, wt_out;     // "wt" = Windows Terminal
    TERMINAL_STARTUP_INFO startup{}; // 全零
    auto hr = terminal->EstablishPtyHandoff(
        wt_in.put(),           // [out] ← 终端返回的 stdin 读端
        wt_out.put(),          // [out] ← 终端返回的 stdout 写端
        signal_write.get(),    // [in]  → 信号管道写端（终端持有）
        ref_handle.get(),      // [in]  → \Reference 句柄
        server_h.get(),        // [in]  → corehost 进程句柄
        client.get(),          // [in]  → 客户端句柄
        &startup               // [in]  → 全零
    );
    if (FAILED(hr)) throw hresult(hr);
    // wt_in  = 终端新创建的管道读端 — corehost 将从这里 ReadFile
    // wt_out = 终端新创建的管道写端 — corehost 将向这里 WriteFile
    // 方向：corehost 的 WriteFile 到 wt_out → 终端读取后渲染
    //       corehost 从 wt_in ReadFile ← 终端写入用户输入
```

**wt_in/wt_out 的语义**：`EstablishPtyHandoff` 返回的 `in` 和 `out` 是从终端视角命名的。`in` 是终端的标准输入（终端从中读取），对 corehost 来说是从中 ReadFile 获取用户键盘输入。`out` 是终端的标准输出（终端向其中写入），对 corehost 来说是向其 WriteFile 发送控制台输出。

```c
    // ── E7a-5: CONNECT 完成 ──
    auto conn_msg = miniio::make_connect_msg(msg);
    // → 将 CONSOLE_PORTABLE_ATTACH_MSG 的 6 个字段复制到 io_msg.descriptor:
    //   Identifier.LowPart  = msg->IdLowPart
    //   Identifier.HighPart = msg->IdHighPart
    //   Process  = static_cast<ULONG_PTR>(msg->Process)
    //   Object   = static_cast<ULONG_PTR>(msg->Object)
    //   Function = msg->Function (= 0x01)
    //   InputSize  = msg->InputSize
    //   OutputSize = msg->OutputSize

    miniio::accept_connection(dup_server.view(), conn_msg);
    // → NtOpenFile(\Input) → in_h 句柄
    // → NtOpenFile(\Output) → out_h 句柄
    //   Process = Input = reinterpret_cast<ULONG_PTR>(in_h.get())
    //   Output  = reinterpret_cast<ULONG_PTR>(out_h.get())
    // → prepare_completion(msg, 0, sizeof(CD_CONNECTION_INFORMATION))
    //   msg.complete.Identifier = msg.descriptor.Identifier
    //   msg.complete.IoStatus.Status = 0 (= STATUS_SUCCESS)
    //   msg.complete.IoStatus.Information = sizeof(CD_CONNECTION_INFORMATION)
    //   msg.complete.Write.Data = &conn
    //   msg.complete.Write.Size = sizeof(CD_CONNECTION_INFORMATION)
    // → complete_io(server, msg.complete)
    //   DeviceIoControl(server, IOCTL_COMPLETE_IO, &msg.complete, ...)
    // 返回 io_handles{in_h, out_h} — 在 do_handoff 中被丢弃
    // （后续 I/O 循环由 conpty_entry 接管，不需要这些句柄）

    // ── E7a-6: 信号线程 ──
    // 创建 vt_in 副本传给信号线程，管道断开时信号线程关闭它
    // → PeekNamedPipe 失败 → _vt_eof → 主 I/O 循环退出
    auto vt_in_for_signal = win32::duplicate_handle(wt_out.get());
    auto tp = std::make_unique<signal_thread_params>(
        signal_thread_params{std::move(signal_read), std::move(vt_in_for_signal)}
    );
    auto sig_thread = win32::basic_thread{signal_thread_proc, tp.release()};
    // → CreateThread(nullptr, 0, signal_thread_proc, tp.get(), 0, nullptr)
    // sig_thread 析构时 CloseHandle(线程句柄)，不终止线程

    // ── E7a-7: 填充 handoff_result ──
    result.server = std::move(dup_server);   // ConDrv \Server 副本
    result.event  = std::move(dup_event);    // ManualReset 事件副本
    result.vt_in  = std::move(wt_out);       // 终端写端 → corehost WriteFile 目标
    result.vt_out = std::move(wt_in);        // 终端读端 → corehost ReadFile 来源
    // dup_server/dup_event 的所有权转移给 result
    // do_handoff 返回后 result 将最终返回给 wWinMain
}
```

##### E7b — fallback_passthrough（第二跳失败路径）

当 `do_handoff` 中任意步骤抛异常（终端未安装、OpenProcess 失败、COM 激活失败等）：

```c
static void fallback_passthrough(win32::handle &dup_server, win32::handle &dup_event,
                                 PCCONSOLE_PORTABLE_ATTACH_MSG msg)
{
    auto pid = static_cast<DWORD>(msg->Process);

    // 仍然必须完成 CONNECT — 收件箱等待此消息
    auto conn_msg = make_connect_msg(msg);
    accept_connection(dup_server.view(), conn_msg);

    // Ctrl+Break 终止客户端进程（避免客户端在无窗口控制台中永久阻塞）
    GenerateConsoleCtrlEvent(CTRL_BREAK_EVENT, pid);

    // 进入 passthrough 循环：接受所有 CONNECT，非 CONNECT 消息走 dispatch_non_connect
    passthrough_handler fb{.server = dup_server.view()};
    run_io_loop(dup_server.view(), dup_event.view(), fb);
    // 此调用阻塞直到所有客户端断开
    // dup_server 和 dup_event 的所有权保留在 fallback_passthrough 调用方
}
```

#### 步骤 E8 — 返回进程句柄

```c
auto proc = win32::duplicate_self();
*process = proc.get();   // 提取原始 HANDLE（不 CloseHandle）
proc.release();          // 放弃 RAII 所有权
// 收件箱 conhost 将调用 WaitForSingleObject(*process, INFINITE)
// 当 corehost 退出时，此句柄变为有信号，收件箱随之退出

return S_OK;
```

### 异常路径简述

`EstablishHandoff` 方法体由 `try { ... } catch (...) { return E_FAIL; }` 包裹。异常可能来自：
- 步骤 E3：任一 `DuplicateHandle` 失败
- 步骤 E4：`NtOpenFile(\Reference)` 失败
- 步骤 E7a：`OpenProcess`、`duplicate_self`、`create_instance`、`EstablishPtyHandoff`、`accept_connection` 任一步失败

步骤 E5 的错误（`CreatePipe`）通过显式检查 `if (!CreatePipe(...)) return HRESULT_FROM_WIN32(...)` 处理，不抛异常。

外层的 `catch(...)` 捕获所有异常后返回 `E_FAIL`。但 `done_guard` 的析构函数在 `return` 之前执行，确保 `done_ev.set()` 一定被调用。

---

## 执行轨迹：主线程唤醒后

### 步骤 2.1 — RAII 析构完成

`EstablishHandoff` 返回 → `done_guard` 析构 → `done_ev.set()` → 主线程从 `done_ev.wait()` 唤醒。

### 步骤 2.2 — 类工厂撤销 + COM 清理

```c
// cls_obj 析构:
~cls_obj() {
    if (cookie == 0) return;
    CoRevokeClassObject(cookie);   // 通知 SCM: 不再接受新激活
}
// com_apartment 析构:
~com_apartment() { CoUninitialize(); }
// handoff (winrt::com_ptr) 析构:
//   Release() → InterlockedDecrement → 若引用计数为 0 → delete console_handoff
```

### 步骤 2.3 — 返回 handoff_result

```c
return result;
// 按值返回，C++17 保证复制消除
// result 的 4 个字段全部由 RAII handle 移动而来，调用方获得所有权
```

### 步骤 2.4 — wWinMain 桥接到 conpty

```c
// wWinMain 中:
auto hr = comserver::com_server_entry();  // 获得 handoff_result

conpty::conpty_entry(
    std::move(hr.server),   // ConDrv Server 句柄
    std::move(hr.vt_in),    // WriteFile → 终端
    std::move(hr.vt_out),   // ReadFile ← 终端
    std::move(hr.event),    // 输入就绪事件
    0,                      // width=0  → 终端自行决定
    0,                      // height=0 → 终端自行决定
    false,                  // inherit_cursor=false
    text_measurement_mode::console  // = 0
);
// conpty_entry 调用 run_io_loop_no_setup(server, event, handler)
// 进入双缓冲 ConDrv 消息循环，阻塞直到驱动断开
```

---

## 线程与时间线总览

```
T=0    主线程: CoInitializeEx → CreateEvent → winrt::make → CoRegisterClassObject
T=1    主线程: done_ev.wait()  ← 阻塞
T=2    系统: 收件箱 conhost CoCreateInstance → SCM 分发到本进程
T=3    RPC线程: CreateInstance → 返回 IConsoleHandoff*
T=4    RPC线程: EstablishHandoff → DuplicateHandle×4 → \Reference → CreatePipe
T=5    RPC线程: do_handoff → OpenProcess → dup_self → create_instance → EstablishPtyHandoff
T=6    RPC线程: accept_connection → signal_thread(启动) → 填充 result
T=7    RPC线程: done_ev.set() → 返回 S_OK
T=8    主线程: done_ev.wait() 返回 → CoRevokeClassObject → CoUninitialize
T=9    主线程: 返回 handoff_result → wWinMain → conpty_entry → I/O 循环
T=10   信号线程: 读取信号管道 → ConsoleControl → ... (持续运行至管道断开)
```

---

## 错误处理完整表

| 步骤 | 操作 | 错误条件 | 返回/行为 |
|------|------|---------|----------|
| E1 | 构造 done_guard | —（不抛异常） | — |
| E2 | `process == nullptr` | 输出指针为空 | `E_INVALIDARG`（0x80070057） |
| E3 | `duplicate_handle(server)` | `DuplicateHandle` 失败 | `E_FAIL`（通过异常→catch） |
| E3 | `duplicate_handle(inputEvent)` | `DuplicateHandle` 失败 | `E_FAIL` |
| E3 | `duplicate_handle(signalPipe)` | `DuplicateHandle` 失败 | `E_FAIL` |
| E3 | `duplicate_handle(inboxProcess)` | `DuplicateHandle` 失败 | `E_FAIL` |
| E4 | `NtOpenFile(\Reference)` | NT 错误 | `E_FAIL` |
| E5 | `CreatePipe` | Win32 错误 | `HRESULT_FROM_WIN32(GetLastError())` |
| E6 | `default_clsid(terminal)` | 注册表不存在/类型错误 | `clsid::zero` — 跳过第二跳 |
| E7a-1 | `OpenProcess` | 进程已退出 | 抛异常 → `E_FAIL` + fallback |
| E7a-2 | `duplicate_self` | `DuplicateHandle` 失败 | 抛异常 → `E_FAIL` + fallback |
| E7a-3 | `create_instance` | 终端未安装 | 抛异常 → `E_FAIL` + fallback |
| E7a-4 | `EstablishPtyHandoff` | 终端返回失败 HRESULT | 抛异常 → `E_FAIL` + fallback |
| E7a-5 | `accept_connection` | NtOpenFile 失败 | 抛异常 → `E_FAIL` + fallback |
| E8 | `duplicate_self` → `*process` | `DuplicateHandle` 失败 | `E_FAIL` |
| — | 所有成功路径 | — | `S_OK`（0x00000000） |

所有异常路径中 `done_guard` 析构一定执行 → `done_ev.set()` 一定被调用。

---

## 信号线程执行循环

`signal_thread_proc(LPVOID param)` 在新线程中运行。参数所有权转移给线程。

```c
DWORD WINAPI signal_thread_proc(LPVOID param) {
    auto pp = unique_ptr<signal_thread_params>{(signal_thread_params*)param};
    win32::handle &pipe = pp->pipe;  // 信号管道读端

    for (;;) {
        uint8_t code = 0;
        if (!read_exact(pipe.view(), &code, 1))  // ReadFile 精确 1 字节
            break;  // 管道断开 → 终端退出 → 线程结束

        switch (code) {
        case 1: {  // ConsoleNotifyConsoleApplication
            CONSOLENOTIFYAPPDATA d{};
            read_exact(pipe.view(), &d, sizeof(d));  // 读 8 字节
            if (d.dwSize > sizeof(d))
                skip_bytes(pipe.view(), d.dwSize - sizeof(d));
            CONSOLE_PROCESS_INFO cpi{d.dwProcessID, CPI_NEWPROCESSWINDOW};
            ConsoleControl(ConsoleNotifyConsoleApplication, &cpi, sizeof(cpi));
            // = ConsoleControl(1, {pid, 0x0001}, 8)
            break;
        }
        case 5:  // ConsoleSetForeground — 忽略
            break;
        case 7: {  // ConsoleEndTask
            CONSOLEENDTASKDATA d{};
            read_exact(pipe.view(), &d, sizeof(d));  // 读 16 字节
            if (d.dwSize > sizeof(d))
                skip_bytes(pipe.view(), d.dwSize - sizeof(d));
            CONSOLEENDTASK c{d.ProcessId, nullptr, d.ConsoleEventCode, d.ConsoleFlags};
            ConsoleControl(ConsoleEndTask, &c, sizeof(c));
            // = ConsoleControl(7, {pid, NULL, eventCode, ctrlFlags}, 24)
            break;
        }
        default:
            std::abort();
        }
    }
    return 0;
}
```

管道两端：写端在步骤 E7a-4 中传给终端（`EstablishPtyHandoff` 的 `signal` 参数）。终端在键盘事件发生时写入 1 字节消息类型 + 结构体。管道协议和结构体定义见《默认终端协议》文档。



