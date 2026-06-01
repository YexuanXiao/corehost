#pragma once

// Worker side for large-mixed-vt-cjk-output. This code runs inside the ConPTY
// child process and generates complete VT/CJK output lines directly to stdout.

#include "terminal_output_worker.hpp"

namespace bench
{

inline constexpr std::string_view mixed_vt_cjk_line =
    "\x1b[0m"
    "ASCII abcdefghijklmnopqrstuvwxyz 0123456789 "
    "\x1b[1mBold\x1b[22m "
    "\x1b[2mFaint\x1b[22m "
    "\x1b[3mItalic\x1b[23m "
    "\x1b[4mUnderline\x1b[24m "
    "\x1b[5mBlink\x1b[25m "
    "\x1b[7mInverse\x1b[27m "
    "\x1b[8mHidden\x1b[28m "
    "\x1b[9mStrike\x1b[29m "
    "\x1b[31m喜欢你\x1b[39m "
    "\x1b[93mBrightFg\x1b[39m "
    "\x1b[44mBlueBg\x1b[49m "
    "\x1b[102mBrightBg\x1b[49m "
    "\x1b[38;5;202mIndexedFg\x1b[39m "
    "\x1b[48;5;24mIndexedBg\x1b[49m "
    "\x1b[38;2;12;120;220m核心终端性能测试\x1b[39m "
    "\x1b[48;2;32;40;48mRgbBg\x1b[49m "
    "\x1b[1;3;4;38;2;220;80;40mCombined-SGR\x1b[0m "
    "mixed output line payload payload payload payload\r\n";

// --emit-mixed <target-bytes> [marker] [ready-marker] [trigger-event]
inline int emit_mixed_vt_cjk_to_stdout(size_t target_bytes, const wchar_t *marker, const wchar_t *ready_marker,
                                       const wchar_t *trigger_event_name)
{
    return emit_repeated_output_line(mixed_vt_cjk_line, target_bytes, marker, ready_marker, trigger_event_name);
}

} // namespace bench
