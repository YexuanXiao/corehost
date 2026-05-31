# Console Feature List

This is the manual-test coverage map for the implemented console host behavior.
The list is intentionally fuller than the current executable set; new manual
checks should be added against this index.

## Console Session And Handoff

- COM embedding handoff from inbox conhost to corehost.
- Startup title transfer from `CONSOLE_SERVER_MSG::Title`.
- `STARTF_USESHOWWINDOW` / `wShowWindow` transfer to Windows Terminal.
- Default text area size managed as 120x30.
- ConDrv server, input, output, reference and signal handles kept alive for the
  session.
- Pty signal pipe handling for show/hide, reparent, resize and close.
- Default terminal entry, headless entry and client-launch entry.
- Text measurement modes: console, graphemes and wcswidth.
- Ambiguous-width mode for East Asian ambiguous characters.
- Initial cursor inheritance by DSR/CPR.

## VT Output Parser And Dispatch

- Printable text, CR, LF and CRLF handling.
- Cursor movement: CUU, CUD, CUF, CUB, CNL, CPL, CHA, VPA, CUP/HVP.
- Tab movement: HT, CHT, CBT, HTS, TBC current and TBC all.
- Cursor save/restore: DEC `ESC 7` / `ESC 8` and ANSI `CSI s` / `CSI u`.
- Cursor visibility and blinking: `CSI ?25 h/l`, `CSI ?12 h/l`.
- Cursor shape: DECSCUSR `CSI Ps SP q`.
- Erase operations: ED 0/1/2 and EL 0/1/2.
- Text modification: ICH, DCH, ECH, IL, DL.
- Scrolling: SU, SD, DECSTBM scrolling region and reverse index.
- SGR: reset, bold, faint, italic, underline, blink, reverse, conceal,
  strikethrough.
- SGR colors: 16-color foreground/background, default foreground/background,
  24-bit RGB foreground/background.
- OSC title: OSC 0 / OSC 2.
- OSC palette: OSC 4 `rgb:RR/GG/BB`.
- DEC line drawing and ASCII charset designation: `ESC ( 0`, `ESC ( B`.
- Alternate screen buffer: `CSI ?1049 h/l`.
- Column mode requests: DECCOLM 132 and 80 columns.
- Soft reset: DECSTR.
- Device reports: DA, DSR cursor-position request, CPR response.
- Window resize notification: `CSI 8 ; rows ; cols t`.
- Keypad application/numeric mode.
- Cursor-key application/normal mode.
- SS3 and CSI keyboard sequences for arrows, Home/End, Insert/Delete,
  PageUp/PageDown and F1-F12.
- Ctrl-arrow keyboard variants.
- C0 input controls: DEL, SUB, ESC and NUL.
- Win32 Input Mode key packets: `CSI Vk;Sc;Uc;Kd;Cs;Rc _`.
- OSC filtering for ConDrv-private OSC payloads before visible output.

## Console API Layer 1

- GetConsoleCP / GetConsoleOutputCP.
- GetConsoleMode and SetConsoleMode.
- GetNumberOfConsoleInputEvents.
- ReadConsoleInput.
- ReadConsole.
- WriteConsole.
- GetConsoleLangId.
- Deprecated L1 compatibility completions.

## Console API Layer 2

- FillConsoleOutputCharacter and FillConsoleOutputAttribute.
- GenerateConsoleCtrlEvent.
- SetConsoleActiveScreenBuffer.
- FlushConsoleInputBuffer.
- SetConsoleCP / SetConsoleOutputCP.
- GetConsoleCursorInfo / SetConsoleCursorInfo.
- GetConsoleScreenBufferInfo / SetConsoleScreenBufferInfo.
- SetConsoleScreenBufferSize.
- SetConsoleCursorPosition.
- GetLargestConsoleWindowSize.
- ScrollConsoleScreenBuffer.
- SetConsoleTextAttribute.
- SetConsoleWindowInfo.
- ReadConsoleOutputCharacter / ReadConsoleOutputAttribute.
- WriteConsoleInput.
- WriteConsoleOutput.
- WriteConsoleOutputCharacter / WriteConsoleOutputAttribute.
- ReadConsoleOutput.
- GetConsoleTitle / GetConsoleOriginalTitle.
- SetConsoleTitle.

## Console API Layer 3

- GetNumberOfConsoleMouseButtons.
- GetConsoleFontSize.
- GetCurrentConsoleFont.
- SetConsoleDisplayMode / GetConsoleDisplayMode.
- AddConsoleAlias.
- GetConsoleAlias.
- GetConsoleAliasesLength.
- GetConsoleAliasExesLength.
- GetConsoleAliases.
- GetConsoleAliasExes.
- ExpungeConsoleCommandHistory.
- SetConsoleNumberOfCommands.
- GetConsoleCommandHistoryLength.
- GetConsoleCommandHistory.
- GetConsoleWindow.
- GetConsoleSelectionInfo.
- GetConsoleProcessList.
- GetConsoleHistoryInfo / SetConsoleHistoryInfo.
- SetCurrentConsoleFontEx.
- Deprecated L3 compatibility completions.

## Input Editing And Console Read

- Cooked line input with echo.
- Raw input records.
- KEY_EVENT generation from VT keyboard input.
- Win32 Input Mode key event generation.
- Backspace, Delete, Left/Right, Home/End, Up/Down history browsing.
- Ctrl-arrow movement.
- Enter completion with CRLF echo.
- EOF / Ctrl+Z behavior.
- Input flushing.
- Command history storage and duplicate suppression.
- DOSKEY alias expansion.
- PSReadLine-oriented single-record input behavior.
- Cursor synchronization after echoed input and WriteConsole output.

## Screen Buffer And Unicode

- Main and alternate screen buffers.
- Screen snapshots when switching buffers.
- Cell attributes and BGR-to-SGR color mapping.
- CHAR_INFO read/write conversion.
- Wide-character trailing cell tracking.
- Combining mark and zero-width character handling.
- Grapheme cluster width.
- Emoji, variation selector and supplementary-plane text.
- CJK full-width wrapping at line boundaries.
- Ambiguous-width characters in narrow/wide modes.
- DEC line drawing character conversion.
- Tab stop management.
