#include "legacy_sgr.hpp"

#include <cstring>

namespace corehost::conptyio::simd
{
namespace
{
constexpr char8_t legacy_sgr[] = u8"\x1b[0;37;40m";
constexpr std::size_t legacy_sgr_size = sizeof(legacy_sgr) - 1;
} // namespace

const char8_t *find_legacy_sgr_scalar(const char8_t *data, std::size_t size) noexcept
{
    for (std::size_t i = 0; i + legacy_sgr_size <= size; ++i)
    {
        if (std::memcmp(data + i, legacy_sgr, legacy_sgr_size) == 0)
            return data + i;
    }
    return nullptr;
}

} // namespace corehost::conptyio::simd
