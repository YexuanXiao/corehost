#pragma once

#include <cstdint>
#include <type_traits>
#include <utility>

namespace corehost::screen
{

// Page-local style IDs are 32-bit so a style table entry can be shared across
// pages and scrollback without remapping cells.
using style_id = std::uint32_t;

// Hyperlink IDs are shared by the whole screen buffer. 0 means "no link".
using hyperlink_id = std::uint32_t;

// A 24-bit offset into the screen's grapheme UTF-16 store.
using cluster_offset = std::uint32_t;

// 2-bit semantic content tag. complex/surrogate/grapheme are aliases: all
// mean "primary is U+FFFD and the complete UTF-16 cluster is in the store".
enum class cell_content : std::uint8_t
{
    blank = 0,
    scalar = 1,
    complex = 2,
    surrogate = 2,
    grapheme = 2,
    background = 3,
};

// 2-bit column semantics.
enum class cell_width : std::uint8_t
{
    narrow = 0,
    wide = 1,
    tail = 2,
    spacer_head = 3,
};

// 12-bit cell flags; the high eight bits are reserved for future attributes.
enum class cell_flag : std::uint16_t
{
    none = 0,
    protected_cell = 1u << 0,
    hyperlink = 1u << 1,
    semantic_input = 1u << 2,
    semantic_prompt = 1u << 3,
};

// 16-byte screen cell.
//
// Layout (two 64-bit words):
//   word 0: primary 16 | content 2 | width 2 | offset 24 | length 8 | flags 12
//   word 1: style 32 | hyperlink 32
//
// The primary is a UTF-16 code unit. For scalar it is the actual BMP code
// unit; for content=2 it is always U+FFFD and the real UTF-16 cluster lives
// in screen_buffer::graphemes() at offset/length.
class cell final
{
  public:
    using flag = cell_flag;

    static constexpr std::uint8_t max_cluster_length = 255;

    constexpr cell() noexcept = default;

    [[nodiscard]] static constexpr cell blank(style_id style = 0, hyperlink_id hyperlink = 0) noexcept
    {
        return make(u' ', style, hyperlink, 0, 0, cell_content::blank, cell_width::narrow, 0);
    }

    [[nodiscard]] static constexpr cell background(style_id style = 0, hyperlink_id hyperlink = 0) noexcept
    {
        return make(u' ', style, hyperlink, 0, 0, cell_content::background, cell_width::narrow, 0);
    }

    [[nodiscard]] static constexpr cell tail(style_id style = 0, hyperlink_id hyperlink = 0) noexcept
    {
        return make(u' ', style, hyperlink, 0, 0, cell_content::blank, cell_width::tail, 0);
    }

    [[nodiscard]] static constexpr cell spacer_head(style_id style = 0, hyperlink_id hyperlink = 0) noexcept
    {
        return make(u' ', style, hyperlink, 0, 0, cell_content::blank, cell_width::spacer_head, 0);
    }

    [[nodiscard]] static constexpr cell scalar(char16_t primary, style_id style = 0,
                                               cell_width width = cell_width::narrow, hyperlink_id hyperlink = 0,
                                               std::uint16_t flags = 0) noexcept
    {
        return make(is_surrogate(primary) ? u'\uFFFD' : primary, style, hyperlink, 0, 0, cell_content::scalar, width,
                    flags);
    }

    [[nodiscard]] static constexpr cell complex(cluster_offset offset, std::uint8_t length, style_id style = 0,
                                                cell_width width = cell_width::narrow, hyperlink_id hyperlink = 0,
                                                std::uint16_t flags = 0) noexcept
    {
        return make(u'\uFFFD', style, hyperlink, offset, length, cell_content::complex, width, flags);
    }

    [[nodiscard]] static constexpr cell surrogate(cluster_offset offset, std::uint8_t length, style_id style = 0,
                                                  cell_width width = cell_width::narrow, hyperlink_id hyperlink = 0,
                                                  std::uint16_t flags = 0) noexcept
    {
        return complex(offset, length, style, width, hyperlink, flags);
    }

    [[nodiscard]] static constexpr cell grapheme(cluster_offset offset, std::uint8_t length, style_id style = 0,
                                                 cell_width width = cell_width::narrow, hyperlink_id hyperlink = 0,
                                                 std::uint16_t flags = 0) noexcept
    {
        return complex(offset, length, style, width, hyperlink, flags);
    }

    [[nodiscard]] constexpr char16_t primary() const noexcept
    {
        return static_cast<char16_t>(primary_);
    }

    // A single-unit view for APIs that cannot consume a cluster. blank returns
    // a space; complex returns the stored U+FFFD primary.
    [[nodiscard]] constexpr char16_t text_unit() const noexcept
    {
        return is_blank() ? u' ' : primary();
    }

    [[nodiscard]] constexpr style_id style() const noexcept
    {
        return style_;
    }

    [[nodiscard]] constexpr hyperlink_id hyperlink() const noexcept
    {
        return hyperlink_;
    }

    [[nodiscard]] constexpr cell_content content() const noexcept
    {
        return static_cast<cell_content>(content_);
    }

    [[nodiscard]] constexpr cell_width width() const noexcept
    {
        return static_cast<cell_width>(width_);
    }

    [[nodiscard]] constexpr cluster_offset offset() const noexcept
    {
        return offset_;
    }

    // Actual UTF-16 unit count, 1..255. The field itself stores the value
    // directly; there is no minus-one encoding.
    [[nodiscard]] constexpr std::uint8_t length() const noexcept
    {
        return length_;
    }

    [[nodiscard]] constexpr std::uint16_t flags() const noexcept
    {
        return flags_;
    }

    [[nodiscard]] constexpr bool has(flag value) const noexcept
    {
        return (flags_ & std::to_underlying(value)) != 0;
    }

    [[nodiscard]] constexpr bool is_blank() const noexcept
    {
        return content_ == std::to_underlying(cell_content::blank) ||
               content_ == std::to_underlying(cell_content::background);
    }

    [[nodiscard]] constexpr bool has_cluster() const noexcept
    {
        return content_ == std::to_underlying(cell_content::complex);
    }

    [[nodiscard]] constexpr bool is_complex() const noexcept
    {
        return has_cluster();
    }

    [[nodiscard]] constexpr bool is_wide() const noexcept
    {
        return width_ == std::to_underlying(cell_width::wide);
    }

    [[nodiscard]] constexpr bool is_continuation() const noexcept
    {
        return width_ == std::to_underlying(cell_width::tail);
    }

    [[nodiscard]] constexpr bool is_spacer_head() const noexcept
    {
        return width_ == std::to_underlying(cell_width::spacer_head);
    }

    [[nodiscard]] constexpr cell with_style(style_id value) const noexcept
    {
        auto result = *this;
        result.style_ = value;
        return result;
    }

    [[nodiscard]] constexpr cell with_hyperlink(hyperlink_id value) const noexcept
    {
        auto result = *this;
        result.hyperlink_ = value;
        return result;
    }

    [[nodiscard]] constexpr cell with_flag(flag value, bool enabled = true) const noexcept
    {
        auto result = *this;
        const auto bit = std::to_underlying(value);
        result.flags_ = enabled ? static_cast<std::uint16_t>(result.flags_ | bit)
                                : static_cast<std::uint16_t>(result.flags_ & ~bit);
        return result;
    }

    [[nodiscard]] constexpr cell with_cluster_ref(cluster_offset offset, std::uint8_t length) const noexcept
    {
        auto result = *this;
        result.offset_ = offset;
        result.length_ = length;
        return result;
    }

    friend constexpr bool operator==(const cell &, const cell &) noexcept = default;

  private:
    [[nodiscard]] static constexpr cell make(char16_t primary, style_id style, hyperlink_id hyperlink,
                                             cluster_offset offset, std::uint8_t length, cell_content content,
                                             cell_width width, std::uint16_t flags) noexcept
    {
        cell result;
        result.primary_ = static_cast<std::uint64_t>(static_cast<std::uint16_t>(primary));
        result.content_ = std::to_underlying(content);
        result.width_ = std::to_underlying(width);
        result.offset_ = offset & 0xFFFFFFu;
        result.length_ = length;
        result.flags_ = flags & 0x0FFFu;
        result.style_ = style;
        result.hyperlink_ = hyperlink;
        return result;
    }

    [[nodiscard]] static constexpr bool is_surrogate(char16_t value) noexcept
    {
        return (value & 0xF800u) == 0xD800u;
    }

    std::uint64_t primary_ : 16;
    std::uint64_t content_ : 2;
    std::uint64_t width_ : 2;
    std::uint64_t offset_ : 24;
    std::uint64_t length_ : 8;
    std::uint64_t flags_ : 12;
    std::uint64_t style_ : 32;
    std::uint64_t hyperlink_ : 32;
};

static_assert(sizeof(cell) == 16);
static_assert(alignof(cell) == alignof(std::uint64_t));
static_assert(std::is_trivially_copyable_v<cell>);
static_assert(std::is_standard_layout_v<cell>);

} // namespace corehost::screen
