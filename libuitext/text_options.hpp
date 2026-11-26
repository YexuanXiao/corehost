#pragma once

#include <cstdint>

namespace corehost::uitext
{

enum class font_family : std::uint8_t
{
    cascadia_mono,
    cascadia_code,
};

// Runtime text rendering options. These used to be compile-time switches for
// the test matrix; they are now ordinary C++ options so callers can select the
// behavior without rebuilding.
struct text_options final
{
    font_family font{font_family::cascadia_mono};
    bool contrast{true};
    bool alpha_correction{true};
};

} // namespace corehost::uitext
