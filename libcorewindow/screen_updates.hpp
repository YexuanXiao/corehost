#pragma once

#include "grapheme_store.hpp"
#include "screen_buffer.hpp"

#include <cstddef>
#include <vector>

namespace corehost::window
{

struct screen_cell_update final
{
    screen::cell cell;
};

struct screen_row_update final
{
    std::size_t y{};
    std::size_t begin{};
    std::size_t end{};
    std::vector<screen_cell_update> cells;
    screen::grapheme_store graphemes;
};

struct screen_snapshot final
{
    std::vector<screen_row_update> rows;
};

} // namespace corehost::window
