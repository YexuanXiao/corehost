#pragma once
#include <span>
#include <string_view>
#include <windows.h>
#include "cstring_view/cstring_view.hpp"

namespace win32
{
using wcstring_view = beman::cstring_view::wcstring_view;
} // namespace win32
