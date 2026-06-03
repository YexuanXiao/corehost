#pragma once

#include <windows.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct _PseudoConsole
{
    HANDLE hSignal;
    HANDLE hPtyReference;
    HANDLE hConPtyProcess;
} PseudoConsole;

#define PTY_SIGNAL_SHOWHIDE_WINDOW  (1u)
#define PTY_SIGNAL_CLEAR_WINDOW     (2u)
#define PTY_SIGNAL_REPARENT_WINDOW  (3u)
#define PTY_SIGNAL_RESIZE_WINDOW    (8u)

#ifndef PSEUDOCONSOLE_INHERIT_CURSOR
#define PSEUDOCONSOLE_INHERIT_CURSOR (0x1)
#endif

#ifndef PSEUDOCONSOLE_GLYPH_WIDTH__MASK
#define PSEUDOCONSOLE_GLYPH_WIDTH__MASK      0x18
#define PSEUDOCONSOLE_GLYPH_WIDTH_GRAPHEMES  0x08
#define PSEUDOCONSOLE_GLYPH_WIDTH_WCSWIDTH   0x10
#define PSEUDOCONSOLE_GLYPH_WIDTH_CONSOLE    0x18
#endif

#ifndef PSEUDOCONSOLE_AMBIGUOUS_IS_WIDE
#define PSEUDOCONSOLE_AMBIGUOUS_IS_WIDE      0x20
#endif

// Function Description:
// - Creates a new PseudoConsole for the given size and input/output handles.
// Arguments:
// - size:    The dimensions of the conpty, in characters.
// - hInput:  The input handle for the conpty (client→conhost).
// - hOutput: The output handle for the conpty (conhost→client).
// - dwFlags: Creation flags (PSEUDOCONSOLE_*).
// - phPC:    Receives the HPCON handle.
// Return Value:
// - S_OK on success, otherwise an appropriate HRESULT.
HRESULT WINAPI ConptyCreatePseudoConsole(
    _In_ COORD size,
    _In_ HANDLE hInput,
    _In_ HANDLE hOutput,
    _In_ DWORD dwFlags,
    _Out_ HPCON* phPC);

// Function Description:
// - Creates a new PseudoConsole as a specific user.
// Arguments:
// - hToken:  User token (or nullptr for current user).
// - size:    The dimensions of the conpty, in characters.
// - hInput:  The input handle for the conpty.
// - hOutput: The output handle for the conpty.
// - dwFlags: Creation flags (PSEUDOCONSOLE_*).
// - phPC:    Receives the HPCON handle.
HRESULT WINAPI ConptyCreatePseudoConsoleAsUser(
    _In_ HANDLE hToken,
    _In_ COORD size,
    _In_ HANDLE hInput,
    _In_ HANDLE hOutput,
    _In_ DWORD dwFlags,
    _Out_ HPCON* phPC);

// Function Description:
// - Resizes the conpty. Sends a PTY_SIGNAL_RESIZE_WINDOW message.
// Arguments:
// - hPC:  The PseudoConsole handle.
// - size: The new dimensions, in characters.
HRESULT WINAPI ConptyResizePseudoConsole(
    _In_ HPCON hPC,
    _In_ COORD size);

// Function Description:
// - Clears the conpty buffer. Sends a PTY_SIGNAL_CLEAR_WINDOW message.
// - NOTE: Not currently exported from kernel32; provided for completeness.
HRESULT WINAPI ConptyClearPseudoConsole(
    _In_ HPCON hPC,
    _In_ BOOL keepCursorRow);

// Function Description:
// - Shows or hides the internal conpty window. Sends PTY_SIGNAL_SHOWHIDE_WINDOW.
// Arguments:
// - hPC:  The PseudoConsole handle.
// - show: true to show, false to hide.
HRESULT WINAPI ConptyShowHidePseudoConsole(
    _In_ HPCON hPC,
    _In_ BOOL show);

// Function Description:
// - Reparents the conpty's internal window to the given parent.
// Arguments:
// - hPC:       The PseudoConsole handle.
// - newParent: The new parent window handle.
HRESULT WINAPI ConptyReparentPseudoConsole(
    _In_ HPCON hPC,
    _In_ HWND newParent);

// Function Description:
// - Closes all members of a PseudoConsole and frees the HPCON.
// Arguments:
// - hPC: The PseudoConsole handle to close.
VOID WINAPI ConptyClosePseudoConsole(
    _In_ HPCON hPC);

// Function Description:
// - Releases the \Reference handle, allowing conhost to exit
//   once the last client disconnects.
HRESULT WINAPI ConptyReleasePseudoConsole(
    _In_ HPCON hPC);

// Function Description:
// - Packs loose handle information for an inbound ConPTY session
//   into an HPCON (the same kind as a created session).
// Arguments:
// - hProcess: Process handle to conhost/OpenConsole.
// - hRef:     \Reference handle.
// - hSignal:  Signal pipe handle.
// - phPC:     Receives the HPCON handle.
HRESULT WINAPI ConptyPackPseudoConsole(
    _In_ HANDLE hProcess,
    _In_ HANDLE hRef,
    _In_ HANDLE hSignal,
    _Out_ HPCON* phPC);

#define CreatePseudoConsole   ConptyCreatePseudoConsole
#define ResizePseudoConsole   ConptyResizePseudoConsole
#define ClosePseudoConsole    ConptyClosePseudoConsole
#define ClearPseudoConsole    ConptyClearPseudoConsole
#define ReleasePseudoConsole  ConptyReleasePseudoConsole

#ifdef __cplusplus
}
#endif
