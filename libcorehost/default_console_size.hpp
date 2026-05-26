// ── conpty/default_console_size.hpp ─────────────────────
// corehost/libcorehost 共用的默认终端尺寸。
#pragma once
#include <windows.h>

namespace conpty
{

inline constexpr COORD default_console_size{120, 30};

} // namespace conpty
