#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>

namespace corehost::scrollbar
{

struct geometry final
{
    float x{};
    float y{};
    float width{};
    float height{};
    float thumb_y{};
    float thumb_height{};
    bool visible{};
};

enum class hit_target : std::uint8_t
{
    none,
    above_thumb,
    thumb,
    below_thumb,
};

struct state final
{
    std::size_t total_rows{};
    std::size_t visible_rows{};
    std::size_t top_row{};
};

inline constexpr float default_width = 8.0f;
inline constexpr float minimum_thumb_height = 24.0f;

[[nodiscard]] inline std::size_t max_top(const state &value) noexcept
{
    return value.total_rows > value.visible_rows ? value.total_rows - value.visible_rows : 0;
}

[[nodiscard]] inline geometry compute(float x, float y, float width, float height, const state &value) noexcept
{
    geometry result{.x = x, .y = y, .width = width, .height = height};
    if (value.visible_rows == 0 || value.total_rows <= value.visible_rows || height <= 0.0f)
    {
        return result;
    }

    result.visible = true;
    const auto track = std::max(0.0f, height - 2.0f);
    const auto thumb = std::clamp(track * static_cast<float>(value.visible_rows) /
                                      static_cast<float>(value.total_rows),
                                  std::min(minimum_thumb_height, track), track);
    const auto top = std::min(value.top_row, max_top(value));
    const auto fraction = max_top(value) == 0
                              ? 0.0f
                              : static_cast<float>(top) / static_cast<float>(max_top(value));
    result.thumb_height = thumb;
    result.thumb_y = y + 1.0f + (track - thumb) * fraction;
    return result;
}

[[nodiscard]] inline hit_target hit_test(const geometry &value, float x, float y) noexcept
{
    if (!value.visible || x < value.x || x >= value.x + value.width || y < value.y || y >= value.y + value.height)
    {
        return hit_target::none;
    }
    if (y < value.thumb_y)
    {
        return hit_target::above_thumb;
    }
    if (y >= value.thumb_y + value.thumb_height)
    {
        return hit_target::below_thumb;
    }
    return hit_target::thumb;
}

[[nodiscard]] inline std::size_t top_from_pointer(const geometry &value, float pointer_y, float grab_offset,
                                                   const state &scroll) noexcept
{
    const auto track = std::max(0.0f, value.height - 2.0f);
    const auto travel = std::max(0.0f, track - value.thumb_height);
    if (travel <= 0.0f || max_top(scroll) == 0)
    {
        return 0;
    }

    const auto position = std::clamp(pointer_y - value.y - 1.0f - grab_offset, 0.0f, travel);
    return static_cast<std::size_t>(
        static_cast<float>(max_top(scroll)) * (position / travel) + 0.5f);
}

[[nodiscard]] inline float grab_offset(const geometry &value, float pointer_y) noexcept
{
    return std::clamp(pointer_y - value.thumb_y, 0.0f, value.thumb_height);
}

} // namespace corehost::scrollbar
