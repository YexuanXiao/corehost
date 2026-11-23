#include "simd.hpp"

#if COREHOST_X64_SIMD
#include <immintrin.h>

#include <bit>
#include <cstdint>

namespace corehost::vtparser::simd
{

COREHOST_SIMD_NOINLINE std::size_t decode_ascii_avx2(const char8_t *input, std::size_t size, char16_t *output) noexcept
{
    std::size_t count = 0;
    while (count + 32 <= size)
    {
        const auto bytes = _mm256_loadu_si256(reinterpret_cast<const __m256i *>(input + count));
        if (_mm256_movemask_epi8(bytes) != 0)
            break;

        const auto low_bytes = _mm256_castsi256_si128(bytes);
        const auto high_bytes = _mm256_extracti128_si256(bytes, 1);
        const auto low = _mm256_cvtepu8_epi16(low_bytes);
        const auto high = _mm256_cvtepu8_epi16(high_bytes);
        _mm256_storeu_si256(reinterpret_cast<__m256i *>(output + count), low);
        _mm256_storeu_si256(reinterpret_cast<__m256i *>(output + count + 16), high);
        count += 32;
    }

    return count + decode_ascii_scalar(input + count, size - count, output + count);
}

COREHOST_SIMD_NOINLINE std::size_t ground_text_avx2(const char16_t *text, std::size_t size, bool &ascii) noexcept
{
    const auto control_limit = _mm256_set1_epi16(0x1F);
    const auto ascii_limit = _mm256_set1_epi16(0x7F);
    const auto del = _mm256_set1_epi16(0x7F);
    std::size_t count = 0;

    while (count + 16 <= size)
    {
        const auto units = _mm256_loadu_si256(reinterpret_cast<const __m256i *>(text + count));
        const auto ascii_units = _mm256_cmpeq_epi16(_mm256_min_epu16(units, ascii_limit), units);
        const auto non_ascii_mask = (~static_cast<unsigned>(_mm256_movemask_epi8(ascii_units))) & 0xAAAAAAAAu;
        const auto control = _mm256_cmpeq_epi16(_mm256_min_epu16(units, control_limit), units);
        const auto is_del = _mm256_cmpeq_epi16(units, del);
        const auto mask = static_cast<unsigned>(_mm256_movemask_epi8(_mm256_or_si256(control, is_del)));
        if (mask != 0)
        {
            const auto index = std::countr_zero(mask) >> 1;
            if ((non_ascii_mask & ((1u << (index * 2 + 1)) - 1u)) != 0)
                ascii = false;
            return count + index;
        }

        if (non_ascii_mask != 0)
            ascii = false;
        count += 16;
    }

    return count + ground_text_scalar(text + count, size - count, ascii);
}

} // namespace corehost::vtparser::simd
#endif
