#include "simd.hpp"

#include "utility/isa_features.hpp"

namespace corehost::vtparser::simd
{
namespace
{

using decode_fn = std::size_t (*)(const char8_t *, std::size_t, char16_t *) noexcept;
using ground_fn = std::size_t (*)(const char16_t *, std::size_t, bool &) noexcept;

[[nodiscard]] decode_fn select_decode() noexcept
{
    switch (corehost::isa::detect_level())
    {
#ifdef COREHOST_SIMD_HAVE_AVX10
    case corehost::isa::level::avx10_1:
        return &decode_ascii_avx10;
#endif
#ifdef COREHOST_SIMD_HAVE_AVX2
    case corehost::isa::level::avx2:
        return &decode_ascii_avx2;
#endif
#ifdef COREHOST_SIMD_HAVE_SSE42
    case corehost::isa::level::sse42:
        return &decode_ascii_sse42;
#endif
    case corehost::isa::level::scalar:
    default:
        return &decode_ascii_scalar;
    }
}

[[nodiscard]] ground_fn select_ground() noexcept
{
    switch (corehost::isa::detect_level())
    {
#ifdef COREHOST_SIMD_HAVE_AVX10
    case corehost::isa::level::avx10_1:
        return &ground_text_avx10;
#endif
#ifdef COREHOST_SIMD_HAVE_AVX2
    case corehost::isa::level::avx2:
        return &ground_text_avx2;
#endif
#ifdef COREHOST_SIMD_HAVE_SSE42
    case corehost::isa::level::sse42:
        return &ground_text_sse42;
#endif
    case corehost::isa::level::scalar:
    default:
        return &ground_text_scalar;
    }
}

} // namespace

decode_ascii_function select_decode_backend() noexcept
{
    return select_decode();
}

ground_text_function select_ground_backend() noexcept
{
    return select_ground();
}

} // namespace corehost::vtparser::simd
