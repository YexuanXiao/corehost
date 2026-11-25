#pragma once

#include "cell.hpp"
#include "geometry.hpp"
#include "grapheme_store.hpp"
#include "hyperlink.hpp"
#include "image.hpp"
#include "legacy_projection.hpp"
#include "text_writer.hpp"
#include "style.hpp"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include "deque.hpp"
#include <flat_map>
#include <limits>
#include <utility>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace corehost::screen
{

// Row-level line flags. These are shared by Windows Terminal and Contour and
// cannot be represented correctly as per-cell flags.
enum class line_flag : std::uint16_t
{
    none = 0,
    wrappable = 1u << 0,
    wrapped = 1u << 1,
    marked = 1u << 2,
    output_start = 1u << 3,
    double_width = 1u << 4,
    double_height_top = 1u << 5,
    double_height_bottom = 1u << 6,
    command_end = 1u << 7,
    prompt_end = 1u << 8,
    user_mark = 1u << 9,
};

using line_flags = std::uint16_t;

namespace line_flag_ops
{
constexpr line_flags raw(line_flag value) noexcept
{
    return static_cast<line_flags>(value);
}

constexpr line_flags combine(line_flags left, line_flags right) noexcept
{
    return static_cast<line_flags>(left | right);
}

constexpr bool has(line_flags value, line_flag flag) noexcept
{
    return (value & raw(flag)) != 0;
}

constexpr line_flags without(line_flags value, line_flag flag) noexcept
{
    return static_cast<line_flags>(value & ~raw(flag));
}
} // namespace line_flag_ops

// Platform-neutral CHAR_INFO analogue: UTF-16 character plus legacy Win32
// attributes. The conversion to/from CHAR_INFO belongs at the API boundary.
struct legacy_cell final
{
    char16_t character{u' '};
    std::uint16_t attributes{0x07};
};

inline constexpr std::int32_t no_command_exit_code = std::numeric_limits<std::int32_t>::min();

struct row_state final
{
    std::uint32_t used_columns{};
    line_flags flags{};
    std::int32_t command_exit_code{no_command_exit_code};
    std::uint32_t dirty_begin{};
    std::uint32_t dirty_end{};

    [[nodiscard]] bool is_dirty() const noexcept
    {
        return dirty_begin < dirty_end;
    }
};

// History rows only need line flags and command status; used-column and damage
// metadata are active-page concerns and would inflate scrollback.
struct history_state final
{
    line_flags flags{};
    std::int32_t command_exit_code{no_command_exit_code};
};

using cell_vector = std::vector<cell>;

struct row_block final
{
    cell_vector cells;
    row_state state;
    grapheme_store graphemes;
};

struct history_row final
{
    cell_vector cells;
    history_state state;
    grapheme_store graphemes;
};

static_assert(sizeof(row_state) <= 24);
static_assert(sizeof(history_state) <= 8);
static_assert(sizeof(history_row) < sizeof(row_block));

struct rectangle final
{
    std::size_t x{};
    std::size_t y{};
    std::size_t width{};
    std::size_t height{};
};

// Page rows are independently owned row blocks in a bizwen::deque. This keeps
// a row contiguous for span/CHAR_INFO projection while allowing O(1) row moves
// into history without copying 16B cells.
class page final
{
  public:
    page(std::size_t width, std::size_t height, cell_style fill_style = default_cell_style);

    [[nodiscard]] std::size_t width() const noexcept
    {
        return width_;
    }

    [[nodiscard]] std::size_t height() const noexcept
    {
        return height_;
    }

    [[nodiscard]] cell &at(std::size_t x, std::size_t y) noexcept
    {
        assert(x < width_ && y < height_);
        return rows_[y].cells[x];
    }

    [[nodiscard]] const cell &at(std::size_t x, std::size_t y) const noexcept
    {
        assert(x < width_ && y < height_);
        return rows_[y].cells[x];
    }

    [[nodiscard]] std::span<cell> row_cells(std::size_t y) noexcept
    {
        assert(y < height_);
        return std::span<cell>{rows_[y].cells};
    }

    [[nodiscard]] std::span<const cell> row_cells(std::size_t y) const noexcept
    {
        assert(y < height_);
        return std::span<const cell>{rows_[y].cells};
    }

    [[nodiscard]] row_state &row(std::size_t y) noexcept
    {
        assert(y < height_);
        return rows_[y].state;
    }

    [[nodiscard]] const row_state &row(std::size_t y) const noexcept
    {
        assert(y < height_);
        return rows_[y].state;
    }

    [[nodiscard]] bool has_damage() const noexcept
    {
        return damage_active_;
    }

    [[nodiscard]] bool full_damage() const noexcept
    {
        return full_damage_;
    }

    [[nodiscard]] row_block &block(std::size_t y) noexcept
    {
        assert(y < height_);
        return rows_[y];
    }

    [[nodiscard]] const row_block &block(std::size_t y) const noexcept
    {
        assert(y < height_);
        return rows_[y];
    }

    [[nodiscard]] grapheme_store &graphemes(std::size_t y) noexcept
    {
        assert(y < height_);
        return rows_[y].graphemes;
    }

    [[nodiscard]] const grapheme_store &graphemes(std::size_t y) const noexcept
    {
        assert(y < height_);
        return rows_[y].graphemes;
    }

    void clear(cell_style fill_style)
    {
        for (auto &row : rows_)
        {
            row.cells.assign(width_, cell::blank(fill_style));
            row.state = {};
            row.graphemes.clear();
        }
        mark_all_dirty();
    }

    void erase_front() noexcept
    {
        assert(rows_.size() == height_);
        rows_.pop_front();
    }

    void push_blank(cell_style fill_style)
    {
        assert(rows_.size() + 1 == height_);
        rows_.push_back(row_block{cell_vector(width_, cell::blank(fill_style)), row_state{}, {}});
    }

    void push_row(cell_vector &&cells)
    {
        assert(rows_.size() + 1 == height_);
        rows_.push_back(row_block{std::move(cells), row_state{}, {}});
    }

    void mark_dirty(std::size_t y, std::size_t begin, std::size_t end) noexcept;
    void mark_used(std::size_t y, std::size_t end) noexcept;
    void clear_damage() noexcept;
    void mark_all_dirty() noexcept;

  private:
    std::size_t width_{};
    std::size_t height_{};
    bool damage_active_{};
    bool full_damage_{};
    bizwen::deque<row_block> rows_;
};
class screen_buffer final
{
  public:
    using line = cell_vector;

    screen_buffer(std::size_t width, std::size_t height, std::size_t history_limit = 1000);

    [[nodiscard]] std::size_t width() const noexcept
    {
        return width_;
    }

    [[nodiscard]] coord buffer_size() const noexcept
    {
        return coord{static_cast<std::int32_t>(width_), static_cast<std::int32_t>(height_)};
    }

    [[nodiscard]] coord viewport_origin() const noexcept
    {
        return view_.origin();
    }

    [[nodiscard]] coord viewport_size() const noexcept
    {
        return view_.size();
    }

    [[nodiscard]] viewport &view() noexcept
    {
        return view_;
    }

    [[nodiscard]] const viewport &view() const noexcept
    {
        return view_;
    }

    void set_viewport(rect area) noexcept
    {
        (void)view_.set_rect(area, buffer_size());
    }

    [[nodiscard]] bool snap_viewport_to_cursor(coord cursor) noexcept
    {
        return view_.snap_to_cursor(cursor, buffer_size());
    }

    [[nodiscard]] std::size_t height() const noexcept
    {
        return height_;
    }

    [[nodiscard]] const cell &at(std::size_t x, std::size_t y) const noexcept;
    [[nodiscard]] std::optional<cell> try_at(coord position) const noexcept;
    [[nodiscard]] char_info char_info_at(coord position) const noexcept;
    [[nodiscard]] std::span<cell> row_cells(std::size_t y) noexcept;
    [[nodiscard]] std::span<const cell> row_cells(std::size_t y) const noexcept;
    [[nodiscard]] const row_block &block(std::size_t y) const noexcept;
    [[nodiscard]] row_state &row(std::size_t y) noexcept;
    [[nodiscard]] const row_state &row(std::size_t y) const noexcept;

    [[nodiscard]] line_flags flags(std::size_t y) const noexcept;
    void set_flags(std::size_t y, line_flags value) noexcept;
    void add_flags(std::size_t y, line_flag value) noexcept;
    void remove_flags(std::size_t y, line_flag value) noexcept;
    [[nodiscard]] bool has_flag(std::size_t y, line_flag value) const noexcept;
    [[nodiscard]] bool is_double_width(std::size_t y) const noexcept;
    [[nodiscard]] bool is_double_height_top(std::size_t y) const noexcept;
    [[nodiscard]] bool is_double_height_bottom(std::size_t y) const noexcept;
    void set_command_exit_code(std::size_t y, std::int32_t code) noexcept;
    void clear_command_exit_code(std::size_t y) noexcept;
    [[nodiscard]] std::optional<std::int32_t> command_exit_code(std::size_t y) const noexcept;

    // Writes a single UTF-16 code unit. width=wide reserves x+1 as a tail.
    // Returns false when the write would escape the page or the grapheme store
    // cannot accept the cluster.
    [[nodiscard]] bool write_scalar(std::size_t x, std::size_t y, char16_t primary,
                                    cell_style style = default_cell_style, cell_width width = cell_width::narrow,
                                    cell_hyperlink hyperlink = no_cell_hyperlink, cell_flags flags = cell_flags::none);
    // Writes printable ASCII cells without re-selecting the page or checking
    // bounds per cell. Stops at the end of the row.
    std::size_t write_ascii(std::size_t x, std::size_t y, std::u16string_view text, cell_style style,
                            cell_hyperlink hyperlink);

    // Writes a complete UTF-16 cluster, including its primary. The stored cell
    // primary is U+FFFD and offset/length point into graphemes().
    [[nodiscard]] bool write_cluster(std::size_t x, std::size_t y, std::span<const char16_t> cluster,
                                     cell_style style = default_cell_style, cell_width width = cell_width::narrow,
                                     cell_hyperlink hyperlink = no_cell_hyperlink, cell_flags flags = cell_flags::none);

    // Converts a Unicode scalar to one UTF-16 unit or a surrogate-pair cluster.
    // Invalid scalars are normalized to U+FFFD. The caller owns column width.
    [[nodiscard]] bool write_codepoint(std::size_t x, std::size_t y, char32_t codepoint,
                                       cell_style style = default_cell_style, cell_width width = cell_width::narrow,
                                       cell_hyperlink hyperlink = no_cell_hyperlink,
                                       cell_flags flags = cell_flags::none);

    // Appends the complete glyph at x as UTF-16. A trailing cell resolves to its
    // leading cell. Blank/background cells produce one space.
    template <typename String>
    void glyph_at_into(std::size_t x, std::size_t y, String &result) const;

    // First Unicode scalar of the glyph at x. Surrogate pairs are decoded.
    [[nodiscard]] char32_t codepoint_at(std::size_t x, std::size_t y) const;

    // Column width of the glyph at x: 0 for a trailing half, 2 for a wide head.
    [[nodiscard]] int glyph_width(std::size_t x, std::size_t y) const;

    [[nodiscard]] bool is_trailing(std::size_t x, std::size_t y) const;

    [[nodiscard]] std::size_t used_columns(std::size_t y) const;

    // Appends the visible row text, skipping trailing wide-cell columns.
    template <typename String>
    void line_text_into(std::size_t y, String &result) const;

    [[nodiscard]] legacy_cell legacy_at(std::size_t x, std::size_t y) const;

    // Convenience path for Console API writes that only carry a legacy WORD.
    [[nodiscard]] bool write_legacy_scalar(std::size_t x, std::size_t y, char16_t primary, std::uint16_t attributes,
                                           cell_width width = cell_width::narrow);

    // Writes an image-fragment cell. The fragment metadata lives in images();
    // rasterization/decoding is a renderer responsibility.
    [[nodiscard]] bool write_image(std::size_t x, std::size_t y, std::uint32_t fragment,
                                   cell_style style = default_cell_style, cell_width width = cell_width::narrow,
                                   cell_hyperlink hyperlink = no_cell_hyperlink, cell_flags flags = cell_flags::none);

    // Low-level write with wide-cell cleanup. The caller owns cluster allocation
    // for complex cells.
    [[nodiscard]] bool write_cell(std::size_t x, std::size_t y, cell value);

    void fill_rect(rectangle area, cell value);
    void clear(cell_style style = default_cell_style);

    // Console/CHAR_INFO boundary APIs. They clamp to the buffer and return the
    // number of cells actually processed; internal accessors still assert.
    [[nodiscard]] std::size_t read_char_info(rect area, std::span<char_info> out) const noexcept;
    [[nodiscard]] std::size_t write_char_info(rect area, std::span<const char_info> values) noexcept;
    [[nodiscard]] std::size_t read_char_info_abi(rect area, std::span<interop::win32::char_info> out) const noexcept;
    [[nodiscard]] std::size_t write_char_info_abi(rect area,
                                                  std::span<const interop::win32::char_info> values) noexcept;
    [[nodiscard]] std::size_t read_chars(coord start, std::span<char16_t> out) const noexcept;
    [[nodiscard]] std::size_t read_attrs(coord start, std::span<std::uint16_t> out) const noexcept;
    [[nodiscard]] std::size_t write_chars(coord start, std::u16string_view text) noexcept;
    [[nodiscard]] std::size_t write_attrs(coord start, std::span<const std::uint16_t> attrs) noexcept;
    void fill_chars(rect area, char16_t character) noexcept;
    void fill_attrs(rect area, std::uint16_t attributes) noexcept;
    [[nodiscard]] bool set_attr(coord position, std::uint16_t attributes) noexcept;
    [[nodiscard]] cell_style style_for_attributes(std::uint16_t attributes);
    [[nodiscard]] std::uint16_t attr_at(coord position) const noexcept;

    struct text_write_result final
    {
        std::size_t consumed{};
        bool row_end{};
    };

    template <text_measurement Mode = text_measurement::console, bool AmbiguousIsWide = false>
    text_write_result write_text_row(coord &cursor, std::u16string_view text, std::span<const std::uint8_t> widths,
                                     cell_style style, bool wrap_at_eol = true,
                                     cell_hyperlink hyperlink = no_cell_hyperlink) noexcept;
    text_write_result write_text_row_for(text_measurement measurement, bool ambiguous_is_wide, coord &cursor,
                                         std::u16string_view text, std::span<const std::uint8_t> widths,
                                         cell_style style, bool wrap_at_eol = true,
                                         cell_hyperlink hyperlink = no_cell_hyperlink) noexcept;
    text_write_result write_console_text_row(coord &cursor, std::u16string_view text,
                                             std::span<const std::uint8_t> widths, cell_style style,
                                             bool wrap_at_eol = true,
                                             cell_hyperlink hyperlink = no_cell_hyperlink) noexcept;
    text_write_result write_grapheme_text_row(coord &cursor, std::u16string_view text,
                                              std::span<const std::uint8_t> widths, bool ambiguous_is_wide,
                                              cell_style style, bool wrap_at_eol = true,
                                              cell_hyperlink hyperlink = no_cell_hyperlink) noexcept;
    text_write_result write_wcswidth_text_row(coord &cursor, std::u16string_view text,
                                              std::span<const std::uint8_t> widths, bool ambiguous_is_wide,
                                              cell_style style, bool wrap_at_eol = true,
                                              cell_hyperlink hyperlink = no_cell_hyperlink) noexcept;
    [[nodiscard]] bool write_glyph(coord position, std::u16string_view glyph, int columns, cell_style style,
                                   cell_hyperlink hyperlink = no_cell_hyperlink) noexcept;

    void copy_rect(rectangle source, rectangle clip, bool use_clip, coord destination) noexcept;
    void copy_rect_rows(std::int32_t copy_left, std::int32_t copy_top, std::int32_t copy_right,
                        std::int32_t copy_bottom, std::int32_t destination_x, std::int32_t destination_y,
                        bool descending) noexcept;
    void fill_scroll_segment(std::int32_t x, std::int32_t y, std::int32_t width, char16_t fill_char,
                             std::uint16_t fill_attr) noexcept;
    void scroll_rect(rectangle source, rectangle clip, bool use_clip, coord destination, char16_t fill_char,
                     std::uint16_t fill_attr) noexcept;

    // Full-page vertical scroll. On the main page, the top lines are moved to
    // scrollback; the alternate page never records history.
    void scroll_up(std::size_t lines, cell_style fill_style = default_cell_style);
    void scroll_region_up(rectangle area, std::size_t lines, cell_style fill_style = default_cell_style);
    void scroll_region_down(rectangle area, std::size_t lines, cell_style fill_style = default_cell_style);

    void resize(std::size_t width, std::size_t height, cell_style fill_style = default_cell_style,
                bool anchor_bottom = false, coord *cursor = nullptr);
    void set_alternate(bool enabled);
    [[nodiscard]] bool alternate_active() const noexcept
    {
        return alternate_active_;
    }

    [[nodiscard]] std::size_t history_size() const noexcept
    {
        return history_.size();
    }

    [[nodiscard]] std::size_t history_limit() const noexcept
    {
        return history_limit_;
    }

    void set_history_limit(std::size_t limit)
    {
        history_limit_ = limit;
        trim_history();
    }

    [[nodiscard]] std::span<const cell> history_line(std::size_t index) const;
    [[nodiscard]] line_flags history_flags(std::size_t index) const noexcept;
    [[nodiscard]] std::optional<std::int32_t> history_command_exit_code(std::size_t index) const noexcept;
    void clear_history();

    [[nodiscard]] style_table &styles() noexcept
    {
        return styles_;
    }

    [[nodiscard]] const style_table &styles() const noexcept
    {
        return styles_;
    }

    [[nodiscard]] hyperlink_table &hyperlinks() noexcept
    {
        return hyperlinks_;
    }

    [[nodiscard]] const hyperlink_table &hyperlinks() const noexcept
    {
        return hyperlinks_;
    }

    [[nodiscard]] image_table &images() noexcept
    {
        return images_;
    }

    [[nodiscard]] const image_table &images() const noexcept
    {
        return images_;
    }

    [[nodiscard]] const image_fragment *image_of(const cell &value) const noexcept;

    [[nodiscard]] grapheme_store &row_graphemes(std::size_t y) noexcept
    {
        return active_page().graphemes(y);
    }

    [[nodiscard]] const grapheme_store &row_graphemes(std::size_t y) const noexcept
    {
        return active_page().graphemes(y);
    }

    [[nodiscard]] grapheme_store &history_graphemes(std::size_t index) noexcept;
    [[nodiscard]] const grapheme_store &history_graphemes(std::size_t index) const noexcept;

    [[nodiscard]] std::span<const char16_t> cluster_of(std::size_t y, const cell &value) const noexcept;
    [[nodiscard]] std::span<const char16_t> history_cluster_of(std::size_t index, const cell &value) const noexcept;
    [[nodiscard]] bool rebuild_graphemes();
    [[nodiscard]] std::size_t damage_begin(std::size_t y) const noexcept;
    [[nodiscard]] std::size_t damage_end(std::size_t y) const noexcept;
    void clear_damage() noexcept;
    [[nodiscard]] bool has_damage() const noexcept
    {
        return active_page().has_damage();
    }
    [[nodiscard]] bool has_pending_scroll() const noexcept
    {
        return pending_scroll_up_ != 0;
    }
    [[nodiscard]] std::size_t take_scroll_up() noexcept
    {
        const auto value = pending_scroll_up_;
        pending_scroll_up_ = 0;
        return value;
    }

  private:
    [[nodiscard]] page &active_page() noexcept;
    [[nodiscard]] const page &active_page() const noexcept;
    void clear_wide_neighborhood(page &target, std::size_t x, std::size_t y);
    [[nodiscard]] cell_style style_for_legacy(std::uint16_t attributes);
    void trim_history();

    std::size_t width_{};
    std::size_t height_{};
    std::size_t history_limit_{};
    std::size_t pending_scroll_up_{};
    page main_;
    viewport view_;
    // Win32 multiple screen buffers are not modeled. This is the VT alternate
    // screen. Its storage is cleared on every leave and re-entry.
    page alternate_;
    bool alternate_active_{};
    bizwen::deque<history_row> history_;
    std::vector<cell_vector> spare_rows_;
    cell_vector blank_template_;
    cell_style blank_style_{default_cell_style};
    style_table styles_;
    cell_style default_style_id_{};
    hyperlink_table hyperlinks_;
    image_table images_;
    grapheme_store grapheme_scratch_;
};

inline page::page(std::size_t width, std::size_t height, cell_style fill_style) : width_{width}, height_{height}
{
    assert(width != 0 && height != 0);
    for (std::size_t y = 0; y < height_; ++y)
    {
        rows_.push_back(row_block{cell_vector(width_, cell::blank(fill_style)), row_state{}, {}});
    }
    mark_all_dirty();
}

inline void page::mark_dirty(std::size_t y, std::size_t begin, std::size_t end) noexcept
{
    assert(y < rows_.size());
    if (begin >= end || full_damage_)
    {
        return;
    }

    begin = std::min(begin, width_);
    end = std::min(end, width_);
    if (begin >= end)
    {
        return;
    }

    auto &state = rows_[y].state;
    if (!state.is_dirty())
    {
        state.dirty_begin = static_cast<std::uint32_t>(begin);
        state.dirty_end = static_cast<std::uint32_t>(end);
        damage_active_ = true;
        return;
    }

    state.dirty_begin = std::min(state.dirty_begin, static_cast<std::uint32_t>(begin));
    state.dirty_end = std::max(state.dirty_end, static_cast<std::uint32_t>(end));
    damage_active_ = true;
}

inline void page::mark_used(std::size_t y, std::size_t end) noexcept
{
    assert(y < rows_.size());
    rows_[y].state.used_columns =
        std::max(rows_[y].state.used_columns, static_cast<std::uint32_t>(std::min(end, width_)));
}

inline void page::clear_damage() noexcept
{
    for (auto &row : rows_)
    {
        row.state.dirty_begin = 0;
        row.state.dirty_end = 0;
    }
    damage_active_ = false;
    full_damage_ = false;
}

inline void page::mark_all_dirty() noexcept
{
    damage_active_ = true;
    full_damage_ = true;
}

inline screen_buffer::screen_buffer(std::size_t width, std::size_t height, std::size_t history_limit)
    : width_{width}, height_{height}, history_limit_{history_limit}, main_(width, height),
      view_(coord{static_cast<std::int32_t>(width), static_cast<std::int32_t>(height)}), alternate_(width, height)
{
    assert(width != 0 && height != 0);
    default_style_id_ = styles_.intern(text_style{.legacy_attributes = 0x0007});
}

inline const cell &screen_buffer::at(std::size_t x, std::size_t y) const noexcept
{
    return active_page().at(x, y);
}

inline const row_block &screen_buffer::block(std::size_t y) const noexcept
{
    return active_page().block(y);
}

inline std::optional<cell> screen_buffer::try_at(coord position) const noexcept
{
    if (position.x < 0 || position.y < 0 || static_cast<std::size_t>(position.x) >= width_ ||
        static_cast<std::size_t>(position.y) >= height_)
    {
        return std::nullopt;
    }
    return at(static_cast<std::size_t>(position.x), static_cast<std::size_t>(position.y));
}

inline char_info screen_buffer::char_info_at(coord position) const noexcept
{
    const auto value = try_at(position);
    if (!value)
    {
        return char_info{};
    }
    return project_cell(*value, styles_);
}

inline std::span<cell> screen_buffer::row_cells(std::size_t y) noexcept
{
    return active_page().row_cells(y);
}

inline std::span<const cell> screen_buffer::row_cells(std::size_t y) const noexcept
{
    return active_page().row_cells(y);
}

inline row_state &screen_buffer::row(std::size_t y) noexcept
{
    return active_page().row(y);
}

inline const row_state &screen_buffer::row(std::size_t y) const noexcept
{
    return active_page().row(y);
}

inline line_flags screen_buffer::flags(std::size_t y) const noexcept
{
    return active_page().row(y).flags;
}

inline void screen_buffer::set_flags(std::size_t y, line_flags value) noexcept
{
    active_page().row(y).flags = value;
}

inline void screen_buffer::add_flags(std::size_t y, line_flag value) noexcept
{
    auto &target = active_page().row(y);
    target.flags = line_flag_ops::combine(target.flags, line_flag_ops::raw(value));
}

inline void screen_buffer::remove_flags(std::size_t y, line_flag value) noexcept
{
    auto &target = active_page().row(y);
    target.flags = line_flag_ops::without(target.flags, value);
}

inline bool screen_buffer::has_flag(std::size_t y, line_flag value) const noexcept
{
    return line_flag_ops::has(active_page().row(y).flags, value);
}

inline bool screen_buffer::is_double_width(std::size_t y) const noexcept
{
    return has_flag(y, line_flag::double_width);
}

inline bool screen_buffer::is_double_height_top(std::size_t y) const noexcept
{
    return has_flag(y, line_flag::double_height_top);
}

inline bool screen_buffer::is_double_height_bottom(std::size_t y) const noexcept
{
    return has_flag(y, line_flag::double_height_bottom);
}

inline void screen_buffer::set_command_exit_code(std::size_t y, std::int32_t code) noexcept
{
    active_page().row(y).command_exit_code = code;
}

inline void screen_buffer::clear_command_exit_code(std::size_t y) noexcept
{
    active_page().row(y).command_exit_code = no_command_exit_code;
}

inline std::optional<std::int32_t> screen_buffer::command_exit_code(std::size_t y) const noexcept
{
    const auto code = active_page().row(y).command_exit_code;
    return code == no_command_exit_code ? std::nullopt : std::optional<std::int32_t>{code};
}

inline cell_style screen_buffer::style_for_legacy(std::uint16_t attributes)
{
    if (attributes == 0x0007)
    {
        return default_style_id_;
    }

    auto value = text_style{};
    value.legacy_attributes = attributes;
    if ((attributes & 0x0008) != 0)
    {
        value.attributes |= text_style_attribute_bit(text_style_attribute::bold);
    }
    if ((attributes & 0x8000) != 0)
    {
        value.attributes |= text_style_attribute_bit(text_style_attribute::underline);
    }
    if ((attributes & 0x4000) != 0)
    {
        value.attributes |= text_style_attribute_bit(text_style_attribute::negative);
    }
    const auto to_indexed = [](std::uint16_t bgri) noexcept {
        return static_cast<std::uint32_t>((bgri & 0x04 ? 1u : 0u) | (bgri & 0x02 ? 2u : 0u) | (bgri & 0x01 ? 4u : 0u) |
                                          (bgri & 0x08 ? 8u : 0u));
    };
    value.foreground = color{color_kind::indexed, to_indexed(attributes & 0x0F)};
    value.background =
        ((attributes >> 4) & 0x0F) == 0 ? color{} : color{color_kind::indexed, to_indexed((attributes >> 4) & 0x0F)};
    return styles_.intern(value);
}

inline void screen_buffer::clear_wide_neighborhood(page &target, std::size_t x, std::size_t y)
{
    const auto old = target.at(x, y);
    if (old.is_continuation() && x > 0)
    {
        const auto &head = target.at(x - 1, y);
        target.at(x - 1, y) = cell::blank(head.style(), head.hyperlink());
        target.mark_dirty(y, x - 1, x);
    }
    else if (old.is_wide() && x + 1 < width_ && target.at(x + 1, y).is_continuation())
    {
        target.at(x + 1, y) = cell::blank(old.style(), old.hyperlink());
        target.mark_dirty(y, x + 1, x + 2);
    }
}

inline bool screen_buffer::write_cell(std::size_t x, std::size_t y, cell value)
{
    if (x >= width_ || y >= height_)
    {
        return false;
    }

    const bool wide = value.is_wide();
    if (wide && x + 1 >= width_)
    {
        return false;
    }

    auto &target = active_page();
    clear_wide_neighborhood(target, x, y);
    if (wide)
    {
        clear_wide_neighborhood(target, x + 1, y);
    }

    target.at(x, y) = value;
    target.mark_dirty(y, x, x + 1);
    target.mark_used(y, x + 1);

    if (wide)
    {
        target.at(x + 1, y) = cell::tail(value.style(), value.hyperlink());
        target.mark_dirty(y, x + 1, x + 2);
        target.mark_used(y, x + 2);
    }

    return true;
}

inline bool screen_buffer::write_scalar(std::size_t x, std::size_t y, char16_t primary, cell_style style,
                                        cell_width width, cell_hyperlink hyperlink, cell_flags flags)
{
    if (width == cell_width::tail)
    {
        return write_cell(x, y, cell::tail(style, hyperlink));
    }

    return write_cell(x, y, cell::scalar(primary, style, width, hyperlink, flags));
}

inline std::size_t screen_buffer::write_ascii(std::size_t x, std::size_t y, std::u16string_view text, cell_style style,
                                              cell_hyperlink hyperlink)
{
    if (x >= width_ || y >= height_ || text.empty())
    {
        return 0;
    }

    const auto count = std::min(text.size(), width_ - x);
    const auto start = x;
    const auto prototype = cell::scalar(u'\0', style, cell_width::narrow, hyperlink);
    auto &target = active_page();
    auto cells = target.row_cells(y);

    bool all_narrow = true;
    for (std::size_t i = 0; i < count; ++i)
    {
        if (cells[x + i].width_ != cell_width::narrow)
        {
            all_narrow = false;
            break;
        }
    }
    if (all_narrow)
    {
        auto value = prototype;
        for (std::size_t i = 0; i < count; ++i)
        {
            value.primary_ = text[i];
            cells[x + i] = value;
        }
        x += count;
        target.mark_dirty(y, start, x);
        target.mark_used(y, x);
        return count;
    }

    for (std::size_t i = 0; i < count; ++i)
    {
        const auto old = cells[x];
        if (old.width_ != cell_width::narrow)
        {
            if (old.is_continuation() && x > 0)
            {
                const auto &head = cells[x - 1];
                cells[x - 1] = cell::blank(head.style(), head.hyperlink());
                target.mark_dirty(y, x - 1, x);
            }
            else if (old.is_wide() && x + 1 < width_ && cells[x + 1].is_continuation())
            {
                cells[x + 1] = cell::blank(old.style(), old.hyperlink());
                target.mark_dirty(y, x + 1, x + 2);
            }
        }
        auto value = prototype;
        value.primary_ = text[i];
        cells[x] = value;
        ++x;
    }
    target.mark_dirty(y, start, x);
    target.mark_used(y, x);
    return count;
}

inline bool screen_buffer::write_cluster(std::size_t x, std::size_t y, std::span<const char16_t> cluster,
                                         cell_style style, cell_width width, cell_hyperlink hyperlink, cell_flags flags)
{
    if (x >= width_ || y >= height_ || cluster.empty() || !active_page().graphemes(y).can_append(cluster.size()))
    {
        return false;
    }

    if (width == cell_width::wide && x + 1 >= width_)
    {
        return false;
    }

    const auto ref = active_page().graphemes(y).append(cluster);
    return write_cell(x, y, cell::complex(ref.offset, ref.length, style, width, hyperlink, flags));
}

inline bool screen_buffer::write_codepoint(std::size_t x, std::size_t y, char32_t codepoint, cell_style style,
                                           cell_width width, cell_hyperlink hyperlink, cell_flags flags)
{
    if (codepoint > 0x10FFFFu || (codepoint >= 0xD800u && codepoint <= 0xDFFFu))
    {
        codepoint = U'\xFFFD';
    }

    if (codepoint <= 0xFFFFu)
    {
        return write_scalar(x, y, static_cast<char16_t>(codepoint), style, width, hyperlink, flags);
    }

    char16_t cluster[2]{};
    const auto cluster_end = to_u16(codepoint, cluster);
    const auto cluster_size = static_cast<std::size_t>(cluster_end - cluster);
    return write_cluster(x, y, std::span<const char16_t>{cluster, cluster_size}, style, width, hyperlink, flags);
}

template <typename String>
inline void screen_buffer::glyph_at_into(std::size_t x, std::size_t y, String &result) const
{
    result.clear();
    while (x > 0 && at(x, y).is_continuation())
        --x;

    const auto &value = at(x, y);
    if (value.has_cluster())
    {
        const auto cluster = cluster_of(y, value);
        result.append(cluster.begin(), cluster.end());
    }
    else if (value.is_blank())
    {
        result.push_back(u' ');
    }
    else
    {
        result.push_back(value.primary());
    }
}

inline char32_t screen_buffer::codepoint_at(std::size_t x, std::size_t y) const
{
    while (x > 0 && at(x, y).is_continuation())
        --x;

    const auto &value = at(x, y);
    if (value.has_cluster())
    {
        const auto cluster = cluster_of(y, value);
        return next_codepoint(std::u16string_view{cluster.data(), cluster.size()}, 0).value;
    }
    if (value.is_blank())
        return U' ';
    return value.primary();
}

inline int screen_buffer::glyph_width(std::size_t x, std::size_t y) const
{
    const auto &value = at(x, y);
    if (value.is_continuation())
    {
        return 0;
    }
    return value.is_wide() ? 2 : 1;
}

inline bool screen_buffer::is_trailing(std::size_t x, std::size_t y) const
{
    return at(x, y).is_continuation();
}

inline std::size_t screen_buffer::used_columns(std::size_t y) const
{
    return active_page().row(y).used_columns;
}

template <typename String>
inline void screen_buffer::line_text_into(std::size_t y, String &result) const
{
    result.clear();
    result.reserve(width_);
    for (std::size_t x = 0; x < width_; ++x)
    {
        const auto &value = at(x, y);
        if (value.is_continuation())
        {
            continue;
        }
        if (value.has_cluster())
        {
            const auto cluster = cluster_of(y, value);
            result.append(cluster.begin(), cluster.end());
        }
        else if (value.is_blank())
        {
            result.push_back(u' ');
        }
        else
        {
            result.push_back(value.primary());
        }
    }
}

inline legacy_cell screen_buffer::legacy_at(std::size_t x, std::size_t y) const
{
    const auto value = project_cell(at(x, y), styles_);
    return legacy_cell{value.character, value.attributes};
}

inline bool screen_buffer::write_legacy_scalar(std::size_t x, std::size_t y, char16_t primary, std::uint16_t attributes,
                                               cell_width width)
{
    auto value = text_style{};
    value.legacy_attributes = attributes;
    return write_scalar(x, y, primary, styles_.intern(value), width);
}

inline bool screen_buffer::write_image(std::size_t x, std::size_t y, std::uint32_t fragment, cell_style style,
                                       cell_width width, cell_hyperlink hyperlink, cell_flags flags)
{
    if (fragment == 0 || fragment >= images_.size())
    {
        return false;
    }

    return write_cell(x, y, cell::image(fragment, style, width, hyperlink, flags));
}

inline const image_fragment *screen_buffer::image_of(const cell &value) const noexcept
{
    if (!value.has_image())
    {
        return nullptr;
    }

    const auto id = value.image_fragment_id();
    if (id == 0 || id >= images_.size())
    {
        return nullptr;
    }
    return &images_.at(id);
}

inline void screen_buffer::fill_rect(rectangle area, cell value)
{
    const auto x_end = std::min(width_, area.x + area.width);
    const auto y_end = std::min(height_, area.y + area.height);
    if (area.x >= x_end || area.y >= y_end)
    {
        return;
    }

    auto &target = active_page();
    for (auto y = area.y; y < y_end; ++y)
    {
        for (auto x = area.x; x < x_end; ++x)
        {
            target.at(x, y) = value;
        }
        target.mark_dirty(y, area.x, x_end);
        target.mark_used(y, x_end);
    }
}

inline void screen_buffer::clear(cell_style style)
{
    fill_rect(rectangle{0, 0, width_, height_}, cell::blank(style));
}

inline std::size_t screen_buffer::read_char_info(rect area, std::span<char_info> out) const noexcept
{
    const auto left = std::max(area.left, 0);
    const auto top = std::max(area.top, 0);
    const auto right = std::min(area.right, static_cast<std::int32_t>(width_) - 1);
    const auto bottom = std::min(area.bottom, static_cast<std::int32_t>(height_) - 1);

    const auto out_size = out.size();
    std::size_t count = 0;
    for (auto y = top; y <= bottom && count < out_size; ++y)
    {
        for (auto x = left; x <= right && count < out_size; ++x)
        {
            out[count++] = char_info_at(coord{x, y});
        }
    }
    return count;
}

inline std::size_t screen_buffer::write_char_info(rect area, std::span<const char_info> values) noexcept
{
    const auto values_size = values.size();
    std::size_t index = 0;
    for (auto y = area.top; y <= area.bottom && index < values_size; ++y)
    {
        for (auto x = area.left; x <= area.right && index < values_size; ++x)
        {
            if (x < 0 || y < 0 || static_cast<std::size_t>(x) >= width_ || static_cast<std::size_t>(y) >= height_)
            {
                ++index;
                continue;
            }

            auto value = values[index++];
            const auto is_tail = (value.attributes & interop::win32::lvb_trailing_byte) != 0;
            const auto is_head = (value.attributes & interop::win32::lvb_leading_byte) != 0;
            value.attributes = static_cast<std::uint16_t>(
                value.attributes & ~(interop::win32::lvb_leading_byte | interop::win32::lvb_trailing_byte));
            const auto style = style_for_legacy(value.attributes);

            if (is_tail)
            {
                (void)write_cell(static_cast<std::size_t>(x), static_cast<std::size_t>(y), cell::tail(style));
                continue;
            }

            const auto next_is_tail =
                index < values.size() && (values[index].attributes & interop::win32::lvb_trailing_byte) != 0;
            const auto wide = is_head || next_is_tail;
            (void)write_cell(static_cast<std::size_t>(x), static_cast<std::size_t>(y),
                             cell::scalar(value.character, style, wide ? cell_width::wide : cell_width::narrow));
            if (wide)
            {
                if (index < values.size())
                {
                    ++index;
                }
                ++x;
            }
        }
    }
    return index;
}

inline std::size_t screen_buffer::read_char_info_abi(rect area, std::span<interop::win32::char_info> out) const noexcept
{
    const auto left = std::max(area.left, 0);
    const auto top = std::max(area.top, 0);
    const auto right = std::min(area.right, static_cast<std::int32_t>(width_) - 1);
    const auto bottom = std::min(area.bottom, static_cast<std::int32_t>(height_) - 1);

    const auto out_size = out.size();
    std::size_t count = 0;
    for (auto y = top; y <= bottom && count < out_size; ++y)
    {
        for (auto x = left; x <= right && count < out_size; ++x)
        {
            out[count++] = to_win32(char_info_at(coord{x, y}));
        }
    }
    return count;
}

inline std::size_t screen_buffer::write_char_info_abi(rect area,
                                                      std::span<const interop::win32::char_info> values) noexcept
{
    const auto values_size = values.size();
    std::size_t index = 0;
    for (auto y = area.top; y <= area.bottom && index < values_size; ++y)
    {
        for (auto x = area.left; x <= area.right && index < values_size; ++x)
        {
            if (x < 0 || y < 0 || static_cast<std::size_t>(x) >= width_ || static_cast<std::size_t>(y) >= height_)
            {
                ++index;
                continue;
            }

            auto value = from_win32(values[index++]);
            const auto is_tail = (value.attributes & interop::win32::lvb_trailing_byte) != 0;
            const auto is_head = (value.attributes & interop::win32::lvb_leading_byte) != 0;
            value.attributes = static_cast<std::uint16_t>(
                value.attributes & ~(interop::win32::lvb_leading_byte | interop::win32::lvb_trailing_byte));
            const auto style = style_for_legacy(value.attributes);

            if (is_tail)
            {
                (void)write_cell(static_cast<std::size_t>(x), static_cast<std::size_t>(y), cell::tail(style));
                continue;
            }

            const auto next_is_tail =
                index < values.size() && (values[index].attributes & interop::win32::lvb_trailing_byte) != 0;
            const auto wide = is_head || next_is_tail;
            (void)write_cell(static_cast<std::size_t>(x), static_cast<std::size_t>(y),
                             cell::scalar(value.character, style, wide ? cell_width::wide : cell_width::narrow));
            if (wide)
            {
                if (index < values.size())
                {
                    ++index;
                }
                ++x;
            }
        }
    }
    return index;
}

inline std::size_t screen_buffer::read_chars(coord start, std::span<char16_t> out) const noexcept
{
    if (start.x < 0 || start.y < 0 || static_cast<std::size_t>(start.x) >= width_ ||
        static_cast<std::size_t>(start.y) >= height_)
    {
        return 0;
    }

    const auto out_size = out.size();
    std::size_t count = 0;
    for (auto y = start.y; y < static_cast<std::int32_t>(height_) && count < out_size; ++y)
    {
        auto x = y == start.y ? start.x : 0;
        while (x < static_cast<std::int32_t>(width_) && count < out_size)
        {
            const auto &value = at(static_cast<std::size_t>(x), static_cast<std::size_t>(y));
            if (value.is_continuation() || !value.has_cluster())
            {
                out[count++] = value.is_continuation() || value.has_image() ? u' ' : value.text_unit();
            }
            else
            {
                const auto cluster = cluster_of(y, value);
                out[count++] =
                    cluster.empty() || (cluster[0] >= 0xD800 && cluster[0] <= 0xDBFF) ? u'\uFFFD' : cluster[0];
            }
            ++x;
        }
    }
    return count;
}
inline std::size_t screen_buffer::read_attrs(coord start, std::span<std::uint16_t> out) const noexcept
{
    auto x = start.x;
    auto y = start.y;
    if (x < 0 || y < 0 || static_cast<std::size_t>(x) >= width_ || static_cast<std::size_t>(y) >= height_)
    {
        return 0;
    }

    const auto out_size = out.size();
    std::size_t count = 0;
    while (count < out_size && static_cast<std::size_t>(y) < height_)
    {
        out[count++] = char_info_at(coord{x, y}).attributes;
        if (++x >= static_cast<std::int32_t>(width_))
        {
            x = 0;
            ++y;
        }
    }
    return count;
}

inline std::size_t screen_buffer::write_chars(coord start, std::u16string_view text) noexcept
{
    auto x = start.x;
    auto y = start.y;
    if (x < 0 || y < 0 || static_cast<std::size_t>(x) >= width_ || static_cast<std::size_t>(y) >= height_)
    {
        return 0;
    }

    const auto text_size = text.size();
    std::size_t count = 0;
    while (count < text_size && static_cast<std::size_t>(y) < height_)
    {
        const auto decoded = next_codepoint(text, count);
        auto columns = console_width(decoded.value);
        if (columns <= 0)
        {
            columns = 1;
        }
        if (x + columns > static_cast<std::int32_t>(width_))
        {
            break;
        }

        const auto &old = at(static_cast<std::size_t>(x), static_cast<std::size_t>(y));
        if (!write_codepoint(static_cast<std::size_t>(x), static_cast<std::size_t>(y), decoded.value, old.style(),
                             columns >= 2 ? cell_width::wide : cell_width::narrow, old.hyperlink(), old.flags()))
        {
            break;
        }
        count += decoded.units;
        x += columns;
        if (x >= static_cast<std::int32_t>(width_))
        {
            x = 0;
            ++y;
        }
    }
    return count;
}

inline std::size_t screen_buffer::write_attrs(coord start, std::span<const std::uint16_t> attrs) noexcept
{
    auto x = start.x;
    auto y = start.y;
    if (x < 0 || y < 0 || static_cast<std::size_t>(x) >= width_ || static_cast<std::size_t>(y) >= height_)
    {
        return 0;
    }

    std::size_t count = 0;
    for (const auto attributes : attrs)
    {
        if (static_cast<std::size_t>(y) >= height_)
        {
            break;
        }
        (void)set_attr(coord{x, y}, attributes);
        ++count;
        if (++x >= static_cast<std::int32_t>(width_))
        {
            x = 0;
            ++y;
        }
    }
    return count;
}

inline void screen_buffer::fill_chars(rect area, char16_t character) noexcept
{
    const auto left = std::max(area.left, 0);
    const auto top = std::max(area.top, 0);
    const auto right = std::min(area.right, static_cast<std::int32_t>(width_) - 1);
    const auto bottom = std::min(area.bottom, static_cast<std::int32_t>(height_) - 1);

    for (auto y = top; y <= bottom; ++y)
    {
        for (auto x = left; x <= right; ++x)
        {
            const auto &old = at(static_cast<std::size_t>(x), static_cast<std::size_t>(y));
            (void)write_cell(static_cast<std::size_t>(x), static_cast<std::size_t>(y),
                             cell::scalar(character, old.style(), cell_width::narrow, old.hyperlink(), old.flags()));
        }
    }
}

inline void screen_buffer::fill_attrs(rect area, std::uint16_t attributes) noexcept
{
    const auto left = std::max(area.left, 0);
    const auto top = std::max(area.top, 0);
    const auto right = std::min(area.right, static_cast<std::int32_t>(width_) - 1);
    const auto bottom = std::min(area.bottom, static_cast<std::int32_t>(height_) - 1);
    const auto style = style_for_legacy(attributes);
    auto &target = active_page();
    for (auto y = top; y <= bottom; ++y)
    {
        for (auto x = left; x <= right; ++x)
        {
            target.at(static_cast<std::size_t>(x), static_cast<std::size_t>(y)) =
                target.at(static_cast<std::size_t>(x), static_cast<std::size_t>(y)).with_style(style);
            target.mark_dirty(static_cast<std::size_t>(y), static_cast<std::size_t>(x),
                              static_cast<std::size_t>(x + 1));
        }
    }
}

inline cell_style screen_buffer::style_for_attributes(std::uint16_t attributes)
{
    return style_for_legacy(attributes);
}

inline bool screen_buffer::set_attr(coord position, std::uint16_t attributes) noexcept
{
    if (position.x < 0 || position.y < 0 || static_cast<std::size_t>(position.x) >= width_ ||
        static_cast<std::size_t>(position.y) >= height_)
    {
        return false;
    }
    auto &target = active_page().at(static_cast<std::size_t>(position.x), static_cast<std::size_t>(position.y));
    target = target.with_style(style_for_legacy(attributes));
    active_page().mark_dirty(static_cast<std::size_t>(position.y), static_cast<std::size_t>(position.x),
                             static_cast<std::size_t>(position.x + 1));
    return true;
}

inline std::uint16_t screen_buffer::attr_at(coord position) const noexcept
{
    return char_info_at(position).attributes;
}

template <text_measurement Mode, bool AmbiguousIsWide>
inline screen_buffer::text_write_result screen_buffer::write_text_row(coord &cursor, std::u16string_view text,
                                                                      std::span<const std::uint8_t> widths,
                                                                      cell_style style, bool wrap_at_eol,
                                                                      cell_hyperlink hyperlink) noexcept
{
    text_write_result result;
    if (text.empty())
    {
        return result;
    }

    if constexpr (Mode == text_measurement::graphemes || Mode == text_measurement::console)
    {
        for (const auto cluster : grapheme_range<Mode, AmbiguousIsWide>{text})
        {
            if (cursor.x < 0 || cursor.y < 0 || static_cast<std::size_t>(cursor.y) >= height_)
            {
                result.row_end = true;
                break;
            }
            if (static_cast<std::size_t>(cursor.x) >= width_)
            {
                if (!wrap_at_eol)
                {
                    result.row_end = true;
                    break;
                }
                cursor.x = 0;
                ++cursor.y;
                if (static_cast<std::size_t>(cursor.y) >= height_)
                {
                    result.row_end = true;
                    break;
                }
                add_flags(static_cast<std::size_t>(cursor.y), line_flag::wrapped);
            }

            auto columns = cluster.width;
            if (!widths.empty() && result.consumed < widths.size())
            {
                columns = static_cast<int>(widths[result.consumed]);
            }
            if (columns <= 0)
            {
                columns = 1;
            }
            if (cursor.x + columns > static_cast<std::int32_t>(width_))
            {
                if (!wrap_at_eol)
                {
                    result.row_end = true;
                    break;
                }
                cursor.x = 0;
                ++cursor.y;
                if (static_cast<std::size_t>(cursor.y) >= height_)
                {
                    result.row_end = true;
                    break;
                }
                add_flags(static_cast<std::size_t>(cursor.y), line_flag::wrapped);
            }

            const std::u16string_view cluster_text{cluster.text.data(), cluster.text.size()};
            if (!write_glyph(cursor, cluster_text, columns, style, hyperlink))
            {
                result.row_end = true;
                break;
            }
            cursor.x += columns;
            result.consumed += cluster.text.size();
        }
        if (cursor.x >= static_cast<std::int32_t>(width_))
        {
            result.row_end = true;
        }
        return result;
    }

    const auto text_size = text.size();
    std::size_t offset = 0;
    while (offset < text_size)
    {
        if (cursor.x < 0 || cursor.y < 0 || static_cast<std::size_t>(cursor.y) >= height_)
        {
            result.row_end = true;
            break;
        }
        if (static_cast<std::size_t>(cursor.x) >= width_)
        {
            if (!wrap_at_eol)
            {
                result.row_end = true;
                break;
            }
            cursor.x = 0;
            ++cursor.y;
            if (static_cast<std::size_t>(cursor.y) >= height_)
            {
                result.row_end = true;
                break;
            }
            add_flags(static_cast<std::size_t>(cursor.y), line_flag::wrapped);
        }

        const auto decoded = next_codepoint(text, offset);
        auto columns = widths.empty() ? codepoint_width<Mode, AmbiguousIsWide>(decoded.value)
                                      : (offset < widths.size() ? static_cast<int>(widths[offset]) : 1);
        if (columns <= 0)
        {
            columns = 1;
        }
        if (cursor.x + columns > static_cast<std::int32_t>(width_))
        {
            result.row_end = true;
            if (!wrap_at_eol)
            {
                break;
            }
            cursor.x = 0;
            ++cursor.y;
            if (static_cast<std::size_t>(cursor.y) < height_)
            {
                add_flags(static_cast<std::size_t>(cursor.y), line_flag::wrapped);
            }
            continue;
        }

        const auto width = columns >= 2 ? cell_width::wide : cell_width::narrow;
        if (!write_codepoint(static_cast<std::size_t>(cursor.x), static_cast<std::size_t>(cursor.y), decoded.value,
                             style, width, hyperlink))
        {
            result.row_end = true;
            break;
        }
        cursor.x += columns;
        offset += decoded.units;
        result.consumed = offset;
    }
    if (cursor.x >= static_cast<std::int32_t>(width_))
    {
        result.row_end = true;
    }
    return result;
}

inline screen_buffer::text_write_result screen_buffer::write_console_text_row(coord &cursor, std::u16string_view text,
                                                                              std::span<const std::uint8_t> widths,
                                                                              cell_style style, bool wrap_at_eol,
                                                                              cell_hyperlink hyperlink) noexcept
{
    return write_text_row<text_measurement::console, false>(cursor, text, widths, style, wrap_at_eol, hyperlink);
}

inline screen_buffer::text_write_result screen_buffer::write_grapheme_text_row(coord &cursor, std::u16string_view text,
                                                                               std::span<const std::uint8_t> widths,
                                                                               bool ambiguous_is_wide, cell_style style,
                                                                               bool wrap_at_eol,
                                                                               cell_hyperlink hyperlink) noexcept
{
    return ambiguous_is_wide
               ? write_text_row<text_measurement::graphemes, true>(cursor, text, widths, style, wrap_at_eol, hyperlink)
               : write_text_row<text_measurement::graphemes, false>(cursor, text, widths, style, wrap_at_eol,
                                                                    hyperlink);
}

inline screen_buffer::text_write_result screen_buffer::write_wcswidth_text_row(coord &cursor, std::u16string_view text,
                                                                               std::span<const std::uint8_t> widths,
                                                                               bool ambiguous_is_wide, cell_style style,
                                                                               bool wrap_at_eol,
                                                                               cell_hyperlink hyperlink) noexcept
{
    return ambiguous_is_wide
               ? write_text_row<text_measurement::wcswidth, true>(cursor, text, widths, style, wrap_at_eol, hyperlink)
               : write_text_row<text_measurement::wcswidth, false>(cursor, text, widths, style, wrap_at_eol, hyperlink);
}

inline screen_buffer::text_write_result screen_buffer::write_text_row_for(
    text_measurement measurement, bool ambiguous_is_wide, coord &cursor, std::u16string_view text,
    std::span<const std::uint8_t> widths, cell_style style, bool wrap_at_eol, cell_hyperlink hyperlink) noexcept
{
    switch (measurement)
    {
    case text_measurement::console:
        return write_console_text_row(cursor, text, widths, style, wrap_at_eol, hyperlink);
    case text_measurement::graphemes:
        return write_grapheme_text_row(cursor, text, widths, ambiguous_is_wide, style, wrap_at_eol, hyperlink);
    case text_measurement::wcswidth:
        return write_wcswidth_text_row(cursor, text, widths, ambiguous_is_wide, style, wrap_at_eol, hyperlink);
    }
    return {};
}

inline bool screen_buffer::write_glyph(coord position, std::u16string_view glyph, int columns, cell_style style,
                                       cell_hyperlink hyperlink) noexcept
{
    if (glyph.empty() || position.x < 0 || position.y < 0 || static_cast<std::size_t>(position.x) >= width_ ||
        static_cast<std::size_t>(position.y) >= height_)
    {
        return false;
    }

    columns = std::max(columns, 1);
    const auto width = columns >= 2 ? cell_width::wide : cell_width::narrow;
    if (glyph.size() == 1)
    {
        return write_scalar(static_cast<std::size_t>(position.x), static_cast<std::size_t>(position.y), glyph[0], style,
                            width, hyperlink);
    }
    return write_cluster(static_cast<std::size_t>(position.x), static_cast<std::size_t>(position.y),
                         std::span<const char16_t>{glyph.data(), glyph.size()}, style, width, hyperlink);
}

inline void screen_buffer::copy_rect(rectangle source, rectangle clip, bool use_clip, coord destination) noexcept
{
    const auto left = std::max<std::int32_t>(static_cast<std::int32_t>(source.x), 0);
    const auto top = std::max<std::int32_t>(static_cast<std::int32_t>(source.y), 0);
    const auto right =
        std::min<std::int32_t>(static_cast<std::int32_t>(source.x + source.width), static_cast<std::int32_t>(width_));
    const auto bottom =
        std::min<std::int32_t>(static_cast<std::int32_t>(source.y + source.height), static_cast<std::int32_t>(height_));
    if (left >= right || top >= bottom)
    {
        return;
    }

    auto copy_left = left;
    auto copy_top = top;
    auto copy_right = right;
    auto copy_bottom = bottom;
    if (use_clip)
    {
        copy_left = std::max<std::int32_t>(copy_left, static_cast<std::int32_t>(clip.x));
        copy_top = std::max<std::int32_t>(copy_top, static_cast<std::int32_t>(clip.y));
        copy_right = std::min<std::int32_t>(copy_right, static_cast<std::int32_t>(clip.x + clip.width));
        copy_bottom = std::min<std::int32_t>(copy_bottom, static_cast<std::int32_t>(clip.y + clip.height));
    }
    if (copy_left >= copy_right || copy_top >= copy_bottom)
    {
        return;
    }

    const auto destination_x = destination.x + (copy_left - left);
    const auto destination_y = destination.y + (copy_top - top);
    copy_rect_rows(copy_left, copy_top, copy_right, copy_bottom, destination_x, destination_y,
                   destination_y > copy_top);
}
inline void screen_buffer::copy_rect_rows(std::int32_t copy_left, std::int32_t copy_top, std::int32_t copy_right,
                                          std::int32_t copy_bottom, std::int32_t destination_x,
                                          std::int32_t destination_y, bool descending) noexcept
{
    auto &target = active_page();
    for (auto row = 0; row < (copy_bottom - copy_top); ++row)
    {
        const auto source_y = descending ? copy_bottom - 1 - row : copy_top + row;
        const auto dest_y = destination_y + (source_y - copy_top);
        if (dest_y < 0 || dest_y >= static_cast<std::int32_t>(height_))
        {
            continue;
        }

        const auto source_begin = std::max(copy_left, copy_left - destination_x);
        const auto source_end = std::min(copy_right, copy_left + static_cast<std::int32_t>(width_) - destination_x);
        if (source_begin >= source_end)
        {
            continue;
        }

        const auto dest_x = static_cast<std::size_t>(destination_x + (source_begin - copy_left));
        const auto source_offset = static_cast<std::size_t>(source_begin);
        const auto count = static_cast<std::size_t>(source_end - source_begin);
        auto source_row = row_cells(static_cast<std::size_t>(source_y));
        auto dest_row = row_cells(static_cast<std::size_t>(dest_y));

        if (source_y == dest_y)
        {
            if (dest_x > source_offset)
            {
                std::copy_backward(source_row.begin() + static_cast<std::ptrdiff_t>(source_offset),
                                   source_row.begin() + static_cast<std::ptrdiff_t>(source_offset + count),
                                   dest_row.begin() + static_cast<std::ptrdiff_t>(dest_x + count));
            }
            else
            {
                std::copy_n(source_row.begin() + static_cast<std::ptrdiff_t>(source_offset), count,
                            dest_row.begin() + static_cast<std::ptrdiff_t>(dest_x));
            }
        }
        else
        {
            const auto &source_store = target.graphemes(static_cast<std::size_t>(source_y));
            auto &dest_store = target.graphemes(static_cast<std::size_t>(dest_y));
            for (std::size_t index = 0; index < count; ++index)
            {
                auto value = source_row[source_offset + index];
                if (value.has_cluster())
                {
                    const auto source = source_store.view(grapheme_store::ref{value.offset(), value.length()});
                    if (!source.empty() && dest_store.can_append(source.size()))
                    {
                        const auto ref = dest_store.append(source);
                        value = value.with_cluster_ref(ref.offset, ref.length);
                    }
                    else
                    {
                        value = cell::blank(value.style(), value.hyperlink());
                    }
                }
                dest_row[dest_x + index] = value;
            }
        }

        target.mark_dirty(static_cast<std::size_t>(dest_y), dest_x, dest_x + count);
    }
}
inline void screen_buffer::fill_scroll_segment(std::int32_t x, std::int32_t y, std::int32_t width, char16_t fill_char,
                                               std::uint16_t fill_attr) noexcept
{
    if (width <= 0)
    {
        return;
    }
    const rect segment{x, y, x + width - 1, y};
    fill_chars(segment, fill_char);
    fill_attrs(segment, fill_attr);
}
inline void screen_buffer::scroll_rect(rectangle source, rectangle clip, bool use_clip, coord destination,
                                       char16_t fill_char, std::uint16_t fill_attr) noexcept
{
    const auto left = std::max<std::int32_t>(static_cast<std::int32_t>(source.x), 0);
    const auto top = std::max<std::int32_t>(static_cast<std::int32_t>(source.y), 0);
    const auto right =
        std::min<std::int32_t>(static_cast<std::int32_t>(source.x + source.width), static_cast<std::int32_t>(width_));
    const auto bottom =
        std::min<std::int32_t>(static_cast<std::int32_t>(source.y + source.height), static_cast<std::int32_t>(height_));
    if (left >= right || top >= bottom)
    {
        return;
    }

    auto copy_left = left;
    auto copy_top = top;
    auto copy_right = right;
    auto copy_bottom = bottom;
    if (use_clip)
    {
        copy_left = std::max<std::int32_t>(copy_left, static_cast<std::int32_t>(clip.x));
        copy_top = std::max<std::int32_t>(copy_top, static_cast<std::int32_t>(clip.y));
        copy_right = std::min<std::int32_t>(copy_right, static_cast<std::int32_t>(clip.x + clip.width));
        copy_bottom = std::min<std::int32_t>(copy_bottom, static_cast<std::int32_t>(clip.y + clip.height));
    }
    if (copy_left >= copy_right || copy_top >= copy_bottom)
    {
        return;
    }

    copy_rect(source, clip, use_clip, destination);

    const auto vertical = copy_bottom - copy_top;
    const auto horizontal = copy_right - copy_left;
    const auto dest_left = destination.x + (copy_left - left);
    const auto dest_top = destination.y + (copy_top - top);
    for (auto y = copy_top; y < copy_bottom; ++y)
    {
        if (y < dest_top || y >= dest_top + vertical)
        {
            fill_scroll_segment(copy_left, y, horizontal, fill_char, fill_attr);
            continue;
        }
        if (copy_left < dest_left)
        {
            fill_scroll_segment(copy_left, y, std::min(dest_left, copy_right) - copy_left, fill_char, fill_attr);
        }
        const auto right_start = std::max(dest_left + horizontal, copy_left);
        if (right_start < copy_right)
        {
            fill_scroll_segment(right_start, y, copy_right - right_start, fill_char, fill_attr);
        }
    }
}
inline void screen_buffer::scroll_up(std::size_t lines, cell_style fill_style)
{
    if (lines == 0 || height_ == 0)
    {
        return;
    }

    auto &target = active_page();
    lines = std::min(lines, height_);

    for (std::size_t y = 0; y < lines; ++y)
    {
        auto &front = target.block(0);
        if (!alternate_active_ && history_limit_ != 0)
        {
            history_.push_back(history_row{
                std::move(front.cells),
                history_state{front.state.flags, front.state.command_exit_code},
                std::move(front.graphemes),
            });
        }
        else if (!front.cells.empty())
        {
            spare_rows_.push_back(std::move(front.cells));
        }
        target.erase_front();
        if (!spare_rows_.empty())
        {
            auto row = std::move(spare_rows_.back());
            spare_rows_.pop_back();
            if (blank_template_.size() != width_ || blank_style_ != fill_style)
            {
                blank_template_.assign(width_, cell::blank(fill_style));
                blank_style_ = fill_style;
            }
            std::copy(blank_template_.begin(), blank_template_.end(), row.begin());
            target.push_row(std::move(row));
        }
        else
        {
            target.push_blank(fill_style);
        }
    }

    trim_history();
    pending_scroll_up_ += lines;
}

inline void screen_buffer::scroll_region_down(rectangle area, std::size_t lines, cell_style fill_style)
{
    if (lines == 0 || area.width == 0 || area.height == 0)
    {
        return;
    }

    const auto top = area.y;
    const auto bottom = std::min(area.y + area.height, height_);
    const auto x_end = std::min(width_, area.x + area.width);
    if (top >= bottom || area.x >= x_end)
    {
        return;
    }
    lines = std::min(lines, bottom - top);
    auto &target = active_page();

    if (area.x == 0 && x_end == width_)
    {
        for (auto y = bottom - lines; y-- > top;)
        {
            auto &source = target.block(y);
            auto &destination = target.block(y + lines);
            destination.cells = std::move(source.cells);
            destination.graphemes = std::move(source.graphemes);
        }
        for (auto y = top; y < top + lines; ++y)
        {
            target.block(y).cells.assign(width_, cell::blank(fill_style));
            target.graphemes(y) = {};
        }
    }
    else
    {
        copy_rect(rectangle{area.x, top, area.width, bottom - top - lines}, rectangle{}, false,
                  coord{static_cast<std::int32_t>(area.x), static_cast<std::int32_t>(top + lines)});
        fill_rect(rectangle{area.x, top, area.width, lines}, cell::blank(fill_style));
    }

    for (auto y = top; y < bottom; ++y)
    {
        target.mark_dirty(y, area.x, x_end);
    }
}

inline void screen_buffer::scroll_region_up(rectangle area, std::size_t lines, cell_style fill_style)
{
    const auto x_end = std::min(width_, area.x + area.width);
    const auto y_end = std::min(height_, area.y + area.height);
    if (area.x >= x_end || area.y >= y_end || lines == 0)
    {
        return;
    }

    const auto region_height = y_end - area.y;
    lines = std::min(lines, region_height);
    auto &target = active_page();

    if (area.x == 0 && x_end == width_)
    {
        for (auto y = area.y; y + lines < y_end; ++y)
        {
            auto &source = target.block(y + lines);
            auto &destination = target.block(y);
            destination.cells = std::move(source.cells);
            destination.graphemes = std::move(source.graphemes);
        }
        for (auto y = y_end - lines; y < y_end; ++y)
        {
            target.block(y).cells.assign(width_, cell::blank(fill_style));
            target.graphemes(y) = {};
        }
    }
    else
    {
        for (auto y = area.y; y + lines < y_end; ++y)
        {
            auto &source_store = target.graphemes(y + lines);
            auto &dest_store = target.graphemes(y);
            for (auto x = area.x; x < x_end; ++x)
            {
                auto value = target.at(x, y + lines);
                if (value.has_cluster())
                {
                    const auto source = source_store.view(grapheme_store::ref{value.offset(), value.length()});
                    if (!source.empty() && dest_store.can_append(source.size()))
                    {
                        const auto ref = dest_store.append(source);
                        value = value.with_cluster_ref(ref.offset, ref.length);
                    }
                    else
                    {
                        value = cell::blank(value.style(), value.hyperlink());
                    }
                }
                target.at(x, y) = value;
            }
        }
        for (auto y = y_end - lines; y < y_end; ++y)
        {
            for (auto x = area.x; x < x_end; ++x)
            {
                target.at(x, y) = cell::blank(fill_style);
            }
        }
    }

    for (std::size_t y = area.y; y < y_end; ++y)
    {
        target.mark_dirty(y, area.x, x_end);
        target.mark_used(y, x_end);
    }
}

inline void screen_buffer::resize(std::size_t width, std::size_t height, cell_style fill_style, bool,
                                  coord *cursor)
{
    assert(width != 0 && height != 0);

    struct source_row_view final
    {
        std::span<const cell> cells;
        const grapheme_store *graphemes{};
        line_flags flags{};
        std::int32_t command_exit_code{no_command_exit_code};
    };

    struct reflow_unit final
    {
        cell value;
        const grapheme_store *source_graphemes{};
        std::size_t source_row{};
        std::size_t source_x{};
    };

    std::vector<source_row_view> sources;
    sources.reserve(history_.size() + height_);
    for (const auto &entry : history_)
    {
        sources.push_back(source_row_view{entry.cells, &entry.graphemes, entry.state.flags,
                                          entry.state.command_exit_code});
    }
    for (std::size_t y = 0; y < height_; ++y)
    {
        sources.push_back(source_row_view{main_.row_cells(y), &main_.graphemes(y), main_.row(y).flags,
                                          main_.row(y).command_exit_code});
    }

    const auto no_cursor = std::numeric_limits<std::size_t>::max();
    auto cursor_source_row = no_cursor;
    auto cursor_source_x = std::size_t{};
    if (cursor != nullptr)
    {
        const auto source_y = static_cast<std::size_t>(
            std::clamp<std::int32_t>(cursor->y, 0, static_cast<std::int32_t>(height_) - 1));
        cursor_source_row = history_.size() + source_y;
        cursor_source_x = static_cast<std::size_t>(
            std::clamp<std::int32_t>(cursor->x, 0, static_cast<std::int32_t>(width_) - 1));
        while (cursor_source_x > 0 && sources[cursor_source_row].cells[cursor_source_x].is_continuation())
        {
            --cursor_source_x;
        }
    }

    std::vector<row_block> output;
    output.reserve(sources.size() + 1);
    bool cursor_found = false;
    std::size_t cursor_output_row = 0;
    std::size_t cursor_output_x = 0;

    for (std::size_t start = 0; start < sources.size();)
    {
        auto end = start + 1;
        while (end < sources.size() && line_flag_ops::has(sources[end].flags, line_flag::wrapped))
        {
            ++end;
        }

        const bool cursor_line = cursor_source_row >= start && cursor_source_row < end;
        std::vector<reflow_unit> units;
        for (auto row_index = start; row_index < end; ++row_index)
        {
            const auto &source = sources[row_index];
            for (std::size_t x = 0; x < source.cells.size(); ++x)
            {
                if (source.cells[x].is_continuation())
                {
                    continue;
                }
                units.push_back(reflow_unit{source.cells[x], source.graphemes, row_index, x});
            }
        }

        while (!units.empty() && units.back().value.is_blank() &&
               !(cursor_line && units.back().source_row == cursor_source_row &&
                 units.back().source_x == cursor_source_x))
        {
            units.pop_back();
        }

        const auto base_flags =
            sources[start].flags & ~line_flag_ops::raw(line_flag::wrapped);
        const auto base_exit = sources[start].command_exit_code;
        row_block current{cell_vector(width, cell::blank(fill_style)), row_state{}, {}};
        current.state.flags = base_flags;
        current.state.command_exit_code = base_exit;
        auto target_x = std::size_t{};

        for (const auto &unit : units)
        {
            auto value = unit.value;
            auto columns = value.is_wide() ? std::size_t{2} : std::size_t{1};

            if (target_x + columns > width)
            {
                if (target_x == 0)
                {
                    value = cell::blank(value.style(), value.hyperlink());
                    columns = 1;
                }
                else
                {
                    current.state.used_columns = static_cast<std::uint32_t>(target_x);
                    current.state.dirty_begin = 0;
                    current.state.dirty_end = static_cast<std::uint32_t>(target_x);
                    output.push_back(std::move(current));

                    current = row_block{cell_vector(width, cell::blank(fill_style)), row_state{}, {}};
                    current.state.flags = base_flags | line_flag_ops::raw(line_flag::wrapped);
                    current.state.command_exit_code = base_exit;
                    target_x = 0;
                }
            }

            if (value.has_cluster())
            {
                const auto cluster = unit.source_graphemes->view(
                    grapheme_store::ref{value.offset(), value.length()});
                if (current.graphemes.can_append(cluster.size()))
                {
                    const auto ref = current.graphemes.append(cluster);
                    value = value.with_cluster_ref(ref.offset, ref.length);
                }
                else
                {
                    value = cell::blank(value.style(), value.hyperlink());
                }
            }

            current.cells[target_x] = value;
            if (columns == 2 && target_x + 1 < width)
            {
                current.cells[target_x + 1] = cell::tail(value.style(), value.hyperlink());
            }

            if (cursor_line && unit.source_row == cursor_source_row && unit.source_x == cursor_source_x)
            {
                cursor_output_row = output.size();
                cursor_output_x = target_x;
                cursor_found = true;
            }

            target_x += columns;
        }

        current.state.used_columns = static_cast<std::uint32_t>(target_x);
        current.state.dirty_begin = 0;
        current.state.dirty_end = static_cast<std::uint32_t>(target_x);
        output.push_back(std::move(current));
        start = end;
    }

    while (output.size() > 1)
    {
        bool blank = true;
        for (const auto &value : output.back().cells)
        {
            if (!value.is_blank())
            {
                blank = false;
                break;
            }
        }
        if (!blank || (cursor_found && cursor_output_row + 1 == output.size()))
        {
            break;
        }
        output.pop_back();
    }

    if (output.empty())
    {
        output.push_back(row_block{cell_vector(width, cell::blank(fill_style)), row_state{}, {}});
    }

    auto active_rows = std::min(height, output.size());
    auto history_count = output.size() - active_rows;
    if (history_count > history_limit_)
    {
        const auto drop = history_count - history_limit_;
        output.erase(output.begin(), output.begin() + static_cast<std::ptrdiff_t>(drop));
        if (cursor_found)
        {
            cursor_output_row = cursor_output_row >= drop ? cursor_output_row - drop : 0;
        }
        history_count -= drop;
    }

    history_.clear();
    for (std::size_t index = 0; index < history_count; ++index)
    {
        auto &entry = output[index];
        history_.push_back(history_row{
            std::move(entry.cells),
            history_state{entry.state.flags, entry.state.command_exit_code},
            std::move(entry.graphemes),
        });
    }

    page resized_main(width, height, fill_style);
    for (std::size_t y = 0; y < height; ++y)
    {
        const auto index = history_count + y;
        if (index < output.size())
        {
            resized_main.block(y) = std::move(output[index]);
        }
    }
    resized_main.mark_all_dirty();

    alternate_ = page(width, height, fill_style);
    main_ = std::move(resized_main);
    width_ = width;
    height_ = height;
    (void)view_.clamp_to_buffer(buffer_size());

    if (cursor != nullptr)
    {
        if (cursor_found && cursor_output_row >= history_count)
        {
            cursor->x = static_cast<std::int32_t>(std::min(cursor_output_x, width - 1));
            cursor->y = static_cast<std::int32_t>(
                std::min(cursor_output_row - history_count, height - 1));
        }
        else
        {
            cursor->x = std::clamp<std::int32_t>(cursor->x, 0, static_cast<std::int32_t>(width) - 1);
            cursor->y = std::clamp<std::int32_t>(cursor->y, 0, static_cast<std::int32_t>(height) - 1);
        }
    }
}

inline void screen_buffer::set_alternate(bool enabled)
{
    if (enabled)
    {
        if (!alternate_active_)
        {
            alternate_.clear(default_cell_style);
            alternate_active_ = true;
        }
        return;
    }

    alternate_.clear(default_cell_style);
    alternate_active_ = false;
}

inline void screen_buffer::clear_history()
{
    history_.clear();
}

inline std::span<const cell> screen_buffer::history_line(std::size_t index) const
{
    if (index >= history_.size())
    {
        return {};
    }
    return std::span<const cell>{history_[index].cells};
}

inline line_flags screen_buffer::history_flags(std::size_t index) const noexcept
{
    if (index >= history_.size())
    {
        return 0;
    }
    return history_[index].state.flags;
}

inline std::optional<std::int32_t> screen_buffer::history_command_exit_code(std::size_t index) const noexcept
{
    if (index >= history_.size())
    {
        return std::nullopt;
    }
    return history_[index].state.command_exit_code == no_command_exit_code
               ? std::nullopt
               : std::optional<std::int32_t>{history_[index].state.command_exit_code};
}

inline void screen_buffer::trim_history()
{
    while (history_.size() > history_limit_)
    {
        auto &front = history_.front();
        if (!front.cells.empty())
        {
            spare_rows_.push_back(std::move(front.cells));
        }
        history_.pop_front();
    }
}

inline grapheme_store &screen_buffer::history_graphemes(std::size_t index) noexcept
{
    assert(index < history_.size());
    return history_[index].graphemes;
}

inline const grapheme_store &screen_buffer::history_graphemes(std::size_t index) const noexcept
{
    assert(index < history_.size());
    return history_[index].graphemes;
}

inline std::span<const char16_t> screen_buffer::cluster_of(std::size_t y, const cell &value) const noexcept
{
    if (!value.has_cluster())
    {
        return {};
    }
    return active_page().graphemes(y).view(grapheme_store::ref{value.offset(), value.length()});
}

inline std::span<const char16_t> screen_buffer::history_cluster_of(std::size_t index,
                                                                    const cell &value) const noexcept
{
    if (!value.has_cluster() || index >= history_.size())
    {
        return {};
    }
    return history_[index].graphemes.view(grapheme_store::ref{value.offset(), value.length()});
}

inline bool rebuild_page_graphemes(page &target, grapheme_store &scratch)
{
    for (std::size_t y = 0; y < target.height(); ++y)
    {
        scratch.clear();
        auto &old_store = target.graphemes(y);
        for (auto &value : target.row_cells(y))
        {
            if (!value.has_cluster())
            {
                continue;
            }
            const auto source = old_store.view(grapheme_store::ref{value.offset(), value.length()});
            if (source.empty() || !scratch.can_append(source.size()))
            {
                return false;
            }
            const auto ref = scratch.append(source);
            value = value.with_cluster_ref(ref.offset, ref.length);
        }
        std::swap(old_store, scratch);
    }
    return true;
}

inline bool screen_buffer::rebuild_graphemes()
{
    if (!rebuild_page_graphemes(main_, grapheme_scratch_))
    {
        return false;
    }
    if (!rebuild_page_graphemes(alternate_, grapheme_scratch_))
    {
        return false;
    }

    for (auto &entry : history_)
    {
        grapheme_scratch_.clear();
        auto &old_store = entry.graphemes;
        for (auto &value : entry.cells)
        {
            if (!value.has_cluster())
            {
                continue;
            }
            const auto source = old_store.view(grapheme_store::ref{value.offset(), value.length()});
            if (source.empty() || !grapheme_scratch_.can_append(source.size()))
            {
                return false;
            }
            const auto ref = grapheme_scratch_.append(source);
            value = value.with_cluster_ref(ref.offset, ref.length);
        }
        std::swap(old_store, grapheme_scratch_);
    }
    return true;
}

inline std::size_t screen_buffer::damage_begin(std::size_t y) const noexcept
{
    return active_page().full_damage() ? 0 : active_page().row(y).dirty_begin;
}

inline std::size_t screen_buffer::damage_end(std::size_t y) const noexcept
{
    return active_page().full_damage() ? width_ : active_page().row(y).dirty_end;
}

inline void screen_buffer::clear_damage() noexcept
{
    active_page().clear_damage();
    pending_scroll_up_ = 0;
}

inline page &screen_buffer::active_page() noexcept
{
    if (alternate_active_)
    {
        return alternate_;
    }
    return main_;
}

inline const page &screen_buffer::active_page() const noexcept
{
    if (alternate_active_)
    {
        return alternate_;
    }
    return main_;
}

} // namespace corehost::screen