#pragma once

#include <libunicode/convert.h>
#include <libunicode/codepoint_properties.h>
#include <libunicode/grapheme_segmenter.h>
#include <libunicode/width.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <span>
#include <string>
#include <string_view>

namespace corehost::screen
{

// Matches corehost::conpty::text_measurement_mode.
enum class text_measurement
{
    console = 0,
    graphemes,
    wcswidth,
};

using text_measurement_mode = text_measurement;

struct width_options final
{
    text_measurement mode{text_measurement::console};
    bool ambiguous_is_wide{false};
};

[[nodiscard]] constexpr bool is_combining(char32_t value) noexcept
{
    return (value >= 0x0300 && value <= 0x036F) ||
           (value >= 0x1AB0 && value <= 0x1AFF) ||
           (value >= 0x1DC0 && value <= 0x1DFF) ||
           (value >= 0x20D0 && value <= 0x20FF) ||
           (value >= 0xFE00 && value <= 0xFE0F) ||
           (value >= 0xFE20 && value <= 0xFE2F) ||
           (value >= 0xE0100 && value <= 0xE01EF);
}

[[nodiscard]] constexpr int console_width(char32_t value) noexcept
{
    if (value < 0x20 || value == 0x7F || value == 0x00AD)
    {
        return 0;
    }
    if (value == 0x200B || value == 0x200C || value == 0x200D || value == 0xFEFF)
    {
        return 0;
    }
    if ((value >= 0x0300 && value <= 0x036F) ||
        (value >= 0x1AB0 && value <= 0x1AFF) ||
        (value >= 0x1DC0 && value <= 0x1DFF) ||
        (value >= 0x20D0 && value <= 0x20FF) ||
        (value >= 0xFE20 && value <= 0xFE2F) ||
        (value >= 0xFE00 && value <= 0xFE0F) ||
        (value >= 0xE0100 && value <= 0xE01EF))
    {
        return 0;
    }

    if ((value >= 0x4E00 && value <= 0x9FFF) ||
        (value >= 0x3400 && value <= 0x4DBF) ||
        (value >= 0x20000 && value <= 0x3134F) ||
        (value >= 0xF900 && value <= 0xFAFF) ||
        (value >= 0x2F800 && value <= 0x2FA1F) ||
        (value >= 0xFF01 && value <= 0xFF60) ||
        (value >= 0xFFE0 && value <= 0xFFE6) ||
        (value >= 0x3040 && value <= 0x309F) ||
        (value >= 0x30A0 && value <= 0x30FF) ||
        (value >= 0x31F0 && value <= 0x31FF) ||
        (value >= 0xAC00 && value <= 0xD7AF) ||
        (value >= 0x3130 && value <= 0x318F) ||
        (value >= 0x3000 && value <= 0x303F) ||
        (value >= 0x3200 && value <= 0x33FF) ||
        (value >= 0x2E80 && value <= 0x2FDF) ||
        (value >= 0x31C0 && value <= 0x31EF) ||
        (value >= 0x1F000 && value <= 0x1FFFF) ||
        (value >= 0x2600 && value <= 0x27BF && value != 0x263A && value != 0x263B))
    {
        return 2;
    }

    if (value >= 0x2500 && value <= 0x257F)
    {
        return 1;
    }
    return 1;
}

[[nodiscard]] inline int unicode_width(char32_t value, bool ambiguous_is_wide) noexcept
{
    if (value < 0x20 || value == 0x7F)
    {
        return 0;
    }
    if (ambiguous_is_wide)
    {
        const auto properties = unicode::codepoint_properties::get(value);
        if (properties.east_asian_width == unicode::East_Asian_Width::Ambiguous)
        {
            return 2;
        }
    }
    return static_cast<int>(unicode::width(value));
}

[[nodiscard]] inline int codepoint_width(char32_t value, width_options options) noexcept
{
    switch (options.mode)
    {
    case text_measurement::console:
        return console_width(value);
    case text_measurement::wcswidth:
        return unicode_width(value, options.ambiguous_is_wide);
    case text_measurement::graphemes:
        return unicode_width(value, options.ambiguous_is_wide);
    }
    return 1;
}

[[nodiscard]] inline int char_width_for_mode(char32_t value,
                                             text_measurement mode,
                                             bool ambiguous_is_wide = false) noexcept
{
    return codepoint_width(value, width_options{mode, ambiguous_is_wide});
}

struct codepoint_result final
{
    char32_t value{};
    std::size_t units{};
};

[[nodiscard]] inline codepoint_result next_codepoint(std::u16string_view text, std::size_t offset) noexcept
{
    if (offset >= text.size())
    {
        return {};
    }

    const auto first = static_cast<std::uint32_t>(text[offset]);
    if (first >= 0xD800 && first <= 0xDBFF && offset + 1 >= text.size())
    {
        return codepoint_result{U'\uFFFD', 1};
    }

    auto iterator = text.begin() + static_cast<std::ptrdiff_t>(offset);
    const auto decoded = unicode::decoder<char16_t>{}(iterator);
    if (!decoded.has_value())
    {
        return codepoint_result{U'\uFFFD', 1};
    }
    return codepoint_result{decoded.value(), static_cast<std::size_t>(iterator - text.begin()) - offset};
}

[[nodiscard]] inline std::u16string to_u16(char32_t codepoint)
{
    return unicode::convert_to<char16_t>(codepoint);
}

[[nodiscard]] inline std::u32string to_u32(std::u16string_view text)
{
    return unicode::convert_to<char32_t>(text);
}

struct grapheme_cluster final
{
    std::span<const char16_t> text;
    int width{};
};

class grapheme_iterator final
{
  public:
    using iterator_concept = std::forward_iterator_tag;
    using iterator_category = std::forward_iterator_tag;
    using value_type = grapheme_cluster;
    using difference_type = std::ptrdiff_t;
    using reference = value_type;
    using pointer = void;

    grapheme_iterator() noexcept = default;

    grapheme_iterator(std::u16string_view text, width_options options) noexcept
        : text_{text},
          options_{options}
    {
        scan();
    }

    [[nodiscard]] value_type operator*() const noexcept
    {
        return value_type{std::span<const char16_t>{text_.data() + begin_, end_ - begin_}, width_};
    }

    grapheme_iterator &operator++() noexcept
    {
        begin_ = end_;
        scan();
        return *this;
    }

    grapheme_iterator operator++(int) noexcept
    {
        auto copy = *this;
        ++*this;
        return copy;
    }

    friend bool operator==(const grapheme_iterator &iterator, std::default_sentinel_t) noexcept
    {
        return iterator.at_end_;
    }

  private:
    void scan() noexcept
    {
        if (begin_ >= text_.size())
        {
            end_ = begin_;
            width_ = 0;
            at_end_ = true;
            return;
        }

        at_end_ = false;
        auto cursor = begin_;
        const auto first = next_codepoint(text_, cursor);
        cursor += first.units;

        unicode::grapheme_segmenter_state state{};
        unicode::grapheme_process_init(first.value, state);
        auto width = first.value == 0xFE0F ? 2 : unicode_width(first.value, options_.ambiguous_is_wide);

        while (cursor < text_.size())
        {
            const auto next = next_codepoint(text_, cursor);
            if (unicode::grapheme_process_breakable(next.value, state))
            {
                break;
            }
            width += next.value == 0xFE0F ? 2 : unicode_width(next.value, options_.ambiguous_is_wide);
            cursor += next.units;
        }

        end_ = cursor;
        width_ = width > 2 ? 2 : width;
    }

    std::u16string_view text_{};
    width_options options_{};
    std::size_t begin_{};
    std::size_t end_{};
    int width_{};
    bool at_end_{true};
};

class grapheme_range final
{
  public:
    grapheme_range(std::u16string_view text, width_options options) noexcept
        : text_{text},
          options_{options}
    {
    }

    [[nodiscard]] grapheme_iterator begin() const noexcept
    {
        return grapheme_iterator{text_, options_};
    }

    [[nodiscard]] std::default_sentinel_t end() const noexcept
    {
        return {};
    }

  private:
    std::u16string_view text_;
    width_options options_;
};
[[nodiscard]] inline int glyph_width(std::u16string_view glyph, width_options options) noexcept
{
    if (glyph.empty())
    {
        return 0;
    }

    if (options.mode == text_measurement::graphemes)
    {
        int total = 0;
        for (const auto cluster : grapheme_range{glyph, options})
        {
            total += cluster.width;
        }
        return total;
    }

    auto result = next_codepoint(glyph, 0);
    auto width = codepoint_width(result.value, options);
    auto offset = result.units;
    while (offset < glyph.size())
    {
        result = next_codepoint(glyph, offset);
        width += codepoint_width(result.value, options);
        offset += result.units;
    }
    return width;
}

[[nodiscard]] inline int text_width_for_mode(std::u16string_view text,
                                             text_measurement mode,
                                             bool ambiguous_is_wide = false) noexcept
{
    const width_options options{mode, ambiguous_is_wide};
    switch (mode)
    {
    case text_measurement::console:
    {
        std::size_t offset = 0;
        int total = 0;
        while (offset < text.size())
        {
            const auto decoded = next_codepoint(text, offset);
            total += console_width(decoded.value);
            offset += decoded.units;
        }
        return total;
    }
    case text_measurement::graphemes:
    {
        int total = 0;
        for (const auto cluster : grapheme_range{text, options})
        {
            total += cluster.width;
        }
        return total;
    }
    case text_measurement::wcswidth:
    {
        std::size_t offset = 0;
        int total = 0;
        while (offset < text.size())
        {
            const auto decoded = next_codepoint(text, offset);
            total += unicode_width(decoded.value, ambiguous_is_wide);
            offset += decoded.units;
        }
        return total;
    }
    }
    return 0;
}

[[nodiscard]] inline int text_width(std::u16string_view text, width_options options) noexcept
{
    return text_width_for_mode(text, options.mode, options.ambiguous_is_wide);
}

} // namespace corehost::screen
