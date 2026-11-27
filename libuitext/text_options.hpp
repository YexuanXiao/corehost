#pragma once

#include <array>
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

enum class font_anti_alias : unsigned char
{
    grayscale,
    cleartype,
};

struct font_axis final
{
    std::uint32_t tag{};
    float value{};
};

inline constexpr std::uint32_t font_axis_weight = 0x77676874; // wght
inline constexpr std::uint32_t font_axis_width = 0x77647468;  // wdth
inline constexpr std::uint32_t font_axis_italic = 0x6974616C; // ital
inline constexpr std::uint32_t font_axis_slant = 0x736C6E74;  // slnt
inline constexpr std::uint32_t font_axis_optical_size = 0x6F70737A; // opsz

inline constexpr std::array<font_axis, 5> default_font_variation()
{
    return {font_axis{font_axis_weight, 400.0f}, font_axis{font_axis_width, 100.0f},
            font_axis{font_axis_italic, 0.0f}, font_axis{font_axis_slant, 0.0f},
            font_axis{font_axis_optical_size, 16.0f}};
}

inline constexpr std::array<font_axis, 5> default_font_variation_bold()
{
    auto value = default_font_variation();
    value[0].value = 700.0f;
    return value;
}

inline constexpr std::array<font_axis, 5> default_font_variation_italic()
{
    auto value = default_font_variation();
    value[2].value = 1.0f;
    return value;
}

inline constexpr std::array<font_axis, 5> default_font_variation_bold_italic()
{
    auto value = default_font_variation_bold();
    value[2].value = 1.0f;
    return value;
}

struct text_options final
{
    font_family font{default_font_family};
    bool contrast{default_contrast};
    bool alpha_correction{default_alpha_correction};
};

} // namespace corehost::uitext
