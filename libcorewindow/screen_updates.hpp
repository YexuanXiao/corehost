#pragma once

#include "screen_buffer.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace corehost::window
{

struct screen_cell_update final
{
    screen::cell_style style{};
    screen::cell_width width{};
    screen::cell_flags flags{};
    screen::cell_hyperlink hyperlink{};
    std::size_t text_offset{};
    std::size_t text_length{};
};

struct screen_row_update final
{
    std::size_t y{};
    std::size_t begin{};
    std::size_t end{};
    std::vector<screen_cell_update> cells;
    std::u16string text;
};

} // namespace corehost::window
