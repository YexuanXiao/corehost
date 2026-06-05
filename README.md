# corehost

**corehost** is a reimplementation of Windows conhost.

## Background

The orginal Windows conhost has the following issues:

- Abuses mutable global state, resulting in poor code quality
- Contains numerous bugs that are almost impossible to fix
- Lacks necessary documentation and has become unmaintainable

## Positioning

corehost **is not a complete console / terminal**, as it does not provide a window interface.

Its goal is to act as a **bridge between the console GUI and the Windows console system**. To that end, corehost implements three core features:

- **Default terminal handling** - delegates console sessions to Windows Terminal (or other third-party terminals) via COM handoff
- **COM embedding mode** - used to support third-party terminals as the default terminal, compatible with the IConsoleHandoff and ITerminalHandoff protocols used by Windows Terminal.
- **ConPTY** - pseudo console support

corehost aims to provide **high quality and high performance** for driving:

- Windows Terminal
- Contour
- Alacritty
- ConEmu
- and other Windows terminals

Actually, corehost is the ideal OpenConsole/conhost.

## corehost as conhost

corehost solves the problem that the original version of conhost does not support opening the default terminal (e.g., Windows Terminal) when calling `AllocConsole`.

When corehost runs with elevated integrity (High IL) due to sudo or runas, UIPI prevents COM activation and handle transfer to Medium IL terminals. Since no GUI is available (the only GUI available to the original conhost is itself), corehost will refuse to execute the program and send a message in the notification center about what happened.

Note that since msix applications cannot run in safe mode, please back up the system's conhost.exe as an alternative terminal.

## How to use it

corehost supports three usage modes.

First, you can directly replace conhost.exe with corehost.exe, and corehost will take over all functionality of conhost, except that it does not support window mode.

Then, you can use the scripts in the scripts directory to register corehost as the default console. In this mode, when you launch cmd using Run, corehost will take over the functionality of OpenConsole.

Finally, corehost can also be used together with conpty.dll to provide ConPty support for third-party terminals. If you are using conpty.dll released by Microsoft, rename corehost.exe to openconsole.exe; if you are using corehost's libconpty, you can directly use corehost.exe.

Additionally, corehost can be used as a C++ library by third-party terminals, as described below. In this working mode, inter-process communication is completely avoided.

## Compatibility

During development, corehost was tested using cmd, powershell, pwsh, and edit. If you encounter any compatibility issues, including when using other programs, please submit an issue report.

## How to build

1. MSVC14.51+ or Clang20+, supporting both STL and libc++.
2. CMake 4.3 or later
3. `git clone --recurse-submodules https://github.com/YexuanXiao/corehost`
4. `cmake -B build`
5. `cmake --build build --config Release`

## Build targets

- `corehost`: the main executable. It provides the functionality of conhost/OpenConsole.
- `libcorehost`: the core of corehost and implements full ConPTY functionality, currently has around 634kb of code. Third-party terminals can directly link libcorehost into their programs without needing to use an external conhost program.
- `conpty`: the shared library form of libconpty for third-party terminals that export the symbols declared by winconpty.h.
- `conpty_static`: the static library form of libconpty.
- `conhost_proxy`: the COM proxy/stub DLL used by default-terminal handoff and COM embedding.

## CMake options

| Option | Default | Description |
| --- | --- | --- |
| `COREHOST_DISABLE_LOG` | `ON` | Disables corehost logging. Keep this enabled for normal Release builds and performance testing. Set it to `OFF` when diagnosing behavior with log files. |
| `COREHOST_LOG_LEVEL` | `1` | Compile-time log frequency level. `1` records low-frequency events, `2` also records medium-frequency events, and `3` also records high-frequency events. This only matters when `COREHOST_DISABLE_LOG=OFF`. |
| `COREHOST_PERF_DIAG` | `OFF` | Enables aggregated performance diagnostics inside corehost. This is for profiling and should stay disabled for normal performance comparisons. |
| `COREHOST_ANSI_OPT` | `OFF` | Enables optional table-driven ANSI code page fast paths. For users who have enabled UTF-8, this is unnecessary and will increase the binary size. |
| `COREHOST_USE_SYSTEM_ICU` | `ON` | Uses the Windows system ICU library for Unicode character width calculation, reducing the binary size. Disable it when targeting Windows versions earlier than 1903. |
| `USE_INBOX_CONHOST` | `OFF` | Make libconpty only use the system's conhost.exe. |

## Architecture and startup flow

corehost is built around one common runtime: a ConPTY session that speaks to ConDrv on one side and to a terminal frontend on the other side. The different startup modes mainly decide where the ConDrv handle comes from, whether a terminal GUI must be created, and which handoff protocol is used before the ConPTY session starts.

### Mode 1: corehost as conhost.exe

Windows starts conhost.exe for classic console allocation paths, such as launching a console process with `CreateProcess` and `CREATE_NEW_CONSOLE`, using Run to start a console program, or calling `AllocConsole` from a GUI process. In this path ConDrv.sys launches conhost with a command-line handle value such as `0x4`. This value is the process-local numeric value of an already-open ConDrv `\Server` handle. The `\Server` object is the user-mode host's control endpoint for one console session: ConDrv produces console requests on it, and corehost consumes those requests and returns completion results. During `CONNECT`, corehost opens the session's `\Input` and `\Output` objects relative to that `\Server` handle. Those two client handles are handed back to ConDrv and become the console input and console output handles observed by the attached application. When the application later calls console APIs or reads/writes those handles, ConDrv turns that client-side activity into requests that corehost receives through `\Server`.

The first important ConDrv message is the `CONNECT` request. Its payload is a `CONSOLE_SERVER_MSG`. This structure contains startup information that originally came from the client process and the Windows console subsystem, including the requested title, show-window flag, initial buffer/window size, process group id, whether the client is a console application, and whether a visible window is expected.

If `CONSOLE_SERVER_MSG` says that the client needs a visible console UI, corehost looks up the configured default terminal COM class under `HKCU\Console\%%Startup`. The relevant registry values are `DelegationConsole` for the first handoff step and `DelegationTerminal` for the second terminal handoff step. In this direct conhost path, corehost creates the configured terminal's `IConsoleHandoff` object and passes the ConDrv server handle, the ConDrv input-available event, a portable description of the pending `CONNECT` request, a signal pipe, and a corehost process handle. A successful `IConsoleHandoff` call transfers the visible session to that default-terminal implementation. In the current path, corehost does not start its own ConPTY loop after this first-hop handoff; it waits for the terminal process or signal pipe to close and then returns.

The process that ConDrv started with the `0x4` handle still cannot exit immediately after `IConsoleHandoff` returns. The terminal receives corehost's process handle and a signal pipe, so corehost must keep those lifetime objects valid until the terminal process exits or the signal pipe closes. In this first-hop path, corehost is a lifetime anchor rather than the long-running ConPTY message loop.

If the `CONNECT` request does not require a visible GUI, corehost accepts the ConDrv connection itself and enters ConPTY mode directly. In that case it acts as a complete headless console host. There is no terminal window, but console APIs still need coherent behavior.

### Mode 2: corehost as the default-terminal COM server

When corehost is registered as an `IConsoleHandoff` implementation, the inbox Windows conhost may start it with `-Embedding`. In this mode corehost is not launched directly with the initial ConDrv handle. Instead, COM activates corehost and invokes the default-terminal handoff entry point.

The incoming `IConsoleHandoff` call provides the ConDrv server handle, the input-available event, a portable attach message that identifies the pending `CONNECT` request, a signal pipe, and process handles used to coordinate lifetimes. The portable attach message intentionally contains only the stable descriptor fields needed to find the original request. It does not contain the full `CONSOLE_SERVER_MSG`, so corehost reads the original `CONNECT` payload back from ConDrv when it needs the startup title, `ShowWindow`, and related startup data.

During the handoff call, corehost duplicates the ConDrv server handle and input event supplied by the inbox conhost, creates a ConDrv `\Reference` handle, opens the client process, reads the original `CONSOLE_SERVER_MSG`, and then looks up the configured terminal-side CLSID. It creates an `ITerminalHandoff3` object, currently implemented by Windows Terminal, passes terminal startup information and process handles, and receives a pair of VT pipes from the terminal. Once that succeeds, corehost accepts the original ConDrv `CONNECT` request and starts its ConPTY runtime using the resulting handles. From that point on, the terminal owns the GUI side of the session, while this corehost process remains the console server that consumes the ConDrv messages and completes the corresponding console requests.

In this second mode, the COM handoff only sets up the session; it does not finish the console host's work. After the handoff, corehost owns the duplicated ConDrv `\Server` handle, the accepted `\Input` and `\Output` handles, the VT pipes returned by the terminal, and the signal pipe used for terminal notifications. The `\Server` handle is needed to consume ConDrv requests and return completions, while `\Input` and `\Output` keep the client-side console handles connected. If corehost exited at this point, the attached application could see broken console handles, blocked console API calls, or lost VT communication with the terminal. Therefore corehost must continue running the ConPTY runtime for the lifetime of the console session.

The terminal and corehost keep separate state for the same session. The terminal owns the visible UI and terminal-facing state, such as windows, tabs, rendering, selection, scrollback, and its VT screen model. corehost owns the Windows console-server state, including process membership, console modes, input records, code pages, API-visible cursor/attribute state, the API-readable screen buffer, pending ConDrv requests, and control-event routing. VT bytes are the boundary between them: terminal input is sent to corehost as VT, and console output is sent back to the terminal as VT. The terminal does not implement Windows Console APIs, and corehost does not own the visible UI.

### Mode 3: ConPTY

`ConptyCreatePseudoConsole` starts corehost with `--headless`. In this mode libconpty creates the ConDrv server handle, a signal handle, and the VT input/output pipes, then launches corehost with arguments such as `--server`, `--signal`, and others.

corehost parses those arguments, adopts the inherited handles, initializes the session configuration, and enters ConPTY mode immediately. This is also the path used by applications that explicitly use the ConPTY API and provide their own terminal frontend.

### ConPTY runtime

The ConPTY runtime is the shared implementation used by all three startup modes. It is intentionally split into a few functional areas:

- ConDrv I/O reads requests from the ConDrv, completes previous requests, accepts `CONNECT`, and keeps the client `\Input` and `\Output` handles alive for as long as the session exists.
- The message router decodes console API messages and dispatches them to focused handlers. These handlers implement the observable Windows console API behavior that programs expect.
- The console state stores process membership, modes, code pages, cursor state, attributes, title, tab stops, scrolling region, and other state that must be visible through console APIs.
- The screen buffer models the visible console buffer needed by APIs such as cursor positioning, text writes, erase operations, scrolling, and screen reads. It is not a renderer; it is the state model that lets corehost answer console API requests correctly.
- The input buffer stores keyboard and control input in console-event form. VT keyboard sequences from the terminal are decoded into console input records when the client reads from the console input handle.
- The VT parser converts terminal-side VT input into structured messages. It recognizes text, cursor movement, erase operations, SGR, OSC title and hyperlink sequences, terminal replies, and keyboard sequences. Unknown or unsupported sequences are preserved in a form that allows the caller to either pass them through or expose printable text when appropriate.
- The VT output path serializes console output and console-state changes back to VT for the terminal frontend. It buffers output so that large console writes do not become one small `WriteFile` call per character or per escape sequence.
- The signal path handles terminal-side control notifications, such as close, resize, and control events, and maps them into the Windows console behavior expected by attached processes.

### Design

In addition to debugging facilities, corehost **does not use any mutable global variables**. All immutable global variables are **initialized within the main function**, and **no singletons, including magic statics**, are used. All state is **passed through function parameters** and is modularized as much as possible, making corehost very easy to understand and debug, whether for humans or for AI.

The main data path uses a **synchronous I/O** model and is intentionally almost **single-threaded**. One main loop reads a ConDrv request, lets the router update console state or prepare a pending read, flushes VT output when needed, and returns the completion result to ConDrv. Terminal input is also serviced from that same loop during idle or pending-read phases, so ordinary keyboard input, terminal replies, console output, and most API state transitions are serialized through **one state machine**. This **avoids the need for locks** around most console state. A request is either completed immediately, completed by the next ConDrv read cycle, or held pending until terminal input supplies the missing data.

The signal thread is the main exception to that model. It exists because the terminal has a separate control channel that is not part of the normal VT input/output stream. In the direct default-terminal handoff path, the signal thread reads console-control notifications from the terminal signal pipe, forwards the relevant control events to the Windows console subsystem, and notices when the pipe closes. In ConPTY sessions, the PtySignal thread consumes terminal-side sideband messages such as show/hide, clear-buffer, parent-window, and resize notifications. Some messages are only consumed to keep the wire protocol aligned; others update the local screen buffer or console size. When the signal pipe closes, the thread signals the main loop so pending input waits can end cleanly.

Data usually flows in two directions. Output from a console application arrives as ConDrv API messages, is applied to the local console and screen state, and is serialized as VT to the terminal. Input from the terminal arrives as VT bytes, is parsed into keyboard, text, control, or terminal-response messages, and is then exposed to the console application through the input APIs. This is the core bridge that lets Windows console programs run behind a modern terminal frontend while still observing Windows console semantics.
