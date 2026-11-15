#pragma once

#include "win32_types.hpp"

#include <algorithm>
#include <cstdint>

namespace corehost::screen
{

struct coord final
{
    std::int32_t x{};
    std::int32_t y{};

    friend constexpr bool operator==(const coord &, const coord &) noexcept = default;
};

// Inclusive buffer coordinates, matching Win32 SMALL_RECT semantics.
struct rect final
{
    std::int32_t left{};
    std::int32_t top{};
    std::int32_t right{};
    std::int32_t bottom{};

    friend constexpr bool operator==(const rect &, const rect &) noexcept = default;
};

[[nodiscard]] inline coord from_win32(interop::win32::coord value) noexcept
{
    return coord{value.x, value.y};
}

[[nodiscard]] inline interop::win32::coord to_win32(coord value) noexcept
{
    return interop::win32::coord{static_cast<std::int16_t>(value.x), static_cast<std::int16_t>(value.y)};
}

[[nodiscard]] inline rect from_win32(interop::win32::small_rect value) noexcept
{
    return rect{value.left, value.top, value.right, value.bottom};
}

[[nodiscard]] inline interop::win32::small_rect to_win32(rect value) noexcept
{
    return interop::win32::small_rect{
        static_cast<std::int16_t>(value.left),
        static_cast<std::int16_t>(value.top),
        static_cast<std::int16_t>(value.right),
        static_cast<std::int16_t>(value.bottom),
    };
}

class viewport final
{
  public:
    viewport() noexcept
    {
        reset_to_buffer(coord{1, 1});
    }

    explicit viewport(coord buffer_size) noexcept
    {
        reset_to_buffer(buffer_size);
    }

    [[nodiscard]] rect bounds() const noexcept
    {
        return bounds_;
    }

    [[nodiscard]] coord origin() const noexcept
    {
        return coord{bounds_.left, bounds_.top};
    }

    [[nodiscard]] coord size() const noexcept
    {
        return coord{bounds_.right - bounds_.left + 1, bounds_.bottom - bounds_.top + 1};
    }

    [[nodiscard]] bool contains(coord value) const noexcept
    {
        return value.x >= bounds_.left && value.x <= bounds_.right &&
               value.y >= bounds_.top && value.y <= bounds_.bottom;
    }

    [[nodiscard]] coord relative(coord buffer_position) const noexcept
    {
        return coord{buffer_position.x - bounds_.left, buffer_position.y - bounds_.top};
    }

    [[nodiscard]] coord absolute(coord relative_position) const noexcept
    {
        return coord{bounds_.left + relative_position.x, bounds_.top + relative_position.y};
    }

    [[nodiscard]] rect relative_rect(rect buffer_rect) const noexcept
    {
        return rect{buffer_rect.left - bounds_.left,
                    buffer_rect.top - bounds_.top,
                    buffer_rect.right - bounds_.left,
                    buffer_rect.bottom - bounds_.top};
    }

    [[nodiscard]] bool set_rect(rect requested, coord buffer_size) noexcept
    {
        const auto old = bounds_;
        const auto buffer_width = dimension(buffer_size.x);
        const auto buffer_height = dimension(buffer_size.y);
        const auto width = std::clamp<std::int32_t>(requested.right - requested.left + 1, 1, buffer_width);
        const auto height = std::clamp<std::int32_t>(requested.bottom - requested.top + 1, 1, buffer_height);
        const auto left = std::clamp<std::int32_t>(requested.left, 0, buffer_width - width);
        const auto top = std::clamp<std::int32_t>(requested.top, 0, buffer_height - height);
        bounds_ = rect{left, top, left + width - 1, top + height - 1};
        return old != bounds_;
    }

    [[nodiscard]] bool set_origin(coord requested, coord buffer_size) noexcept
    {
        const auto current = size();
        return set_rect(rect{requested.x,
                             requested.y,
                             requested.x + current.x - 1,
                             requested.y + current.y - 1},
                        buffer_size);
    }

    [[nodiscard]] bool set_size(coord requested, coord buffer_size) noexcept
    {
        return set_rect(rect{bounds_.left,
                             bounds_.top,
                             bounds_.left + std::max<std::int32_t>(requested.x, 1) - 1,
                             bounds_.top + std::max<std::int32_t>(requested.y, 1) - 1},
                        buffer_size);
    }

    [[nodiscard]] bool clamp_to_buffer(coord buffer_size) noexcept
    {
        return set_rect(bounds_, buffer_size);
    }

    [[nodiscard]] bool snap_to_cursor(coord cursor, coord buffer_size) noexcept
    {
        auto next = origin();
        if (cursor.x < bounds_.left)
        {
            next.x = cursor.x;
        }
        else if (cursor.x > bounds_.right)
        {
            next.x += cursor.x - bounds_.right;
        }

        if (cursor.y < bounds_.top)
        {
            next.y = cursor.y;
        }
        else if (cursor.y > bounds_.bottom)
        {
            next.y += cursor.y - bounds_.bottom;
        }

        return set_origin(next, buffer_size);
    }

    void reset_to_buffer(coord buffer_size) noexcept
    {
        const auto width = dimension(buffer_size.x);
        const auto height = dimension(buffer_size.y);
        bounds_ = rect{0, 0, width - 1, height - 1};
    }

  private:
    static constexpr std::int32_t dimension(std::int32_t value) noexcept
    {
        return std::max<std::int32_t>(value, 1);
    }

    rect bounds_{0, 0, 0, 0};
};

} // namespace corehost::screen
