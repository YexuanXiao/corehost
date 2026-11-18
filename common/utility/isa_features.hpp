#pragma once

namespace corehost::isa
{

enum class level
{
    scalar,
    sse42,
    avx2,
    avx10_1,
};

// Shared CPU feature query. SIMD implementations themselves stay in the
// modules that use them (libvtparser / libcorepty).
[[nodiscard]] level detect_level() noexcept;

} // namespace corehost::isa
