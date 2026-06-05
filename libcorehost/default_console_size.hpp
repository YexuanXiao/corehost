// ── conpty/default_console_size.hpp ─────────────────────
// corehost/libcorehost 共用的默认终端尺寸。
#pragma once
#include <windows.h>

namespace corehost::conpty
{

// 默认尺寸统一使用 120x30；width/height 为 0 的入口配置会回退到该值。
inline constexpr COORD default_console_size{120, 30};

} // namespace corehost::conpty
