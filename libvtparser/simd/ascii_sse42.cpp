#include "simd.hpp"

#include <immintrin.h>

#include <bit>
#include <cstdint>

#if defined(_MSC_VER)
#define COREHOST_SIMD_NOINLINE __declspec(noinline)
#else
#define COREHOST_SIMD_NOINLINE
#endif

namespace corehost::vtparser::simd
{

COREHOST_SIMD_NOINLINE std::size_t decode_ascii_sse42(const char8_t *input, std::size_t size, char16_t *output) noexcept
{
    std::size_t count = 0;
    while (count + 16 <= size)
    {
        const auto bytes = _mm_loadu_si128(reinterpret_cast<const __m128i *>(input + count));
        if (_mm_movemask_epi8(bytes) != 0)
            break;

        const auto low = _mm_cvtepu8_epi16(bytes);
        const auto high = _mm_cvtepu8_epi16(_mm_srli_si128(bytes, 8));
        _mm_storeu_si128(reinterpret_cast<__m128i *>(output + count), low);
        _mm_storeu_si128(reinterpret_cast<__m128i *>(output + count + 8), high);
        count += 16;
    }

    return count + decode_ascii_scalar(input + count, size - count, output + count);
}

COREHOST_SIMD_NOINLINE std::size_t ground_text_sse42(const char16_t *text, std::size_t size, bool &ascii) noexcept
{
    const auto control_limit = _mm_set1_epi16(0x1F);
    const auto ascii_limit = _mm_set1_epi16(0x7F);
    const auto del = _mm_set1_epi16(0x7F);
    std::size_t count = 0;

    while (count + 8 <= size)
    {
        const auto units = _mm_loadu_si128(reinterpret_cast<const __m128i *>(text + count));
        const auto ascii_units = _mm_cmpeq_epi16(_mm_min_epu16(units, ascii_limit), units);
        const auto non_ascii_mask = (~static_cast<unsigned>(_mm_movemask_epi8(ascii_units))) & 0xAAAAu;
        const auto control = _mm_cmpeq_epi16(_mm_min_epu16(units, control_limit), units);
        const auto is_del = _mm_cmpeq_epi16(units, del);
        const auto mask = static_cast<unsigned>(_mm_movemask_epi8(_mm_or_si128(control, is_del)));
        if (mask != 0)
        {
            const auto index = std::countr_zero(mask) >> 1;
            if ((non_ascii_mask & ((1u << (index * 2 + 1)) - 1u)) != 0)
                ascii = false;
            return count + index;
        }

        if (non_ascii_mask != 0)
            ascii = false;
        count += 8;
    }

    return count + ground_text_scalar(text + count, size - count, ascii);
}

} // namespace corehost::vtparser::simd
