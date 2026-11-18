#include "simd.hpp"

namespace corehost::vtparser::simd
{

std::size_t decode_ascii_scalar(const char8_t *input, std::size_t size, char16_t *output) noexcept
{
    std::size_t count = 0;
    while (count < size && input[count] < 0x80)
    {
        output[count] = static_cast<char16_t>(input[count]);
        ++count;
    }
    return count;
}

} // namespace corehost::vtparser::simd
