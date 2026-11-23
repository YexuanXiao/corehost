#pragma once

#include "utility/simd_support.hpp"

#include <cstddef>

namespace corehost::conptyio::simd
{

[[nodiscard]] const char8_t *find_legacy_sgr_scalar(const char8_t *data, std::size_t size) noexcept;
[[nodiscard]] const char8_t *find_legacy_sgr_sse42(const char8_t *data, std::size_t size) noexcept;
[[nodiscard]] const char8_t *find_legacy_sgr_avx2(const char8_t *data, std::size_t size) noexcept;
[[nodiscard]] const char8_t *find_legacy_sgr_avx10(const char8_t *data, std::size_t size) noexcept;

using find_legacy_sgr_function = const char8_t *(*)(const char8_t *, std::size_t) noexcept;

[[nodiscard]] find_legacy_sgr_function select_legacy_sgr_backend() noexcept;

} // namespace corehost::conptyio::simd
