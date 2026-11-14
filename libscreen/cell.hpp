#pragma once

#include <cstdint>
#include <type_traits>
#include <utility>

namespace corehost::screen
{

// cell_info packs type, width and flags into one 16-bit value:
//   bits 0..2   type
//   bits 3..5   width
//   bits 6..15  flags
enum class cell_info : std::uint16_t
{
    // ---- cell_type: bits 0-2 ----
    // complex/surrogate/grapheme are aliases: primary is U+FFFD and the full
    // UTF-16 cluster lives in the store.
    /* skip blank slot
    blank      = 0u,
    */
    scalar = 1u << 0,
    complex = 1u << 1, // three synonyms
    surrogate = 1u << 1,
    grapheme = 1u << 1,
    background = 1u << 2,

    // ---- cell_width: bits 3-5 ----
    /* skip narrow slot
    narrow      = 0u,
    */
    wide = 1u << 3,
    tail = 1u << 4,
    spacer_head = 1u << 5,

    // ---- cell_flags: bits 6-15 ----
    /* skip none slot
    none            = 0u,
    */
    protected_cell = 1u << 6,
    hyperlink = 1u << 7,
    semantic_input = 1u << 8,
    semantic_prompt = 1u << 9,
};

namespace cell_info_ops
{
// ---- field masks ----
constexpr std::uint16_t cell_type_mask = 0x07u;    // bits 0-2
constexpr std::uint16_t cell_width_mask = 0x38u;   // bits 3-5
constexpr std::uint16_t cell_flags_mask = 0xFFC0u; // bits 6-15

constexpr std::uint16_t raw(cell_info c) noexcept
{
    return static_cast<std::uint16_t>(c);
}

constexpr std::uint16_t type_of(cell_info c) noexcept
{
    return raw(c) & cell_type_mask;
}

constexpr std::uint16_t width_of(cell_info c) noexcept
{
    return raw(c) & cell_width_mask;
}

constexpr std::uint16_t flags_of(cell_info c) noexcept
{
    return raw(c) & cell_flags_mask;
}

// ---- cell_type: bits 0-2 ----
constexpr bool is_type_blank(cell_info c) noexcept
{
    return type_of(c) == 0;
}

constexpr bool is_type_scalar(cell_info c) noexcept
{
    return type_of(c) == raw(cell_info::scalar);
}

constexpr bool is_type_complex(cell_info c) noexcept
{
    return type_of(c) == raw(cell_info::complex);
}

constexpr bool is_type_surrogate(cell_info c) noexcept
{
    return is_type_complex(c);
}

constexpr bool is_type_grapheme(cell_info c) noexcept
{
    return is_type_complex(c);
}

constexpr bool is_type_background(cell_info c) noexcept
{
    return type_of(c) == raw(cell_info::background);
}

// ---- cell_width: bits 3-5 ----
constexpr bool is_width_narrow(cell_info c) noexcept
{
    return width_of(c) == 0;
}

constexpr bool is_width_wide(cell_info c) noexcept
{
    return width_of(c) == raw(cell_info::wide);
}

constexpr bool is_width_tail(cell_info c) noexcept
{
    return width_of(c) == raw(cell_info::tail);
}

constexpr bool is_width_spacer_head(cell_info c) noexcept
{
    return width_of(c) == raw(cell_info::spacer_head);
}

// ---- cell_flags: bits 6-15 ----
constexpr bool is_flags_none(cell_info c) noexcept
{
    return flags_of(c) == 0;
}

// flags are combinable; test bits rather than equality.
constexpr bool is_flags_protected(cell_info c) noexcept
{
    return (raw(c) & raw(cell_info::protected_cell)) != 0;
}

constexpr bool is_flags_hyperlink(cell_info c) noexcept
{
    return (raw(c) & raw(cell_info::hyperlink)) != 0;
}

constexpr bool is_flags_semantic_input(cell_info c) noexcept
{
    return (raw(c) & raw(cell_info::semantic_input)) != 0;
}

constexpr bool is_flags_semantic_prompt(cell_info c) noexcept
{
    return (raw(c) & raw(cell_info::semantic_prompt)) != 0;
}

// Small composition helpers used by cell factories and callers. They do not
// add enum values; they only manipulate the packed bit ranges above.
constexpr cell_info combine(cell_info left, cell_info right) noexcept
{
    return static_cast<cell_info>(raw(left) | raw(right));
}

constexpr cell_info with_type(cell_info value, cell_info type) noexcept
{
    return static_cast<cell_info>((raw(value) & ~cell_type_mask) | (raw(type) & cell_type_mask));
}

constexpr cell_info with_width(cell_info value, cell_info width) noexcept
{
    return static_cast<cell_info>((raw(value) & ~cell_width_mask) | (raw(width) & cell_width_mask));
}
} // namespace cell_info_ops

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

// 16-byte cell:
//   char16_t primary   2
//   cell_info info     2
//   cell_extent extent 4
//   cell_style style   4
//   cell_hyperlink     4
struct cell
{
    static constexpr std::uint8_t max_cluster_length = 255;

    char16_t primary_{};
    cell_info info_{};
    cell_extent extent_{};
    cell_style style_{};
    cell_hyperlink hyperlink_{};

    constexpr cell() noexcept = default;

    [[nodiscard]] static constexpr cell blank(cell_style style = default_cell_style,
                                              cell_hyperlink hyperlink = no_cell_hyperlink,
                                              cell_info flags = cell_info{}) noexcept
    {
        cell result;
        result.primary_ = u' ';
        result.info_ = cell_info_ops::with_type(flags, cell_info{});
        result.extent_ = make_cell_extent(0, 0);
        result.style_ = style;
        result.hyperlink_ = hyperlink;
        return result;
    }

    [[nodiscard]] static constexpr cell background(cell_style style = default_cell_style,
                                                   cell_hyperlink hyperlink = no_cell_hyperlink,
                                                   cell_info flags = cell_info{}) noexcept
    {
        auto result = blank(style, hyperlink, flags);
        result.info_ = cell_info_ops::with_type(result.info_, cell_info::background);
        return result;
    }

    [[nodiscard]] static constexpr cell tail(cell_style style = default_cell_style,
                                             cell_hyperlink hyperlink = no_cell_hyperlink,
                                             cell_info flags = cell_info{}) noexcept
    {
        auto result = blank(style, hyperlink, flags);
        result.info_ = cell_info_ops::with_width(result.info_, cell_info::tail);
        return result;
    }

    [[nodiscard]] static constexpr cell spacer_head(cell_style style = default_cell_style,
                                                    cell_hyperlink hyperlink = no_cell_hyperlink,
                                                    cell_info flags = cell_info{}) noexcept
    {
        auto result = blank(style, hyperlink, flags);
        result.info_ = cell_info_ops::with_width(result.info_, cell_info::spacer_head);
        return result;
    }

    [[nodiscard]] static constexpr cell scalar(char16_t primary, cell_style style = default_cell_style,
                                               cell_info info = cell_info{},
                                               cell_hyperlink hyperlink = no_cell_hyperlink) noexcept
    {
        cell result;
        result.primary_ = is_surrogate(primary) ? u'\uFFFD' : primary;
        result.info_ = cell_info_ops::with_type(info, cell_info::scalar);
        result.extent_ = make_cell_extent(0, 0);
        result.style_ = style;
        result.hyperlink_ = hyperlink;
        return result;
    }

    [[nodiscard]] static constexpr cell complex(std::uint32_t offset, std::uint8_t length,
                                                cell_style style = default_cell_style, cell_info info = cell_info{},
                                                cell_hyperlink hyperlink = no_cell_hyperlink) noexcept
    {
        cell result;
        result.primary_ = u'\uFFFD';
        result.info_ = cell_info_ops::with_type(info, cell_info::complex);
        result.extent_ = make_cell_extent(offset, length);
        result.style_ = style;
        result.hyperlink_ = hyperlink;
        return result;
    }

    [[nodiscard]] static constexpr cell surrogate(std::uint32_t offset, std::uint8_t length,
                                                  cell_style style = default_cell_style, cell_info info = cell_info{},
                                                  cell_hyperlink hyperlink = no_cell_hyperlink) noexcept
    {
        return complex(offset, length, style, info, hyperlink);
    }

    [[nodiscard]] static constexpr cell grapheme(std::uint32_t offset, std::uint8_t length,
                                                 cell_style style = default_cell_style, cell_info info = cell_info{},
                                                 cell_hyperlink hyperlink = no_cell_hyperlink) noexcept
    {
        return complex(offset, length, style, info, hyperlink);
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

    [[nodiscard]] constexpr cell_info info() const noexcept
    {
        return info_;
    }

    [[nodiscard]] constexpr std::uint32_t offset() const noexcept
    {
        return extent_offset(extent_);
    }

    [[nodiscard]] constexpr std::uint8_t length() const noexcept
    {
        return extent_length(extent_);
    }

    [[nodiscard]] constexpr bool has(cell_info flag) const noexcept
    {
        const auto bits = cell_info_ops::raw(flag) & cell_info_ops::cell_flags_mask;
        return (cell_info_ops::raw(info_) & bits) != 0;
    }

    [[nodiscard]] constexpr bool is_blank() const noexcept
    {
        return cell_info_ops::is_type_blank(info_) || cell_info_ops::is_type_background(info_);
    }

    [[nodiscard]] constexpr bool has_cluster() const noexcept
    {
        return cell_info_ops::is_type_complex(info_);
    }

    [[nodiscard]] constexpr bool is_complex() const noexcept
    {
        return has_cluster();
    }

    [[nodiscard]] constexpr bool is_wide() const noexcept
    {
        return cell_info_ops::is_width_wide(info_);
    }

    [[nodiscard]] constexpr bool is_continuation() const noexcept
    {
        return cell_info_ops::is_width_tail(info_);
    }

    [[nodiscard]] constexpr bool is_spacer_head() const noexcept
    {
        return cell_info_ops::is_width_spacer_head(info_);
    }

    [[nodiscard]] constexpr cell with_info(cell_info value) const noexcept
    {
        auto result = *this;
        result.info_ = value;
        return result;
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

    [[nodiscard]] constexpr cell with_flag(cell_info value, bool enabled = true) const noexcept
    {
        auto result = *this;
        const auto bit = cell_info_ops::raw(value) & cell_info_ops::cell_flags_mask;
        const auto old = cell_info_ops::raw(info_);
        result.info_ = static_cast<cell_info>(enabled ? (old | bit) : (old & ~bit));
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
