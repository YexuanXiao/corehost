#pragma once
#include "utility/raw_byte_allocator.hpp"

#include <cstddef>
#include <string>

namespace corehost::vtparser
{

using corehost::utility::raw_byte_allocator;
using corehost::utility::raw_byte_vector;
using corehost::utility::raw_string_allocator;

template <typename T>
using raw_byte_string = std::basic_string<T, std::char_traits<T>, raw_string_allocator<T>>;

using raw_string = raw_byte_string<char>;
using raw_u16_string = raw_byte_string<char16_t>;

using raw_u8_buffer = raw_byte_vector<char8_t>;
using raw_u16_buffer = raw_byte_vector<char16_t>;

// ANSI is treated as DBCS here: a wide unit can produce at most two bytes.
// UTF-8 is the 4-byte character set; over an arbitrary UTF-16 sequence the
// tight upper bound is three bytes per code unit (a surrogate pair is four
// bytes for two units, while a BMP character can be three bytes).
[[nodiscard]] constexpr std::size_t narrow_capacity_from_wide(std::size_t units, bool utf8) noexcept
{
    return utf8 ? units * 3 : units * 2;
}

} // namespace corehost::vtparser
