#pragma once

#include "cell.hpp"
#include "grapheme_store.hpp"
#include "hyperlink.hpp"
#include "image.hpp"
#include "style.hpp"

#include <cstddef>
#include <deque>
#include <optional>
#include <span>
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

struct row_state final
{
    std::size_t used_columns{};
    line_flags flags{};
    std::optional<std::int32_t> command_exit_code{};
    std::size_t dirty_begin{};
    std::size_t dirty_end{};

    [[nodiscard]] bool is_dirty() const noexcept
    {
        return dirty_begin < dirty_end;
    }
};

struct rectangle final
{
    std::size_t x{};
    std::size_t y{};
    std::size_t width{};
    std::size_t height{};
};

// One dense screen page: rows * columns cells plus per-row metadata. A page
// does not own cluster text or styles; those are screen-wide resources.
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
        return cells_[y * width_ + x];
    }

    [[nodiscard]] const cell &at(std::size_t x, std::size_t y) const noexcept
    {
        return cells_[y * width_ + x];
    }

    [[nodiscard]] std::span<cell> row_cells(std::size_t y) noexcept
    {
        return std::span<cell>{cells_}.subspan(y * width_, width_);
    }

    [[nodiscard]] std::span<const cell> row_cells(std::size_t y) const noexcept
    {
        return std::span<const cell>{cells_}.subspan(y * width_, width_);
    }

    [[nodiscard]] row_state &row(std::size_t y) noexcept
    {
        return rows_[y];
    }

    [[nodiscard]] const row_state &row(std::size_t y) const noexcept
    {
        return rows_[y];
    }

    void mark_dirty(std::size_t y, std::size_t begin, std::size_t end) noexcept;
    void mark_used(std::size_t y, std::size_t end) noexcept;
    void clear_damage() noexcept;
    void mark_all_dirty() noexcept;

  private:
    std::size_t width_{};
    std::size_t height_{};
    std::vector<cell> cells_;
    std::vector<row_state> rows_;
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

    [[nodiscard]] cell at(std::size_t x, std::size_t y) const noexcept;
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
    struct history_entry
    {
        line cells;
        line_flags flags{};
        std::optional<std::int32_t> command_exit_code{};
    };

    std::size_t history_limit_{};
    page main_;
    std::optional<page> alternate_;
    bool alternate_active_{};
    std::deque<history_entry> history_;
    style_table styles_;
    hyperlink_table hyperlinks_;
    image_table images_;
    grapheme_store graphemes_;
};

} // namespace corehost::screen
