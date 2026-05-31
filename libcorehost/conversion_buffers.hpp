#pragma once
#include <string>

namespace conpty
{

class conversion_buffers
{
  public:
    std::u32string &u32() noexcept
    {
        return _u32;
    }

    std::string &utf8() noexcept
    {
        return _utf8;
    }

    std::wstring &wide() noexcept
    {
        return _wide;
    }

  private:
    std::u32string _u32;
    std::string _utf8;
    std::wstring _wide;
};

} // namespace conpty
