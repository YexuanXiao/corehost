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
    // 返回 UTF-32 临时缓冲，供 ANSI/UTF-16 输入转换和标题/历史序列化复用。
    raw_u32_buffer &u32() noexcept
    {
        return _u32;
    }

    // 返回 UTF-8/ANSI 字节临时缓冲，供输出管道和 ANSI API 复用。
    raw_u8_buffer &utf8() noexcept
    {
        return _utf8;
    }

    // 返回 UTF-16 临时缓冲，供非 UTF-8 代码页的 Win32 转码复用。
    raw_wide_buffer &wide() noexcept
    {
        return _wide;
    }

    // 返回 CHAR_INFO 临时缓冲，供矩形读写 API 在消息体边界转换。
    std::vector<CHAR_INFO> &char_info() noexcept
    {
        return _char_info;
    }

    // 返回 INPUT_RECORD 临时缓冲，供 WriteConsoleInputA/W 转换后批量入队。
    std::vector<INPUT_RECORD> &input_records() noexcept
    {
        return _input_records;
    }

  private:
    // 以下缓冲只保存一次 API 调用内的临时转换结果，调用结束后可被下次复用。
    raw_u32_buffer _u32;
    raw_u8_buffer _utf8;
    raw_wide_buffer _wide;
    std::vector<CHAR_INFO> _char_info;
    std::vector<INPUT_RECORD> _input_records;
};

} // namespace conpty
