#pragma once

#include "win32/error.hpp"

#include <cstdlib>
#include <string>
#include <string_view>

#include <Windows.h>

namespace utility
{

// ── CRT error dialog suppression ────────────────────────────────────
// Prevents the CRT from displaying message boxes for assertions, invalid
// parameter errors, and abort() calls. These dialogs would block automated
// test runs and headless server processes. Must be called early in process
// startup (before any CRT function that could trigger a dialog).
//
// Declares minimal CRT debug API signatures locally to avoid including
// <crtdbg.h> which can cause macro conflicts with standard library headers.

inline void suppress_crt_error_dialogs() noexcept
{
    // Suppress system-level error dialogs (critical errors, GPF).
    ::SetErrorMode(SEM_NOGPFAULTERRORBOX | SEM_FAILCRITICALERRORS);

    // Suppress CRT assertion / invalid parameter message boxes
    // by directing all report types to the debug output only.
    // Report type constants: 0=CRT_WARN, 1=CRT_ERROR, 2=CRT_ASSERT.
    // Report mode 2 = _CRTDBG_MODE_DEBUG (write to debug output).
    constexpr int kCrtWarn = 0;
    constexpr int kCrtError = 1;
    constexpr int kCrtAssert = 2;
    constexpr int kModeDebug = 2;
    _CrtSetReportMode(kCrtAssert, kModeDebug);
    _CrtSetReportMode(kCrtError, kModeDebug);
    _CrtSetReportMode(kCrtWarn, kModeDebug);

    // Prevent abort() from showing a "Retry, Abort, Ignore" dialog.
    // The process terminates immediately instead.
    ::_set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
}

} // namespace utility
