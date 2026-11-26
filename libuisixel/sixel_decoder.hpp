#pragma once

#include "sixel.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace corehost::uisixel
{
    struct bitmap final
    {
        std::uint32_t width{};
        std::uint32_t height{};
        std::vector<std::uint8_t> rgba;
    };

    namespace detail
    {
        struct rgb final
        {
            std::uint8_t r{};
            std::uint8_t g{};
            std::uint8_t b{};
        };

        inline std::array<rgb, 256> default_palette() noexcept
        {
            std::array<rgb, 256> palette{};
            constexpr std::array<rgb, 16> base{{
                {0x00, 0x00, 0x00}, {0x80, 0x00, 0x00}, {0x00, 0x80, 0x00}, {0x80, 0x80, 0x00},
                {0x00, 0x00, 0x80}, {0x80, 0x00, 0x80}, {0x00, 0x80, 0x80}, {0xC0, 0xC0, 0xC0},
                {0x80, 0x80, 0x80}, {0xFF, 0x00, 0x00}, {0x00, 0xFF, 0x00}, {0xFF, 0xFF, 0x00},
                {0x00, 0x00, 0xFF}, {0xFF, 0x00, 0xFF}, {0x00, 0xFF, 0xFF}, {0xFF, 0xFF, 0xFF},
            }};
            std::copy(base.begin(), base.end(), palette.begin());
            for (std::size_t index = 16; index < 232; ++index)
            {
                constexpr std::uint8_t levels[6]{0, 95, 135, 175, 215, 255};
                const auto cube = index - 16;
                palette[index] = rgb{levels[(cube / 36) % 6], levels[(cube / 6) % 6], levels[cube % 6]};
            }
            for (std::size_t index = 232; index < 256; ++index)
            {
                const auto level = static_cast<std::uint8_t>(8 + 10 * (index - 232));
                palette[index] = rgb{level, level, level};
            }
            return palette;
        }

        inline std::uint32_t number(std::u16string_view data, std::size_t &position) noexcept
        {
            std::uint32_t value = 0;
            while (position < data.size() && data[position] >= u'0' && data[position] <= u'9')
            {
                value = value * 10 + static_cast<std::uint32_t>(data[position] - u'0');
                ++position;
            }
            return value;
        }

        inline void parse_raster(std::u16string_view data, std::size_t &position, std::uint32_t &width,
                                 std::uint32_t &height) noexcept
        {
            std::array<std::uint32_t, 4> values{};
            for (std::size_t i = 0; i < values.size() && position < data.size(); ++i)
            {
                values[i] = number(data, position);
                if (position >= data.size() || data[position] != u';')
                {
                    break;
                }
                ++position;
            }
            width = std::max(width, values[2]);
            height = std::max(height, values[3]);
        }
    }

    // Decodes one Sixel DCS payload into premultiplied-free RGBA8 pixels.
    [[nodiscard]] inline std::optional<bitmap> decode(std::u16string_view data) noexcept
    {
        if (data.empty())
        {
            return std::nullopt;
        }

        std::uint32_t declared_width = 0;
        std::uint32_t declared_height = 0;
        auto palette = detail::default_palette();
        std::size_t current_color = 0;
        std::size_t x = 0;
        std::size_t y = 0;
        std::size_t max_x = 0;
        std::size_t max_y = 0;
        std::vector<std::uint8_t> pixels;

        auto ensure_size = [&]() {
            const auto width = std::max<std::size_t>(declared_width, max_x);
            const auto height = std::max<std::size_t>(declared_height, max_y);
            if (width == 0 || height == 0)
            {
                return;
            }
            if (pixels.size() != width * height * 4)
            {
                pixels.resize(width * height * 4, 0);
            }
        };

        auto set_pixel = [&](std::size_t px, std::size_t py, detail::rgb color) {
            if (px >= 4096 || py >= 4096)
            {
                return;
            }
            max_x = std::max(max_x, px + 1);
            max_y = std::max(max_y, py + 1);
            const auto width = std::max<std::size_t>(declared_width, max_x);
            const auto height = std::max<std::size_t>(declared_height, max_y);
            if (width == 0 || height == 0 || py >= height || px >= width)
            {
                return;
            }
            pixels.resize(width * height * 4);
            auto *target = pixels.data() + (py * width + px) * 4;
            target[0] = color.r;
            target[1] = color.g;
            target[2] = color.b;
            target[3] = 0xFF;
        };

        std::size_t position = 0;
        while (position < data.size())
        {
            const auto ch = data[position++];
            if (ch == u'#')
            {
                std::array<std::uint32_t, 5> values{};
                std::size_t count = 0;
                while (position < data.size() && count < values.size())
                {
                    values[count++] = detail::number(data, position);
                    if (position >= data.size() || data[position] != u';')
                    {
                        break;
                    }
                    ++position;
                }
                if (count == 0)
                {
                    continue;
                }
                current_color = std::min<std::size_t>(values[0], palette.size() - 1);
                if (count >= 5 && values[1] == 2)
                {
                    palette[current_color] = detail::rgb{
                        static_cast<std::uint8_t>(std::min<std::uint32_t>(values[2], 100) * 255 / 100),
                        static_cast<std::uint8_t>(std::min<std::uint32_t>(values[3], 100) * 255 / 100),
                        static_cast<std::uint8_t>(std::min<std::uint32_t>(values[4], 100) * 255 / 100)};
                }
                continue;
            }
            if (ch == u'"')
            {
                detail::parse_raster(data, position, declared_width, declared_height);
                ensure_size();
                continue;
            }
            if (ch == u'$')
            {
                x = 0;
                continue;
            }
            if (ch == u'-')
            {
                x = 0;
                y += 6;
                continue;
            }
            if (ch == u'!')
            {
                const auto count = std::max<std::uint32_t>(1, detail::number(data, position));
                if (position >= data.size())
                {
                    break;
                }
                const auto value = data[position++];
                if (value >= 0x3F && value <= 0x7E)
                {
                    const auto bits = static_cast<std::uint32_t>(value - 0x3F);
                    for (std::uint32_t repeat = 0; repeat < count; ++repeat)
                    {
                        for (std::uint32_t bit = 0; bit < 6; ++bit)
                        {
                            if ((bits & (1u << bit)) != 0)
                            {
                                set_pixel(x, y + bit, palette[current_color]);
                            }
                        }
                        ++x;
                    }
                }
                continue;
            }
            if (ch >= 0x3F && ch <= 0x7E)
            {
                const auto bits = static_cast<std::uint32_t>(ch - 0x3F);
                for (std::uint32_t bit = 0; bit < 6; ++bit)
                {
                    if ((bits & (1u << bit)) != 0)
                    {
                        set_pixel(x, y + bit, palette[current_color]);
                    }
                }
                ++x;
            }
        }

        ensure_size();
        if (pixels.empty())
        {
            return std::nullopt;
        }

        bitmap result;
        result.width = static_cast<std::uint32_t>(std::max<std::size_t>(declared_width, max_x));
        result.height = static_cast<std::uint32_t>(std::max<std::size_t>(declared_height, max_y));
        result.rgba = std::move(pixels);
        return result;
    }
}
