#pragma once

#include "utility/raw_byte_allocator.hpp"

namespace corehost::conpty
{

using corehost::utility::raw_byte_allocator;
using corehost::utility::raw_byte_vector;

using raw_u8_buffer = raw_byte_vector<char8_t>;
using raw_u16_buffer = raw_byte_vector<char16_t>;
using raw_wide_buffer = raw_byte_vector<wchar_t>;
using raw_u32_buffer = raw_byte_vector<char32_t>;

} // namespace corehost::conpty
