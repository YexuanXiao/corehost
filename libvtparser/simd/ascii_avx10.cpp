#include "simd.hpp"

#if COREHOST_X64_SIMD
#include <immintrin.h>

#include <bit>
#include <cstdint>

namespace corehost::vtparser::simd
{

COREHOST_SIMD_NOINLINE std::size_t decode_ascii_avx10(const char8_t *input, std::size_t size, char16_t *output) noexcept
{
    std::size_t count = 0;
    while (count + 64 <= size)
    {
        const auto bytes = _mm512_loadu_si512(reinterpret_cast<const void *>(input + count));
        if (_mm512_movepi8_mask(bytes) != 0)
            break;

        const auto low_bytes = _mm512_castsi512_si256(bytes);
        const auto high_bytes = _mm512_extracti64x4_epi64(bytes, 1);
        const auto low = _mm512_cvtepu8_epi16(low_bytes);
        const auto high = _mm512_cvtepu8_epi16(high_bytes);
        _mm512_storeu_si512(reinterpret_cast<void *>(output + count), low);
        _mm512_storeu_si512(reinterpret_cast<void *>(output + count + 32), high);
        count += 64;
    }

    return count + decode_ascii_scalar(input + count, size - count, output + count);
}

COREHOST_SIMD_NOINLINE std::size_t ground_text_avx10(const char16_t *text, std::size_t size, bool &ascii) noexcept
{
    const auto control_limit = _mm512_set1_epi16(0x1F);
    const auto del = _mm512_set1_epi16(0x7F);
    std::size_t count = 0;

    while (count + 32 <= size)
    {
        const auto units = _mm512_loadu_si512(reinterpret_cast<const void *>(text + count));
        const auto control = _mm512_cmpeq_epi16_mask(_mm512_min_epu16(units, control_limit), units);
        const auto is_del = _mm512_cmpeq_epi16_mask(units, del);
        const auto high_mask = static_cast<unsigned>(_mm512_movepi16_mask(units));
        const auto mask = static_cast<unsigned>(control | is_del);
        if (mask != 0)
        {
            const auto index = std::countr_zero(mask);
            if ((high_mask & ((1u << index) - 1u)) != 0)
                ascii = false;
            return count + index;
        }

        if (high_mask != 0)
            ascii = false;
        count += 32;
    }

    return count + ground_text_scalar(text + count, size - count, ascii);
}

} // namespace corehost::vtparser::simd
#endif
