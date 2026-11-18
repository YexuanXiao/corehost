#include "legacy_sgr.hpp"

#include "utility/isa_features.hpp"

namespace corehost::conptyio::simd
{
namespace
{

using find_fn = const char8_t *(*)(const char8_t *, std::size_t) noexcept;

[[nodiscard]] find_fn select_backend() noexcept
{
    switch (corehost::isa::detect_level())
    {
#ifdef COREHOST_SIMD_HAVE_AVX10
    case corehost::isa::level::avx10_1:
        return &find_legacy_sgr_avx10;
#endif
#ifdef COREHOST_SIMD_HAVE_AVX2
    case corehost::isa::level::avx2:
        return &find_legacy_sgr_avx2;
#endif
#ifdef COREHOST_SIMD_HAVE_SSE42
    case corehost::isa::level::sse42:
        return &find_legacy_sgr_sse42;
#endif
    case corehost::isa::level::scalar:
    default:
        return &find_legacy_sgr_scalar;
    }
}

} // namespace

find_legacy_sgr_function select_legacy_sgr_backend() noexcept
{
    return select_backend();
}

} // namespace corehost::conptyio::simd
