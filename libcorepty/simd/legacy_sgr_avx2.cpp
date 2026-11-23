#include "legacy_sgr.hpp"

#if COREHOST_X64_SIMD
#include <immintrin.h>

#include <bit>
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

COREHOST_SIMD_NOINLINE const char8_t *find_legacy_sgr_avx2(const char8_t *data, std::size_t size) noexcept
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
            if (candidate + legacy_sgr_size > size)
                return find_legacy_sgr_scalar(data + offset, size - offset);
            if (matches(data + candidate))
                return data + candidate;
            mask &= mask - 1;
        }
        offset += 32;
    }

    return find_legacy_sgr_scalar(data + offset, size - offset);
}

} // namespace corehost::conptyio::simd
#endif
