#pragma once
#include <span>
#include "win32/string.hpp"

namespace notification
{

struct action
{
    win32::wcstring_view label;
    win32::wcstring_view arguments;
};

void send(win32::wcstring_view title, win32::wcstring_view body, std::span<const action> action_buttons = {}) noexcept;

} // namespace notification
