#include "simd.hpp"

namespace corehost::vtparser::simd
{

std::size_t ground_text_scalar(const char16_t *text, std::size_t size, bool &ascii) noexcept
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

} // namespace corehost::vtparser::simd
