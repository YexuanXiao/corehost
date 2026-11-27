#pragma once

namespace corehost::screen
{
enum class text_measurement
{
    console = 0,
    graphemes,
    wcswidth,
};

inline constexpr text_measurement default_text_measurement = text_measurement::graphemes;
} // namespace corehost::screen