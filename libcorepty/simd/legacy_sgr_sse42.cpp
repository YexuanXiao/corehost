#include "legacy_sgr.hpp"

#include <immintrin.h>

#include <bit>
#include <cstring>

#if defined(_MSC_VER)
#define COREHOST_SIMD_NOINLINE __declspec(noinline)
#else
#define COREHOST_SIMD_NOINLINE
#endif

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

COREHOST_SIMD_NOINLINE const char8_t *find_legacy_sgr_sse42(const char8_t *data, std::size_t size) noexcept
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
            if (candidate + legacy_sgr_size > size)
                return find_legacy_sgr_scalar(data + offset, size - offset);
            if (matches(data + candidate))
                return data + candidate;
            mask &= mask - 1;
        }
        offset += 16;
    }

    return find_legacy_sgr_scalar(data + offset, size - offset);
}

} // namespace corehost::conptyio::simd
