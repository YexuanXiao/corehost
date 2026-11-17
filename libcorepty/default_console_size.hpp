// ── corepty2/default_console_size.hpp ───────────────────
// Default terminal dimensions shared by the COM handoff and corepty2 entry.
#pragma once

#include <windows.h>

namespace corehost::conpty
{

inline constexpr COORD default_console_size{120, 30};

} // namespace corehost::conpty
