#pragma once

// Worker side for long-line-three-vt-output. Each emitted line is longer than
// 200 visible characters and contains exactly three SGR VT sequences, modeling
// ordinary long line output with only a small amount of styling.

#include "terminal_output_worker.hpp"

namespace bench
{

inline constexpr std::string_view long_line_three_vt_output_line =
    "\x1b[0m"
    "ASCII abcdefghijklmnopqrstuvwxyz 0123456789 "
    "long line payload payload payload payload payload payload payload payload "
    "ordinary text before the styled section, still on the same console row "
    "\x1b[1m"
    "BoldMiddleSegment "
    "ASCII abcdefghijklmnopqrstuvwxyz 0123456789 "
    "喜欢你 核心终端性能测试 "
    "tail payload payload payload payload payload payload payload payload "
    "more ordinary text so the visible line is comfortably above two hundred characters "
    "\x1b[0m"
    "\r\n";

// --emit-long-line-3vt <target-bytes> [marker] [ready-marker] [trigger-event]
inline int emit_long_line_three_vt_to_stdout(size_t target_bytes, const wchar_t *marker, const wchar_t *ready_marker,
                                             const wchar_t *trigger_event_name)
{
    return emit_repeated_output_line_unbuffered(long_line_three_vt_output_line, target_bytes, marker, ready_marker,
                                                trigger_event_name);
}

} // namespace bench
