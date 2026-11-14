#pragma once

#include "cell.hpp"
#include "screen_buffer.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace corehost::screen
{

enum class cursor_style : std::uint8_t
{
    block = 0,
    underline = 1,
    bar = 2,
};

struct cursor_state final
{
    std::size_t x{};
    std::size_t y{};
    bool visible{true};
    cursor_style style{cursor_style::block};
};

class tab_stop_table final
{
  public:
    void assign(std::size_t columns, bool value);
    void resize(std::size_t columns, bool value);
    [[nodiscard]] std::size_t size() const noexcept;
    [[nodiscard]] bool operator[](std::size_t column) const noexcept;
    void set(std::size_t column, bool value) noexcept;
    void clear() noexcept;

  private:
    std::vector<std::uint8_t> values_;
};

struct selection_range final
{
    bool active{};
    std::size_t anchor_x{};
    std::size_t anchor_y{};
    std::size_t caret_x{};
    std::size_t caret_y{};

    [[nodiscard]] bool contains(std::size_t x, std::size_t y) const noexcept;
};

// Console-level state shared by VT and Console API paths. This deliberately
// remains separate from page/cell storage: cursor, tabs, margins, selection and
// search matches are screen state, not persistent cell content.
class console_state final
{
  public:
    console_state(std::size_t width, std::size_t height);

    [[nodiscard]] std::size_t width() const noexcept
    {
        return width_;
    }

    [[nodiscard]] std::size_t height() const noexcept
    {
        return height_;
    }

    void resize(std::size_t width, std::size_t height);

    [[nodiscard]] cursor_state &cursor() noexcept
    {
        return cursor_;
    }

    [[nodiscard]] const cursor_state &cursor() const noexcept
    {
        return cursor_;
    }

    void set_cursor(std::size_t x, std::size_t y) noexcept;
    void move_cursor(std::ptrdiff_t dx, std::ptrdiff_t dy) noexcept;
    void clamp_cursor() noexcept;

    bool wrap_at_eol{true};
    bool insert_mode{false};
    bool origin_mode{false};
    cell_style default_style{default_cell_style};
    cell_hyperlink default_hyperlink{no_cell_hyperlink};

    [[nodiscard]] std::size_t scroll_top() const noexcept
    {
        return scroll_top_;
    }

    [[nodiscard]] std::size_t scroll_bottom() const noexcept
    {
        return scroll_bottom_;
    }

    void set_scroll_region(std::size_t top, std::size_t bottom);
    void reset_scroll_region() noexcept;
    [[nodiscard]] bool in_scroll_region(std::size_t y) const noexcept;

    [[nodiscard]] tab_stop_table &tabs() noexcept
    {
        return tabs_;
    }

    [[nodiscard]] const tab_stop_table &tabs() const noexcept
    {
        return tabs_;
    }

    void reset_tab_stops();
    [[nodiscard]] std::size_t next_tab_stop(std::size_t column) const noexcept;
    [[nodiscard]] std::size_t previous_tab_stop(std::size_t column) const noexcept;

    [[nodiscard]] const selection_range &selection() const noexcept
    {
        return selection_;
    }

    void set_selection(std::size_t anchor_x, std::size_t anchor_y, std::size_t caret_x, std::size_t caret_y) noexcept;
    void clear_selection() noexcept;
    [[nodiscard]] bool has_selection() const noexcept;
    [[nodiscard]] bool is_selected(std::size_t x, std::size_t y) const noexcept;

    void set_search_matches(std::vector<rectangle> matches);
    void clear_search_matches() noexcept;
    [[nodiscard]] std::span<const rectangle> search_matches() const noexcept;
    [[nodiscard]] bool is_search_match(std::size_t x, std::size_t y) const noexcept;

  private:
    std::size_t width_{};
    std::size_t height_{};
    cursor_state cursor_;
    std::size_t scroll_top_{};
    std::size_t scroll_bottom_{};
    tab_stop_table tabs_;
    selection_range selection_;
    std::vector<rectangle> search_matches_;
};

} // namespace corehost::screen
