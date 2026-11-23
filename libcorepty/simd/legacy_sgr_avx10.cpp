#include "legacy_sgr.hpp"

#if COREHOST_X64_SIMD
#include <immintrin.h>

#include <bit>
#include <cstdint>
#include <cstring>

namespace corehost::conptyio::simd
{
namespace
{
constexpr char8_t legacy_sgr[] = u8"\x1b[0;37;40m";
constexpr std::size_t legacy_sgr_size = sizeof(legacy_sgr) - 1;

[[nodiscard]] bool matches(const char8_t *candidate) noexcept
{
    return std::memcmp(candidate, legacy_sgr, legacy_sgr_size) == 0;
}
} // namespace

COREHOST_SIMD_NOINLINE const char8_t *find_legacy_sgr_avx10(const char8_t *data, std::size_t size) noexcept
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
            if (candidate + legacy_sgr_size > size)
                return find_legacy_sgr_scalar(data + offset, size - offset);
            if (matches(data + candidate))
                return data + candidate;
            mask &= mask - 1;
        }
        offset += 64;
    }

    return find_legacy_sgr_scalar(data + offset, size - offset);
}

} // namespace corehost::conptyio::simd
#endif
