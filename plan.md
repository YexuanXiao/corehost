# noexcept audit plan

## Goal

Audit project functions and make exception specifications explicit. Per project rule,
allocation failure is treated as non-observable for this audit, so functions whose
only possible exception is OOM should be marked `noexcept`.

Functions that intentionally report recoverable resource or Win32 failures by
throwing must not be marked `noexcept`. Resource-acquisition constructors such as
`win32::event` and `win32::basic_thread` keep their throwing contract so callers
can distinguish failure from successful construction.

## Rules

1. Add `noexcept` to pure state accessors, small mutators, parsers that only mutate internal state, and formatting/conversion helpers whose only possible failure is allocation.
2. Keep `noexcept` on destructors, move operations, RAII `clear/release/get/valid` helpers, and best-effort diagnostic paths.
3. Remove `noexcept` when the function reports recoverable errors by throwing.
4. Preserve Windows ABI requirements: COM methods and thread procedures keep
   their required signatures; add `noexcept` only where compatible.

## Audit Order

1. `libcorehost` hot path:
   - `vt_parser`, `pipe_bridge`, `api_handlers`, `vt_msg_dispatch`, `screen_buffer`, `console_state`, input/history helpers.
   - Add `noexcept` to state-only helpers and parser reset/classification paths.
   - API handlers that only mutate local console state or serialize local
     buffers should be marked `noexcept`.
   - Handlers that call ConDrv read/complete paths, wait wrappers, or other
     recoverable Win32 error channels must remain throwing.
   - Progress:
     - Parser, screen buffer, console state, VT output buffer, state-only API
       handlers, and local pipe bridge editing/VT serialization helpers are
       explicit `noexcept`.
     - Pipe bridge completion-preparation helpers and CPR inheritance handling
       are explicit `noexcept`; they only mutate local buffers/state and do not
       submit ConDrv completion.
     - ConDrv payload read/completion, wait wrappers, and pending completion
       exits remain throwing.

2. `common/win32` wrappers:
   - RAII types should be consistently `noexcept` for ownership operations.
   - Throwing wrappers such as `create_pipe`, `duplicate_handle`, registry
     open/query, wait wrappers, and file read helpers keep throwing signatures
     unless they already return explicit status.
   - Non-throwing status-returning wrappers, such as sync I/O result helpers, remain `noexcept`.
   - Progress:
     - Resource acquisition and recoverable error wrappers were rechecked and
       left throwing.
     - Ownership-only handle operations remain explicit `noexcept`.

3. `corehost` entry modules:
   - Entry points and high-level loops should be marked `noexcept` unless an
     external ABI forbids it.
   - COM callbacks stay `noexcept` and convert failures to HRESULT/notification results.
   - Defterm notification/report helpers that are best-effort and catch internally remain `noexcept`.
   - Progress:
     - Entry and wait-heavy paths were not mechanically marked. Wait/COM/
       CreateProcess failure channels remain visible.
     - COM attach descriptor conversion, CONNECT title parsing, defterm GUI
       start policy, best-effort notification helpers, and signal-pipe payload
       readers now have explicit `noexcept`.
     - COM handoff, WT handoff, ConDrv accept/read, and process/handle creation
       paths remain throwing or HRESULT-returning.
     - Common path/query helpers that return empty on failure and internal
       NTAPI function-pointer shims are explicit `noexcept`; ConDrv open/create
       helpers remain throwing.

4. `bench` and `tests`:
   - Only adjust helper utilities that are clearly non-throwing or OOM-only.
   - Test bodies can remain throwing/asserting; they are not production contracts.
   - Progress:
     - No broad bench/test noexcept pass was applied in this step.

## Current Result

- Added explicit `noexcept` to local state mutation, parser/reset,
  screen-buffer, conversion-buffer, VT output serialization, and state-only
  Console API handlers.
- Kept ConDrv `read_input` / `complete_io`, `wait_one` / `wait_any`, event and
  thread creation, registry access, client process creation, and file read
  wrappers throwing.
- Remaining unmarked `pipe_bridge` input functions are mostly pending-read
  orchestration paths that may complete ConDrv requests; they should not be
  marked without further splitting completion from local editing.
- Remaining scan hits are classified:
  - `char_convert.hpp` helper hits already have `noexcept` on the following
    line; the regex reports them because the signature wraps.
  - `condrv_io`, `pipe_bridge_io::complete/read_input`, `io_loop`,
    `message_router` dispatch, `api_router` dispatch, `io_state` object
    handlers, defterm entry loops, and client/VT handle initialization keep
    throwing contracts because they report ConDrv, wait, process, or handle
    creation errors.
  - `pipe_bridge` input orchestration functions keep throwing because many of
    them can indirectly call `complete_pending()` / `_io.complete()`.
- `skip_bytes` now treats a zero-byte synchronous read as failure instead of
  looping with an unchanged remaining byte count.

## Verification

1. Search for suspicious mismatches:
   - `rg "noexcept" common corehost libcorehost bench tests`
   - `rg "throw|throw_last_error|win32::error" common corehost libcorehost bench tests`
2. Build Debug:
   - `cmake --build build --config Debug`
3. Run focused tests:
   - `ctest --test-dir build -C Debug -R "VT|Keyboard|ConPTY|ConsoleState" --output-on-failure`
4. If broad noexcept changes touch common wrappers, run full Debug tests.
