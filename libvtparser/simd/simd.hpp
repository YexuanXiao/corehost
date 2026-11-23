#pragma once

#include "utility/isa_features.hpp"
#include "utility/simd_support.hpp"

#include <bit>
#include <cstddef>
#include <cstdint>

#if COREHOST_X64_SIMD
#include <immintrin.h>
#endif

namespace corehost::vtparser::simd
{

// Each backend is an independent inline function. The dispatcher selects one
// and the owning module stores it as a member function pointer.

inline std::size_t decode_ascii_scalar(const char8_t *input, std::size_t size, char16_t *output) noexcept
{
    std::size_t count = 0;
    while (count < size && input[count] < 0x80)
    {
        output[count] = static_cast<char16_t>(input[count]);
        ++count;
    }
    return count;
}

inline std::size_t ground_text_scalar(const char16_t *text, std::size_t size, bool &ascii) noexcept
{
    std::size_t count = 0;
    while (count < size)
    {
        const auto ch = text[count];
        if (ch >= 0x80)
            ascii = false;
        if ((ch < 0x20) || (ch == 0x7F))
            break;
        ++count;
    }
    return count;
}

#if COREHOST_X64_SIMD

inline COREHOST_SIMD_NOINLINE std::size_t decode_ascii_sse42(const char8_t *input, std::size_t size,
                                                             char16_t *output) noexcept
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

inline COREHOST_SIMD_NOINLINE std::size_t ground_text_sse42(const char16_t *text, std::size_t size,
                                                            bool &ascii) noexcept
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

inline COREHOST_SIMD_NOINLINE std::size_t decode_ascii_avx2(const char8_t *input, std::size_t size,
                                                            char16_t *output) noexcept
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

inline COREHOST_SIMD_NOINLINE std::size_t ground_text_avx2(const char16_t *text, std::size_t size, bool &ascii) noexcept
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

inline COREHOST_SIMD_NOINLINE std::size_t decode_ascii_avx10(const char8_t *input, std::size_t size,
                                                             char16_t *output) noexcept
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

inline COREHOST_SIMD_NOINLINE std::size_t ground_text_avx10(const char16_t *text, std::size_t size,
                                                            bool &ascii) noexcept
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

#endif // COREHOST_X64_SIMD

using decode_ascii_function = std::size_t (*)(const char8_t *, std::size_t, char16_t *) noexcept;
using ground_text_function = std::size_t (*)(const char16_t *, std::size_t, bool &) noexcept;

inline decode_ascii_function select_decode_backend() noexcept
{
    switch (corehost::isa::detect_level())
    {
#if COREHOST_X64_SIMD
    case corehost::isa::level::avx10_1:
        return &decode_ascii_avx10;
    case corehost::isa::level::avx2:
        return &decode_ascii_avx2;
    case corehost::isa::level::sse42:
        return &decode_ascii_sse42;
#endif
    case corehost::isa::level::scalar:
    default:
        return &decode_ascii_scalar;
    }
}

inline ground_text_function select_ground_backend() noexcept
{
    switch (corehost::isa::detect_level())
    {
#if COREHOST_X64_SIMD
    case corehost::isa::level::avx10_1:
        return &ground_text_avx10;
    case corehost::isa::level::avx2:
        return &ground_text_avx2;
    case corehost::isa::level::sse42:
        return &ground_text_sse42;
#endif
    case corehost::isa::level::scalar:
    default:
        return &ground_text_scalar;
    }
}

} // namespace corehost::vtparser::simd
