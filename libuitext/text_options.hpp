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
inline constexpr font_family default_font_family = font_family::cascadia_mono;
inline constexpr bool default_contrast = true;
inline constexpr bool default_alpha_correction = true;

struct text_options final
{
    font_family font{default_font_family};
    bool contrast{default_contrast};
    bool alpha_correction{default_alpha_correction};
};

} // namespace corehost::uitext
