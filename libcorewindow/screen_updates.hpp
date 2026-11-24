#pragma once

#include "screen_buffer.hpp"

#include <Windows.h>

#include <cstddef>
#include <string>
#include <vector>

namespace corehost::window
{

struct screen_cell_update final
{
    screen::cell cell;
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

// Non-owning callback supplied by the console session. The UI thread invokes
// it before rendering; the implementation copies only damaged regions while
// holding its own short-lived lock.
struct screen_update_source final
{
    void *context{};
    bool (*take)(void *, std::vector<screen_row_update> &){};
    void (*attach)(void *, HWND){};
};

inline constexpr UINT screen_update_message = WM_APP + 1;

} // namespace corehost::window
