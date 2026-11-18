#pragma once

#include <cstddef>

namespace corehost::vtparser::simd
{

// Each backend is an independent function. The dispatcher only selects one of
// them; no function contains another ISA implementation behind #ifdef.

[[nodiscard]] std::size_t decode_ascii_scalar(const char8_t *input, std::size_t size, char16_t *output) noexcept;
[[nodiscard]] std::size_t decode_ascii_sse42(const char8_t *input, std::size_t size, char16_t *output) noexcept;
[[nodiscard]] std::size_t decode_ascii_avx2(const char8_t *input, std::size_t size, char16_t *output) noexcept;
[[nodiscard]] std::size_t decode_ascii_avx10(const char8_t *input, std::size_t size, char16_t *output) noexcept;

[[nodiscard]] std::size_t ground_text_scalar(const char16_t *text, std::size_t size, bool &ascii) noexcept;
[[nodiscard]] std::size_t ground_text_sse42(const char16_t *text, std::size_t size, bool &ascii) noexcept;
[[nodiscard]] std::size_t ground_text_avx2(const char16_t *text, std::size_t size, bool &ascii) noexcept;
[[nodiscard]] std::size_t ground_text_avx10(const char16_t *text, std::size_t size, bool &ascii) noexcept;

// Backend selection is performed once when the owning module object is
// constructed, then stored as a member function pointer.
using decode_ascii_function = std::size_t (*)(const char8_t *, std::size_t, char16_t *) noexcept;
using ground_text_function = std::size_t (*)(const char16_t *, std::size_t, bool &) noexcept;

[[nodiscard]] decode_ascii_function select_decode_backend() noexcept;
[[nodiscard]] ground_text_function select_ground_backend() noexcept;

} // namespace corehost::vtparser::simd
