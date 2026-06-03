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

- **Default terminal handling** — delegates console sessions to Windows Terminal (or other third-party terminals) via COM handoff
- **COM embedding mode** - used to support third-party terminals as the default terminal, compatible with the IConsoleHandoff and ITerminalHandoff protocols used by Windows Terminal.
- **ConPTY** - Pseudo Console

corehost aims to provide **high quality and high performance** for driving:

- Windows Terminal
- Contour
- Alacritty
- ConEmu
- and other Windows terminals

Actually, corehost is the ideal OpenConsole/conhost.

### AllocConsole support

This implementation also solves the problem that the original version of conhost does not support opening the default terminal (e.g., Windows Terminal) when calling `AllocConsole`.

### UAC elevation

When corehost runs with elevated integrity (High IL) due to sudo or runas, UIPI prevents COM activation and handle transfer to Medium IL terminals. When corehost detects this situation, since no GUI is available (the only GUI available to the original conhost is itself), corehost will refuse to execute the program and send a message in the notification center about what happened.

### Roadmap

- [x] Default Terminal Handoff
- [x] COM Embedding Mode
- [x] ConPTY (ongoing)

### How to use it

corehost supports four usage modes.

First, you can directly replace conhost.exe with corehost.exe, and corehost will take over all functionality of conhost, except that it does not support window mode.

Then, you can use the scripts in the scripts directory to register corehost as the default terminal. In this mode, when you launch cmd using Run, corehost will take over the functionality of OpenConsole.

corehost can also be used together with conpty.dll to provide ConPty support for third-party terminals. If you are using conpty.dll released by Microsoft, rename corehost.exe to openconsole.exe; if you are using corehost's libconpty, you can directly use corehost.exe.

Finally, corehost can be compiled directly as a static library. In static library mode, third-party terminals can use conpty directly within the same process, avoiding the overhead of process switching.

### How to build

1. Install the latest VS2026, C++ build tools v14.51 or later
2. Install CMake 4.3 or later
3. `git clone --recurse-submodules https://github.com/YexuanXiao/corehost`
4. `cmake -B build`
5. `cmake --build build --config Release --target corehost`

I plan to support Clang, but it's not a priority at the moment. Therefore, if anyone truly wants to build with Clang, please submit a request, and I will reprioritize accordingly.

### CMake options

| Option | Default | Description |
| --- | --- | --- |
| `COREHOST_DISABLE_LOG` | `ON` | Disables corehost logging. Keep this enabled for normal Release builds and performance testing. Set it to `OFF` when diagnosing behavior with log files. |
| `COREHOST_LOG_LEVEL` | `1` | Compile-time log frequency level. `1` records low-frequency events, `2` also records medium-frequency events, and `3` also records high-frequency events. This only matters when `COREHOST_DISABLE_LOG=OFF`. |
| `COREHOST_PERF_DIAG` | `OFF` | Enables aggregated performance diagnostics inside corehost. This is for profiling and should stay disabled for normal performance comparisons. |
| `COREHOST_ANSI_OPT` | `OFF` | Enables optional table-driven ANSI code page fast paths. For users who have enabled UTF-8, this is unnecessary and will significantly increase the binary size. |
| `COREHOST_USE_SYSTEM_ICU` | `ON` | Uses the Windows system ICU library for Unicode character width calculation, reducing the binary size. Disable it when targeting Windows versions earlier than 1903. |
| `USE_INBOX_CONHOST` | `OFF` | Builds `libconpty` so it always uses the system `conhost.exe` instead of side-loading corehost. |
