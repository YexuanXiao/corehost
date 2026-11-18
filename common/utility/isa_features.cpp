#include "utility/isa_features.hpp"

// __check_isa_support is an MSVC compiler intrinsic. clang-cl defines
// _MSC_VER and ships isa_availability.h, but does not implement the intrinsic,
// so keep clang on the scalar path instead of failing to compile.
#if defined(_MSC_VER) && !defined(__clang__) && (defined(_M_X64) || defined(_M_IX86)) && __has_include(<isa_availability.h>)
#include <immintrin.h>
#include <isa_availability.h>
#define COREHOST_HAS_ISA_QUERY 1
#else
#define COREHOST_HAS_ISA_QUERY 0
#endif

namespace corehost::isa
{

level detect_level() noexcept
{
#if COREHOST_HAS_ISA_QUERY
    if (__check_isa_support(__IA_SUPPORT_AVX10, 1) && __check_isa_support(__IA_SUPPORT_VECTOR512, 1))
    {
        return level::avx10_1;
    }
    if (__check_isa_support(__IA_SUPPORT_VECTOR256, 0))
    {
        return level::avx2;
    }
    if (__check_isa_support(__IA_SUPPORT_SSE42, 0))
    {
        return level::sse42;
    }
#endif
    return level::scalar;
}

} // namespace corehost::isa
