#pragma once
#include <string_view>

namespace notification
{

struct action
{
    std::wstring_view label;
    std::wstring_view arguments;
};

void send(std::wstring_view title, std::wstring_view body, const action *action_button = nullptr) noexcept;

} // namespace notification
