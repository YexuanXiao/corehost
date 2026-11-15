#pragma once

#include "cell.hpp"
#include "grapheme_store.hpp"
#include "hyperlink.hpp"
#include "image.hpp"
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

struct row_block final
{
    std::vector<cell> cells;
    row_state state;
};

struct history_row final
{
    std::vector<cell> cells;
    history_state state;
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

    void erase_front() noexcept
    {
        assert(rows_.size() == height_);
        rows_.pop_front();
    }

    void push_blank(cell_style fill_style)
    {
        assert(rows_.size() + 1 == height_);
        rows_.push_back(row_block{std::vector<cell>(width_, cell::blank(fill_style)), row_state{}});
    }

    void mark_dirty(std::size_t y, std::size_t begin, std::size_t end) noexcept;
    void mark_used(std::size_t y, std::size_t end) noexcept;
    void clear_damage() noexcept;
    void mark_all_dirty() noexcept;

  private:
    std::size_t width_{};
    std::size_t height_{};
    bizwen::deque<row_block> rows_;
};
class screen_buffer final
{
  public:
    using line = std::vector<cell>;

    screen_buffer(std::size_t width, std::size_t height, std::size_t history_limit = 1000);

    [[nodiscard]] std::size_t width() const noexcept
    {
        return width_;
    }

    [[nodiscard]] std::size_t height() const noexcept
    {
        return height_;
    }

    [[nodiscard]] const cell &at(std::size_t x, std::size_t y) const noexcept;
    [[nodiscard]] std::span<cell> row_cells(std::size_t y) noexcept;
    [[nodiscard]] std::span<const cell> row_cells(std::size_t y) const noexcept;
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

    // Returns the complete glyph at x as UTF-16. A trailing cell resolves to its
    // leading cell. Blank/background cells return one space.
    [[nodiscard]] std::u16string glyph_at(std::size_t x, std::size_t y) const;

    // First Unicode scalar of the glyph at x. Surrogate pairs are decoded.
    [[nodiscard]] char32_t codepoint_at(std::size_t x, std::size_t y) const;

    // Column width of the glyph at x: 0 for a trailing half, 2 for a wide head.
    [[nodiscard]] int glyph_width(std::size_t x, std::size_t y) const;

    [[nodiscard]] bool is_trailing(std::size_t x, std::size_t y) const;

    [[nodiscard]] std::size_t used_columns(std::size_t y) const;

    // Returns the visible row text, skipping trailing wide-cell columns.
    [[nodiscard]] std::u16string line_text(std::size_t y) const;

    [[nodiscard]] legacy_cell legacy_at(std::size_t x, std::size_t y) const;

    // Convenience path for Console API writes that only carry a legacy WORD.
    [[nodiscard]] bool write_legacy_scalar(std::size_t x, std::size_t y, char16_t primary,
                                           std::uint16_t attributes, cell_width width = cell_width::narrow);

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

    // Full-page vertical scroll. On the main page, the top lines are moved to
    // scrollback; the alternate page never records history.
    void scroll_up(std::size_t lines, cell_style fill_style = default_cell_style);
    void scroll_region_up(rectangle area, std::size_t lines, cell_style fill_style = default_cell_style);

    void resize(std::size_t width, std::size_t height, cell_style fill_style = default_cell_style);
    void set_alternate(bool enabled);
    [[nodiscard]] bool alternate_active() const noexcept
    {
        return alternate_active_;
    }

    [[nodiscard]] std::size_t history_size() const noexcept
    {
        return history_.size();
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

    [[nodiscard]] grapheme_store &graphemes() noexcept
    {
        return graphemes_;
    }

    [[nodiscard]] const grapheme_store &graphemes() const noexcept
    {
        return graphemes_;
    }

    [[nodiscard]] std::span<const char16_t> cluster_of(const cell &value) const noexcept;
    [[nodiscard]] bool rebuild_graphemes();
    [[nodiscard]] std::size_t damage_begin(std::size_t y) const noexcept;
    [[nodiscard]] std::size_t damage_end(std::size_t y) const noexcept;
    void clear_damage() noexcept;

  private:
    [[nodiscard]] page &active_page() noexcept;
    [[nodiscard]] const page &active_page() const noexcept;
    void clear_wide_neighborhood(page &target, std::size_t x, std::size_t y);
    void trim_history();

    std::size_t width_{};
    std::size_t height_{};
    std::size_t history_limit_{};
    page main_;
    std::optional<page> alternate_;
    bool alternate_active_{};
    bizwen::deque<history_row> history_;
    style_table styles_;
    hyperlink_table hyperlinks_;
    image_table images_;
    grapheme_store graphemes_;
};


namespace
{
using cluster_remap = std::flat_map<cluster_offset, grapheme_store::ref>;

bool remap_cell(cell &value, const grapheme_store &source, grapheme_store &destination, cluster_remap &map)
{
    if (!value.has_cluster())
    {
        return true;
    }

    const auto found = map.find(value.offset());
    if (found != map.end())
    {
        value = value.with_cluster_ref(found->second.offset, found->second.length);
        return true;
    }

    const auto source_view = source.view(grapheme_store::ref{value.offset(), value.length()});
    if (source_view.empty() || !destination.can_append(source_view.size()))
    {
        return false;
    }

    const auto ref = destination.append(source_view);
    map.emplace(value.offset(), ref);
    value = value.with_cluster_ref(ref.offset, ref.length);
    return true;
}

bool remap_page(page &target, const grapheme_store &source, grapheme_store &destination, cluster_remap &map)
{
    for (std::size_t y = 0; y < target.height(); ++y)
    {
        for (auto &value : target.row_cells(y))
        {
            if (!remap_cell(value, source, destination, map))
            {
                return false;
            }
        }
    }
    return true;
}

bool remap_line(std::vector<cell> &line, const grapheme_store &source, grapheme_store &destination, cluster_remap &map)
{
    for (auto &value : line)
    {
        if (!remap_cell(value, source, destination, map))
        {
            return false;
        }
    }
    return true;
}
} // namespace

inline page::page(std::size_t width, std::size_t height, cell_style fill_style)
    : width_{width}, height_{height}
{
    assert(width != 0 && height != 0);
    for (std::size_t y = 0; y < height_; ++y)
    {
        rows_.push_back(row_block{std::vector<cell>(width_, cell::blank(fill_style)), row_state{}});
    }
    mark_all_dirty();
}

inline void page::mark_dirty(std::size_t y, std::size_t begin, std::size_t end) noexcept
{
    assert(y < rows_.size());
    if (begin >= end)
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
        return;
    }

    state.dirty_begin = std::min(state.dirty_begin, static_cast<std::uint32_t>(begin));
    state.dirty_end = std::max(state.dirty_end, static_cast<std::uint32_t>(end));
}

inline void page::mark_used(std::size_t y, std::size_t end) noexcept
{
    assert(y < rows_.size());
    rows_[y].state.used_columns = std::max(rows_[y].state.used_columns, static_cast<std::uint32_t>(std::min(end, width_)));
}

inline void page::clear_damage() noexcept
{
    for (auto &row : rows_)
    {
        row.state.dirty_begin = 0;
        row.state.dirty_end = 0;
    }
}

inline void page::mark_all_dirty() noexcept
{
    for (auto &row : rows_)
    {
        row.state.dirty_begin = 0;
        row.state.dirty_end = static_cast<std::uint32_t>(width_);
    }
}

inline screen_buffer::screen_buffer(std::size_t width, std::size_t height, std::size_t history_limit)
    : width_{width}, height_{height}, history_limit_{history_limit}, main_(width, height)
{
    assert(width != 0 && height != 0);
}

inline const cell &screen_buffer::at(std::size_t x, std::size_t y) const noexcept
{
    return active_page().at(x, y);
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

    if (value.is_wide() && x + 1 >= width_)
    {
        return false;
    }

    auto &target = active_page();
    clear_wide_neighborhood(target, x, y);
    if (value.is_wide())
    {
        clear_wide_neighborhood(target, x + 1, y);
    }

    target.at(x, y) = value;
    target.mark_dirty(y, x, x + 1);
    target.mark_used(y, x + 1);

    if (value.is_wide())
    {
        target.at(x + 1, y) = cell::tail(value.style(), value.hyperlink());
        target.mark_dirty(y, x + 1, x + 2);
        target.mark_used(y, x + 2);
    }

    return true;
}

inline bool screen_buffer::write_scalar(std::size_t x, std::size_t y, char16_t primary, cell_style style, cell_width width,
                                 cell_hyperlink hyperlink, cell_flags flags)
{
    if (width == cell_width::tail)
    {
        return write_cell(x, y, cell::tail(style, hyperlink));
    }

    return write_cell(x, y, cell::scalar(primary, style, width, hyperlink, flags));
}

inline bool screen_buffer::write_cluster(std::size_t x, std::size_t y, std::span<const char16_t> cluster, cell_style style,
                                  cell_width width, cell_hyperlink hyperlink, cell_flags flags)
{
    if (x >= width_ || y >= height_ || cluster.empty() || !graphemes_.can_append(cluster.size()))
    {
        return false;
    }

    if (width == cell_width::wide && x + 1 >= width_)
    {
        return false;
    }

    const auto ref = graphemes_.append(cluster);
    return write_cell(x, y, cell::complex(ref.offset, ref.length, style, width, hyperlink, flags));
}

inline bool screen_buffer::write_codepoint(std::size_t x, std::size_t y, char32_t codepoint,
                                           cell_style style, cell_width width, cell_hyperlink hyperlink,
                                           cell_flags flags)
{
    if (codepoint > 0x10FFFFu || (codepoint >= 0xD800u && codepoint <= 0xDFFFu))
    {
        codepoint = U'\xFFFD';
    }

    if (codepoint <= 0xFFFFu)
    {
        return write_scalar(x, y, static_cast<char16_t>(codepoint), style, width, hyperlink, flags);
    }

    const auto value = codepoint - 0x10000u;
    const char16_t cluster[2]{
        static_cast<char16_t>(0xD800u | (value >> 10)),
        static_cast<char16_t>(0xDC00u | (value & 0x3FFu)),
    };
    return write_cluster(x, y, std::span<const char16_t>{cluster, 2}, style, width, hyperlink, flags);
}

inline std::u16string screen_buffer::glyph_at(std::size_t x, std::size_t y) const
{
    const auto &value = at(x, y);
    if (value.is_continuation())
    {
        if (x == 0)
        {
            return {};
        }
        return glyph_at(x - 1, y);
    }

    if (value.has_cluster())
    {
        const auto cluster = cluster_of(value);
        return std::u16string{cluster.begin(), cluster.end()};
    }

    if (value.is_blank())
    {
        return std::u16string(1, u' ');
    }

    return std::u16string(1, value.primary());
}

inline char32_t screen_buffer::codepoint_at(std::size_t x, std::size_t y) const
{
    const auto text = glyph_at(x, y);
    if (text.empty())
    {
        return U' ';
    }
    if (text.size() >= 2 && text[0] >= 0xD800 && text[0] <= 0xDBFF &&
        text[1] >= 0xDC00 && text[1] <= 0xDFFF)
    {
        return static_cast<char32_t>(0x10000u +
                                     ((static_cast<std::uint32_t>(text[0]) - 0xD800u) << 10) +
                                     (static_cast<std::uint32_t>(text[1]) - 0xDC00u));
    }
    return text[0];
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

inline std::u16string screen_buffer::line_text(std::size_t y) const
{
    std::u16string result;
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
            const auto cluster = cluster_of(value);
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
    return result;
}

inline legacy_cell screen_buffer::legacy_at(std::size_t x, std::size_t y) const
{
    const auto &value = at(x, y);
    return legacy_cell{value.text_unit(), styles_.at(value.style()).legacy_attributes};
}

inline bool screen_buffer::write_legacy_scalar(std::size_t x, std::size_t y, char16_t primary,
                                               std::uint16_t attributes, cell_width width)
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
            });
        }
        target.erase_front();
        target.push_blank(fill_style);
    }

    trim_history();
    target.mark_all_dirty();
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

    for (std::size_t y = area.y; y + lines < y_end; ++y)
    {
        std::copy(target.row_cells(y + lines).begin() + static_cast<std::ptrdiff_t>(area.x),
                  target.row_cells(y + lines).begin() + static_cast<std::ptrdiff_t>(x_end),
                  target.row_cells(y).begin() + static_cast<std::ptrdiff_t>(area.x));
    }

    for (std::size_t y = y_end - lines; y < y_end; ++y)
    {
        for (auto x = area.x; x < x_end; ++x)
        {
            target.at(x, y) = cell::blank(fill_style);
        }
    }

    for (std::size_t y = area.y; y < y_end; ++y)
    {
        target.mark_dirty(y, area.x, x_end);
        target.mark_used(y, x_end);
    }
}

inline void screen_buffer::resize(std::size_t width, std::size_t height, cell_style fill_style)
{
    assert(width != 0 && height != 0);

    page resized_main(width, height, fill_style);
    const auto copy_width = std::min(width, width_);
    const auto copy_height = std::min(height, height_);
    for (std::size_t y = 0; y < copy_height; ++y)
    {
        std::copy_n(main_.row_cells(y).begin(), copy_width, resized_main.row_cells(y).begin());
        resized_main.row(y).used_columns = static_cast<std::uint32_t>(std::min<std::size_t>(main_.row(y).used_columns, width));
        resized_main.row(y).flags = main_.row(y).flags;
        resized_main.row(y).command_exit_code = main_.row(y).command_exit_code;
    }

    if (alternate_)
    {
        page resized_alt(width, height, fill_style);
        for (std::size_t y = 0; y < copy_height; ++y)
        {
            std::copy_n(alternate_->row_cells(y).begin(), copy_width, resized_alt.row_cells(y).begin());
            resized_alt.row(y).used_columns = static_cast<std::uint32_t>(std::min<std::size_t>(alternate_->row(y).used_columns, width));
            resized_alt.row(y).flags = alternate_->row(y).flags;
            resized_alt.row(y).command_exit_code = alternate_->row(y).command_exit_code;
        }
        *alternate_ = std::move(resized_alt);
    }

    main_ = std::move(resized_main);
    width_ = width;
    height_ = height;
    clear_history();
}

inline void screen_buffer::set_alternate(bool enabled)
{
    if (enabled && !alternate_)
    {
        alternate_.emplace(width_, height_, default_cell_style);
    }
    alternate_active_ = enabled;
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
        history_.pop_front();
    }
}

inline std::span<const char16_t> screen_buffer::cluster_of(const cell &value) const noexcept
{
    if (!value.has_cluster())
    {
        return {};
    }
    return graphemes_.view(grapheme_store::ref{value.offset(), value.length()});
}

inline bool screen_buffer::rebuild_graphemes()
{
    grapheme_store rebuilt;
    cluster_remap map;

    if (!remap_page(main_, graphemes_, rebuilt, map))
    {
        return false;
    }

    if (alternate_ && !remap_page(*alternate_, graphemes_, rebuilt, map))
    {
        return false;
    }

    for (auto &entry : history_)
    {
        if (!remap_line(entry.cells, graphemes_, rebuilt, map))
        {
            return false;
        }
    }

    graphemes_ = std::move(rebuilt);
    return true;
}

inline std::size_t screen_buffer::damage_begin(std::size_t y) const noexcept
{
    return active_page().row(y).dirty_begin;
}

inline std::size_t screen_buffer::damage_end(std::size_t y) const noexcept
{
    return active_page().row(y).dirty_end;
}

inline void screen_buffer::clear_damage() noexcept
{
    active_page().clear_damage();
}

inline page &screen_buffer::active_page() noexcept
{
    if (alternate_active_ && alternate_)
    {
        return *alternate_;
    }
    return main_;
}

inline const page &screen_buffer::active_page() const noexcept
{
    if (alternate_active_ && alternate_)
    {
        return *alternate_;
    }
    return main_;
}


} // namespace corehost::screen
