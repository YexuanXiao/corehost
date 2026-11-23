#pragma once

#include "utility/isa_features.hpp"
#include "utility/simd_support.hpp"

#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>

#if COREHOST_X64_SIMD
#include <immintrin.h>
#endif

namespace corehost::conptyio::simd
{
namespace detail
{
inline constexpr char8_t legacy_sgr[] = u8"\x1b[0;37;40m";
inline constexpr std::size_t legacy_sgr_size = sizeof(legacy_sgr) - 1;

[[nodiscard]] inline bool matches(const char8_t *candidate) noexcept
{
    return std::memcmp(candidate, legacy_sgr, legacy_sgr_size) == 0;
}
} // namespace detail

inline const char8_t *find_legacy_sgr_scalar(const char8_t *data, std::size_t size) noexcept
{
    for (std::size_t i = 0; i + detail::legacy_sgr_size <= size; ++i)
    {
        if (detail::matches(data + i))
            return data + i;
    }
    return nullptr;
}

#if COREHOST_X64_SIMD

inline COREHOST_SIMD_NOINLINE const char8_t *find_legacy_sgr_sse42(const char8_t *data, std::size_t size) noexcept
{
    const auto escape = _mm_set1_epi8(0x1B);
    std::size_t offset = 0;
    while (offset + 16 <= size)
    {
        const auto bytes = _mm_loadu_si128(reinterpret_cast<const __m128i *>(data + offset));
        unsigned mask = static_cast<unsigned>(_mm_movemask_epi8(_mm_cmpeq_epi8(bytes, escape)));
        while (mask != 0)
        {
            const auto candidate = offset + std::countr_zero(mask);
            if (candidate + detail::legacy_sgr_size > size)
                return find_legacy_sgr_scalar(data + offset, size - offset);
            if (detail::matches(data + candidate))
                return data + candidate;
            mask &= mask - 1;
        }
        offset += 16;
    }

    return find_legacy_sgr_scalar(data + offset, size - offset);
}

inline COREHOST_SIMD_NOINLINE const char8_t *find_legacy_sgr_avx2(const char8_t *data, std::size_t size) noexcept
{
    const auto escape = _mm256_set1_epi8(0x1B);
    std::size_t offset = 0;
    while (offset + 32 <= size)
    {
        const auto bytes = _mm256_loadu_si256(reinterpret_cast<const __m256i *>(data + offset));
        unsigned mask = static_cast<unsigned>(_mm256_movemask_epi8(_mm256_cmpeq_epi8(bytes, escape)));
        while (mask != 0)
        {
            const auto candidate = offset + std::countr_zero(mask);
            if (candidate + detail::legacy_sgr_size > size)
                return find_legacy_sgr_scalar(data + offset, size - offset);
            if (detail::matches(data + candidate))
                return data + candidate;
            mask &= mask - 1;
        }
        offset += 32;
    }

    return find_legacy_sgr_scalar(data + offset, size - offset);
}

inline COREHOST_SIMD_NOINLINE const char8_t *find_legacy_sgr_avx10(const char8_t *data, std::size_t size) noexcept
{
    const auto escape = _mm512_set1_epi8(0x1B);
    std::size_t offset = 0;
    while (offset + 64 <= size)
    {
        const auto bytes = _mm512_loadu_si512(reinterpret_cast<const void *>(data + offset));
        std::uint64_t mask = static_cast<std::uint64_t>(_mm512_cmpeq_epi8_mask(bytes, escape));
        while (mask != 0)
        {
            const auto candidate = offset + static_cast<std::size_t>(std::countr_zero(mask));
            if (candidate + detail::legacy_sgr_size > size)
                return find_legacy_sgr_scalar(data + offset, size - offset);
            if (detail::matches(data + candidate))
                return data + candidate;
            mask &= mask - 1;
        }
        offset += 64;
    }

    return find_legacy_sgr_scalar(data + offset, size - offset);
}

#endif // COREHOST_X64_SIMD

using find_legacy_sgr_function = const char8_t *(*)(const char8_t *, std::size_t) noexcept;

inline find_legacy_sgr_function select_legacy_sgr_backend() noexcept
{
    switch (corehost::isa::detect_level())
    {
#if COREHOST_X64_SIMD
    case corehost::isa::level::avx10_1:
        return &find_legacy_sgr_avx10;
    case corehost::isa::level::avx2:
        return &find_legacy_sgr_avx2;
    case corehost::isa::level::sse42:
        return &find_legacy_sgr_sse42;
#endif
    case corehost::isa::level::scalar:
    default:
        return &find_legacy_sgr_scalar;
    }
}

} // namespace corehost::conptyio::simd
