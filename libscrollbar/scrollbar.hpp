#pragma once

#include <algorithm>
#include <cmath>
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
    float track_y{};
    float track_height{};
    float thumb_y{};
    float thumb_height{};
    bool visible{};
};

enum class hit_target : std::uint8_t
{
    none,
    arrow_up,
    above_thumb,
    thumb,
    below_thumb,
    arrow_down,
};

struct state final
{
    std::size_t total_rows{};
    std::size_t visible_rows{};
    double top_row{};
};

// Geometry constants.
inline constexpr float default_width = 16.f;
inline constexpr float arrow_height = 24.f;
inline constexpr float arrow_visual_margin = 8.f;
inline constexpr float arrow_visual_width = 14.f;
inline constexpr float arrow_visual_height = 14.f;
inline constexpr float track_inset = 24.f;
inline constexpr float minimum_thumb_height = 24.f;

// Fixed mask constants. The masks are deliberately fixed at 32x32 with no
// supersampling: generation happens once, atlas usage is bounded, and the
// slight blur from scaling arrows to 14x14 DIP is accepted.
inline constexpr int mask_size = 32;
inline constexpr int mask_samples = 1;
inline constexpr float arrow_radius_ratio = 0.2f;
inline constexpr float arrow_top = 0.06f;
inline constexpr float arrow_side = 0.94f;

// Drawing constants.
inline constexpr float thin_thumb_width = 2.f;
inline constexpr float expanded_thumb_width = 8.f;
inline constexpr float thumb_right_margin = 4.f;
inline constexpr float arrow_center_from_right = 8.f;
inline constexpr float thumb_cap_ratio = 0.5f;
inline constexpr std::uint32_t thumb_mask_key = 0x53424Du;
inline constexpr std::uint32_t arrow_up_mask_key = 0x534255u;
inline constexpr std::uint32_t arrow_down_mask_key = 0x534144u;
inline constexpr float track_alpha = 0.055f;
inline constexpr float thumb_alpha = 0.54f;
inline constexpr int expand_frames = 8;

[[nodiscard]] inline std::size_t max_top(const state &value) noexcept
{
    return value.total_rows > value.visible_rows ? value.total_rows - value.visible_rows : 0;
}

[[nodiscard]] inline geometry compute(float x, float y, float width, float height, const state &value,
                                      float arrow_size = arrow_height) noexcept
{
    geometry result{.x = x, .y = y, .width = width, .height = height};
    const auto track_y = y + arrow_size;
    const auto track_height = height - 2.0f * arrow_size;
    result.track_y = track_y;
    result.track_height = track_height;
    if (value.visible_rows == 0 || value.total_rows <= value.visible_rows || track_height <= 0.0f)
    {
        return result;
    }

    result.visible = true;
    const auto thumb = std::clamp(track_height * static_cast<float>(value.visible_rows) /
                                      static_cast<float>(value.total_rows),
                                  std::min(minimum_thumb_height, track_height), track_height);
    const auto top = std::min(value.top_row, static_cast<double>(max_top(value)));
    const auto fraction = max_top(value) == 0
                              ? 0.0f
                              : static_cast<float>(top / static_cast<double>(max_top(value)));
    result.thumb_height = thumb;
    result.thumb_y = track_y + (track_height - thumb) * fraction;
    return result;
}

[[nodiscard]] inline hit_target hit_test(const geometry &value, float x, float y) noexcept
{
    if (!value.visible || x < value.x || x >= value.x + value.width || y < value.y || y >= value.y + value.height)
    {
        return hit_target::none;
    }
    if (y < value.track_y)
    {
        return hit_target::arrow_up;
    }
    if (y >= value.track_y + value.track_height)
    {
        return hit_target::arrow_down;
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

[[nodiscard]] inline double top_from_pointer(const geometry &value, float pointer_y, float grab_offset,
                                                const state &scroll) noexcept
{
    const auto travel = std::max(0.0f, value.track_height - value.thumb_height);
    if (travel <= 0.0f || max_top(scroll) == 0)
    {
        return 0.0;
    }

    const auto position = std::clamp(pointer_y - value.track_y - grab_offset, 0.0f, travel);
    return static_cast<double>(max_top(scroll)) * (position / travel);
}

[[nodiscard]] inline float grab_offset(const geometry &value, float pointer_y) noexcept
{
    return std::clamp(pointer_y - value.thumb_y, 0.0f, value.thumb_height);
}

} // namespace corehost::scrollbar
