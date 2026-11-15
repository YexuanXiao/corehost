#pragma once

#include <libunicode/convert.h>
#include <libunicode/codepoint_properties.h>
#include <libunicode/grapheme_segmenter.h>
#include <libunicode/width.h>

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>

namespace corehost::screen
{

// True when every UTF-16 unit is printable ASCII. One word covers four units;
// the caller can skip grapheme segmentation for ordinary text.
[[nodiscard]] inline bool all_printable_ascii(std::u16string_view text) noexcept
{
    constexpr std::uint64_t high = 0x8000800080008000ull;
    constexpr std::uint64_t low = 0x0020002000200020ull;
    constexpr std::uint64_t del = 0x007F007F007F007Full;
    constexpr std::uint64_t one = 0x0001000100010001ull;
    constexpr std::uint64_t non_ascii = 0xFF80FF80FF80FF80ull;

    std::size_t offset = 0;
    while (offset + 4 <= text.size())
    {
        std::uint64_t word = 0;
        std::memcpy(&word, text.data() + offset, sizeof(word));

        const auto below_low = ~(((word | high) - low)) & high;
        const auto is_del = word ^ del;
        const auto at_del = ~((is_del | high) - one) & ~is_del & high;
        const auto invalid = below_low | at_del | (word & non_ascii);
        if (invalid != 0)
        {
            return false;
        }
        offset += 4;
    }

    while (offset < text.size())
    {
        const auto ch = text[offset];
        if (ch < 0x20 || ch >= 0x7F)
        {
            return false;
        }
        ++offset;
    }
    return true;
}

// Matches corehost::conpty::text_measurement_mode.
enum class text_measurement
{
    console = 0,
    graphemes,
    wcswidth,
};

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
    if ((value >= 0x0300 && value <= 0x036F) || (value >= 0x1AB0 && value <= 0x1AFF) ||
        (value >= 0x1DC0 && value <= 0x1DFF) || (value >= 0x20D0 && value <= 0x20FF) ||
        (value >= 0xFE20 && value <= 0xFE2F) || (value >= 0xFE00 && value <= 0xFE0F) ||
        (value >= 0xE0100 && value <= 0xE01EF))
    {
        return 0;
    }

    if ((value >= 0x4E00 && value <= 0x9FFF) || (value >= 0x3400 && value <= 0x4DBF) ||
        (value >= 0x20000 && value <= 0x3134F) || (value >= 0xF900 && value <= 0xFAFF) ||
        (value >= 0x2F800 && value <= 0x2FA1F) || (value >= 0xFF01 && value <= 0xFF60) ||
        (value >= 0xFFE0 && value <= 0xFFE6) || (value >= 0x3040 && value <= 0x309F) ||
        (value >= 0x30A0 && value <= 0x30FF) || (value >= 0x31F0 && value <= 0x31FF) ||
        (value >= 0xAC00 && value <= 0xD7AF) || (value >= 0x3130 && value <= 0x318F) ||
        (value >= 0x3000 && value <= 0x303F) || (value >= 0x3200 && value <= 0x33FF) ||
        (value >= 0x2E80 && value <= 0x2FDF) || (value >= 0x31C0 && value <= 0x31EF) ||
        (value >= 0x1F000 && value <= 0x1FFFF) ||
        (value >= 0x2600 && value <= 0x27BF && value != 0x263A && value != 0x263B))
    {
        return 2;
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

template <text_measurement Mode, bool AmbiguousIsWide = false>
[[nodiscard]] constexpr int codepoint_width(char32_t value) noexcept
{
    if constexpr (Mode == text_measurement::console)
    {
        return console_width(value);
    }
    else
    {
        return unicode_width(value, AmbiguousIsWide);
    }
}

template <text_measurement Mode, bool AmbiguousIsWide = false>
[[nodiscard]] constexpr int char_width_for_mode(char32_t value) noexcept
{
    return codepoint_width<Mode, AmbiguousIsWide>(value);
}

struct codepoint_result final
{
    char32_t value{};
    std::size_t units{};
};

template <std::input_iterator Iterator, std::sentinel_for<Iterator> Sentinel>
[[nodiscard]] codepoint_result next_codepoint(Iterator &iterator, Sentinel end) noexcept
{
    if (iterator == end)
    {
        return {};
    }

    const auto first = static_cast<std::uint32_t>(*iterator++);
    std::size_t units = 1;
    char16_t buffer[2]{static_cast<char16_t>(first), 0};
    if (first >= 0xD800 && first <= 0xDBFF)
    {
        if (iterator == end)
        {
            return codepoint_result{U'\uFFFD', 1};
        }
        buffer[1] = *iterator++;
        units = 2;
    }
    else if (first >= 0xDC00 && first <= 0xDFFF)
    {
        return codepoint_result{U'\uFFFD', 1};
    }

    auto source = std::begin(buffer);
    const auto decoded = unicode::decoder<char16_t>{}(source);
    if (!decoded.has_value())
    {
        return codepoint_result{U'\uFFFD', units};
    }
    return codepoint_result{decoded.value(), units};
}

[[nodiscard]] inline codepoint_result next_codepoint(std::u16string_view text, std::size_t offset) noexcept
{
    if (offset >= text.size())
    {
        return {};
    }
    auto iterator = text.begin() + static_cast<std::ptrdiff_t>(offset);
    const auto start = iterator;
    const auto decoded = next_codepoint(iterator, text.end());
    return codepoint_result{decoded.value, static_cast<std::size_t>(iterator - start)};
}

template <std::output_iterator<char16_t> Output>
[[nodiscard]] Output to_u16(char32_t codepoint, Output output)
{
    return unicode::encoder<char16_t>{}(codepoint, output);
}

template <std::ranges::input_range Range, std::output_iterator<char32_t> Output>
    requires std::same_as<std::remove_cvref_t<std::ranges::range_value_t<Range>>, char16_t>
[[nodiscard]] Output to_u32(Range &&text, Output output)
{
    auto iterator = std::ranges::begin(text);
    const auto end = std::ranges::end(text);
    while (iterator != end)
    {
        const auto decoded = next_codepoint(iterator, end);
        *output++ = decoded.value;
    }
    return output;
}

template <std::output_iterator<char32_t> Output>
[[nodiscard]] Output to_u32(std::u32string_view text, Output output)
{
    return std::copy(text.begin(), text.end(), output);
}

struct grapheme_cluster final
{
    std::span<const char16_t> text;
    int width{};
};

template <text_measurement Mode, bool AmbiguousIsWide = false>
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

    explicit grapheme_iterator(std::u16string_view text) noexcept : text_{text}
    {
    }

    [[nodiscard]] value_type operator*() const noexcept
    {
        const auto result = scan(offset_);
        return value_type{std::span<const char16_t>{text_.data() + offset_, result.end - offset_}, result.width};
    }

    grapheme_iterator &operator++() noexcept
    {
        offset_ = scan(offset_).end;
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
        return iterator.offset_ >= iterator.text_.size();
    }

  private:
    struct scan_result final
    {
        std::size_t end{};
        int width{};
    };

    [[nodiscard]] scan_result scan(std::size_t start) const noexcept
    {
        if (start >= text_.size())
        {
            return {};
        }

        auto cursor = start;
        const auto first = next_codepoint(text_, cursor);
        cursor += first.units;

        unicode::grapheme_segmenter_state state{};
        unicode::grapheme_process_init(first.value, state);
        auto width = first.value == 0xFE0F ? 2 : codepoint_width<Mode, AmbiguousIsWide>(first.value);

        while (cursor < text_.size())
        {
            const auto next = next_codepoint(text_, cursor);
            if (unicode::grapheme_process_breakable(next.value, state))
            {
                break;
            }
            width += next.value == 0xFE0F ? 2 : codepoint_width<Mode, AmbiguousIsWide>(next.value);
            cursor += next.units;
        }

        return scan_result{cursor, width > 2 ? 2 : width};
    }

    std::u16string_view text_{};
    std::size_t offset_{};
};

template <text_measurement Mode, bool AmbiguousIsWide = false>
class grapheme_range final
{
  public:
    explicit grapheme_range(std::u16string_view text) noexcept : text_{text}
    {
    }

    [[nodiscard]] grapheme_iterator<Mode, AmbiguousIsWide> begin() const noexcept
    {
        return grapheme_iterator<Mode, AmbiguousIsWide>{text_};
    }

    [[nodiscard]] std::default_sentinel_t end() const noexcept
    {
        return {};
    }

  private:
    std::u16string_view text_;
};

template <text_measurement Mode, bool AmbiguousIsWide = false>
[[nodiscard]] inline int glyph_width(std::u16string_view glyph) noexcept
{
    if (glyph.empty())
    {
        return 0;
    }

    if constexpr (Mode == text_measurement::graphemes)
    {
        int total = 0;
        for (const auto cluster : grapheme_range<Mode, AmbiguousIsWide>{glyph})
        {
            total += cluster.width;
        }
        return total;
    }
    else
    {
        std::size_t offset = 0;
        int width = 0;
        while (offset < glyph.size())
        {
            const auto decoded = next_codepoint(glyph, offset);
            width += codepoint_width<Mode, AmbiguousIsWide>(decoded.value);
            offset += decoded.units;
        }
        return width;
    }
}

template <text_measurement Mode, bool AmbiguousIsWide = false, std::ranges::input_range Range>
    requires std::same_as<std::remove_cvref_t<std::ranges::range_value_t<Range>>, char16_t>
[[nodiscard]] inline int text_width(Range &&text) noexcept
{
    auto iterator = std::ranges::begin(text);
    const auto end = std::ranges::end(text);

    if constexpr (Mode == text_measurement::graphemes)
    {
        int total = 0;
        int cluster = 0;
        bool first = true;
        unicode::grapheme_segmenter_state state{};

        while (iterator != end)
        {
            const auto decoded = next_codepoint(iterator, end);
            if (first)
            {
                unicode::grapheme_process_init(decoded.value, state);
                cluster = decoded.value == 0xFE0F ? 2 : unicode_width(decoded.value, AmbiguousIsWide);
                first = false;
            }
            else if (unicode::grapheme_process_breakable(decoded.value, state))
            {
                total += cluster > 2 ? 2 : cluster;
                unicode::grapheme_process_init(decoded.value, state);
                cluster = decoded.value == 0xFE0F ? 2 : unicode_width(decoded.value, AmbiguousIsWide);
            }
            else
            {
                cluster += decoded.value == 0xFE0F ? 2 : unicode_width(decoded.value, AmbiguousIsWide);
            }
        }

        if (!first)
        {
            total += cluster > 2 ? 2 : cluster;
        }
        return total;
    }
    else if constexpr (Mode == text_measurement::console)
    {
        int total = 0;
        while (iterator != end)
        {
            const auto decoded = next_codepoint(iterator, end);
            total += console_width(decoded.value);
        }
        return total;
    }
    else if constexpr (Mode == text_measurement::wcswidth)
    {
        int total = 0;
        while (iterator != end)
        {
            const auto decoded = next_codepoint(iterator, end);
            total += unicode_width(decoded.value, AmbiguousIsWide);
        }
        return total;
    }
    else
    {
        static_assert(Mode == text_measurement::console || Mode == text_measurement::graphemes ||
                          Mode == text_measurement::wcswidth,
                      "unsupported text measurement mode");
        return 0;
    }
}

template <text_measurement Mode, bool AmbiguousIsWide = false>
[[nodiscard]] inline int text_width(std::u16string_view text) noexcept
{
    return text_width<Mode, AmbiguousIsWide>(std::views::all(text));
}

} // namespace corehost::screen
