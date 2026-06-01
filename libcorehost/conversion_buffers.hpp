#pragma once
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

  private:
    raw_u32_buffer _u32;
    raw_u8_buffer _utf8;
    raw_wide_buffer _wide;
};

} // namespace conpty
