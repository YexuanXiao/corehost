#pragma once

#include <cstdint>
#include <type_traits>
#include <utility>

namespace corehost::screen
{

// 4-bit cell type. complex/surrogate/grapheme are aliases: all mean
// "primary is U+FFFD and the complete UTF-16 cluster is in the store".
enum class cell_type : std::uint8_t
{
    blank = 0,
    scalar = 1,
    complex = 2,
    surrogate = 2,
    grapheme = 2,
    background = 3,
};

// 4-bit column semantics.
enum class cell_width : std::uint8_t
{
    narrow = 0,
    wide = 1,
    tail = 2,
    spacer_head = 3,
};

// 8-bit flags. The upper four bits are reserved.
enum class cell_flags : std::uint8_t
{
    none = 0,
    protected_cell = 1u << 0,
    hyperlink = 1u << 1,
    semantic_input = 1u << 2,
    semantic_prompt = 1u << 3,
};

// Raw 24-bit cluster offset used by the grapheme store and cell extent.
using cluster_offset = std::uint32_t;

// 24-bit offset + 8-bit length packed into one 32-bit enum. Keeping it as an
// enum class makes the intended cell field explicit without adding a struct.
enum class cell_extent : std::int32_t
{
};

inline constexpr cell_extent make_cell_extent(std::uint32_t offset, std::uint8_t length) noexcept
{
    return static_cast<cell_extent>(
        static_cast<std::int32_t>((offset & 0xFFFFFFu) | (static_cast<std::uint32_t>(length) << 24)));
}

[[nodiscard]] inline constexpr std::uint32_t extent_offset(cell_extent value) noexcept
{
    return static_cast<std::uint32_t>(std::to_underlying(value)) & 0xFFFFFFu;
}

[[nodiscard]] inline constexpr std::uint8_t extent_length(cell_extent value) noexcept
{
    return static_cast<std::uint8_t>((static_cast<std::uint32_t>(std::to_underlying(value)) >> 24) & 0xFFu);
}

// 32-bit screen-wide style ID. 0 is the default style.
enum class cell_style : std::int32_t
{
};

// 32-bit screen-wide hyperlink ID. 0 means no hyperlink.
enum class cell_hyperlink : std::int32_t
{
};

inline constexpr cell_style default_cell_style{0};
inline constexpr cell_hyperlink no_cell_hyperlink{0};

// 16-byte cell. The two 32-bit IDs and the packed extent make the struct
// exactly 16 bytes; type/width/flags are bitfields to keep the hot header
// compact and to make tests of individual flags cheap.
struct cell
{
    static constexpr std::uint8_t max_cluster_length = 255;

    char16_t primary_{};
    cell_type type_ : 4;
    cell_width width_ : 4;
    cell_flags flags_ : 8;
    cell_extent extent_{};
    cell_style style_{};
    cell_hyperlink hyperlink_{};

    constexpr cell() noexcept = default;

    [[nodiscard]] static constexpr cell blank(cell_style style = default_cell_style,
                                              cell_hyperlink hyperlink = no_cell_hyperlink) noexcept
    {
        cell result;
        result.primary_ = u' ';
        result.type_ = cell_type::blank;
        result.width_ = cell_width::narrow;
        result.flags_ = cell_flags::none;
        result.extent_ = make_cell_extent(0, 0);
        result.style_ = style;
        result.hyperlink_ = hyperlink;
        return result;
    }

    [[nodiscard]] static constexpr cell background(cell_style style = default_cell_style,
                                                   cell_hyperlink hyperlink = no_cell_hyperlink) noexcept
    {
        auto result = blank(style, hyperlink);
        result.type_ = cell_type::background;
        return result;
    }

    [[nodiscard]] static constexpr cell tail(cell_style style = default_cell_style,
                                             cell_hyperlink hyperlink = no_cell_hyperlink) noexcept
    {
        auto result = blank(style, hyperlink);
        result.width_ = cell_width::tail;
        return result;
    }

    [[nodiscard]] static constexpr cell spacer_head(cell_style style = default_cell_style,
                                                    cell_hyperlink hyperlink = no_cell_hyperlink) noexcept
    {
        auto result = blank(style, hyperlink);
        result.width_ = cell_width::spacer_head;
        return result;
    }

    [[nodiscard]] static constexpr cell scalar(char16_t primary, cell_style style = default_cell_style,
                                               cell_width width = cell_width::narrow,
                                               cell_hyperlink hyperlink = no_cell_hyperlink,
                                               cell_flags flags = cell_flags::none) noexcept
    {
        cell result;
        result.primary_ = is_surrogate(primary) ? u'\uFFFD' : primary;
        result.type_ = cell_type::scalar;
        result.width_ = width;
        result.flags_ = flags;
        result.extent_ = make_cell_extent(0, 0);
        result.style_ = style;
        result.hyperlink_ = hyperlink;
        return result;
    }

    [[nodiscard]] static constexpr cell complex(std::uint32_t offset, std::uint8_t length,
                                                cell_style style = default_cell_style,
                                                cell_width width = cell_width::narrow,
                                                cell_hyperlink hyperlink = no_cell_hyperlink,
                                                cell_flags flags = cell_flags::none) noexcept
    {
        cell result;
        result.primary_ = u'\uFFFD';
        result.type_ = cell_type::complex;
        result.width_ = width;
        result.flags_ = flags;
        result.extent_ = make_cell_extent(offset, length);
        result.style_ = style;
        result.hyperlink_ = hyperlink;
        return result;
    }

    [[nodiscard]] static constexpr cell surrogate(std::uint32_t offset, std::uint8_t length,
                                                  cell_style style = default_cell_style,
                                                  cell_width width = cell_width::narrow,
                                                  cell_hyperlink hyperlink = no_cell_hyperlink,
                                                  cell_flags flags = cell_flags::none) noexcept
    {
        return complex(offset, length, style, width, hyperlink, flags);
    }

    [[nodiscard]] static constexpr cell grapheme(std::uint32_t offset, std::uint8_t length,
                                                 cell_style style = default_cell_style,
                                                 cell_width width = cell_width::narrow,
                                                 cell_hyperlink hyperlink = no_cell_hyperlink,
                                                 cell_flags flags = cell_flags::none) noexcept
    {
        return complex(offset, length, style, width, hyperlink, flags);
    }

    [[nodiscard]] constexpr char16_t primary() const noexcept
    {
        return primary_;
    }

    [[nodiscard]] constexpr char16_t text_unit() const noexcept
    {
        return is_blank() ? u' ' : primary_;
    }

    [[nodiscard]] constexpr cell_style style() const noexcept
    {
        return style_;
    }

    [[nodiscard]] constexpr cell_hyperlink hyperlink() const noexcept
    {
        return hyperlink_;
    }

    [[nodiscard]] constexpr cell_type type() const noexcept
    {
        return type_;
    }

    [[nodiscard]] constexpr cell_width width() const noexcept
    {
        return width_;
    }

    [[nodiscard]] constexpr std::uint32_t offset() const noexcept
    {
        return extent_offset(extent_);
    }

    [[nodiscard]] constexpr std::uint8_t length() const noexcept
    {
        return extent_length(extent_);
    }

    [[nodiscard]] constexpr cell_flags flags() const noexcept
    {
        return flags_;
    }

    [[nodiscard]] constexpr bool has(cell_flags value) const noexcept
    {
        return (std::to_underlying(flags_) & std::to_underlying(value)) != 0;
    }

    [[nodiscard]] constexpr bool is_blank() const noexcept
    {
        return type_ == cell_type::blank || type_ == cell_type::background;
    }

    [[nodiscard]] constexpr bool has_cluster() const noexcept
    {
        return type_ == cell_type::complex;
    }

    [[nodiscard]] constexpr bool is_complex() const noexcept
    {
        return has_cluster();
    }

    [[nodiscard]] constexpr bool is_wide() const noexcept
    {
        return width_ == cell_width::wide;
    }

    [[nodiscard]] constexpr bool is_continuation() const noexcept
    {
        return width_ == cell_width::tail;
    }

    [[nodiscard]] constexpr bool is_spacer_head() const noexcept
    {
        return width_ == cell_width::spacer_head;
    }

    [[nodiscard]] constexpr cell with_style(cell_style value) const noexcept
    {
        auto result = *this;
        result.style_ = value;
        return result;
    }

    [[nodiscard]] constexpr cell with_hyperlink(cell_hyperlink value) const noexcept
    {
        auto result = *this;
        result.hyperlink_ = value;
        return result;
    }

    [[nodiscard]] constexpr cell with_flag(cell_flags value, bool enabled = true) const noexcept
    {
        auto result = *this;
        const auto bit = std::to_underlying(value);
        const auto old = std::to_underlying(flags_);
        result.flags_ = static_cast<cell_flags>(enabled ? (old | bit) : (old & ~bit));
        return result;
    }

    [[nodiscard]] constexpr cell with_cluster_ref(std::uint32_t offset, std::uint8_t length) const noexcept
    {
        auto result = *this;
        result.extent_ = make_cell_extent(offset, length);
        return result;
    }

    friend constexpr bool operator==(const cell &, const cell &) noexcept = default;

  private:
    [[nodiscard]] static constexpr bool is_surrogate(char16_t value) noexcept
    {
        return (value & 0xF800u) == 0xD800u;
    }
};

static_assert(sizeof(cell) == 16);
static_assert(alignof(cell) == alignof(std::int32_t));
static_assert(std::is_trivially_copyable_v<cell>);
static_assert(std::is_standard_layout_v<cell>);

} // namespace corehost::screen
