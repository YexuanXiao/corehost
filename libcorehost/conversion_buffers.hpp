#pragma once
#include <windows.h>
#include <array>
#include <vector>
#include "utility/raw_byte_allocator.hpp"

namespace conpty
{

class conversion_buffers
{
  public:
    raw_u32_buffer &u32() noexcept
    {
        return _u32;
    }

    raw_u8_buffer &utf8() noexcept
    {
        return _utf8;
    }

    raw_wide_buffer &wide() noexcept
    {
        return _wide;
    }

    std::vector<CHAR_INFO> &char_info() noexcept
    {
        return _char_info;
    }

    std::vector<INPUT_RECORD> &input_records() noexcept
    {
        return _input_records;
    }

  private:
    raw_u32_buffer _u32;
    raw_u8_buffer _utf8;
    raw_wide_buffer _wide;
    std::vector<CHAR_INFO> _char_info;
    std::vector<INPUT_RECORD> _input_records;
};

} // namespace conpty
