#pragma once
#include <windows.h>
#include <array>
#include <cstring>
#include <string_view>
#include "win32/handle.hpp"
#include "char_convert.hpp"

namespace conpty
{

class vt_output_buffer
{
  public:
    void set_output(win32::handle_view output) noexcept
    {
        _output = output;
    }

    size_t buffered_size() const noexcept
    {
        return _length;
    }

    void flush()
    {
        if (_length == 0)
            return;
        DWORD written = 0;
        ::WriteFile(_output.get(), _buffer.data(), static_cast<DWORD>(_length), &written, nullptr);
        _length = 0;
    }

    void write(const char *data, size_t length)
    {
        if (length == 0)
            return;
        flush();
        DWORD written = 0;
        ::WriteFile(_output.get(), data, static_cast<DWORD>(length), &written, nullptr);
    }

    void append(std::string_view text)
    {
        const auto length = text.size();
        if (length > _buffer.size())
        {
            write(text.data(), length);
            return;
        }
        if (_length + length > _buffer.size())
            flush();
        std::memcpy(_buffer.data() + _length, text.data(), length);
        _length += length;
    }

    void append(char ch)
    {
        if (_length >= _buffer.size())
            flush();
        _buffer[_length++] = ch;
    }

    void append_int(int value)
    {
        if (value == 0)
        {
            append('0');
            return;
        }
        if (value < 0)
        {
            append('-');
            value = -value;
        }
        char digits[16];
        size_t count = 0;
        while (value > 0)
        {
            digits[count++] = static_cast<char>('0' + (value % 10));
            value /= 10;
        }
        if (_length + count > _buffer.size())
            flush();
        while (count > 0)
            _buffer[_length++] = digits[--count];
    }

    void append_cell(char32_t ch)
    {
        char bytes[8];
        const auto length = to_utf8_bytes(ch, bytes);
        if (_length + length > _buffer.size())
            flush();
        std::memcpy(_buffer.data() + _length, bytes, static_cast<size_t>(length));
        _length += static_cast<size_t>(length);
    }

  private:
    static constexpr size_t buffer_capacity = 8192;

    win32::handle_view _output;
    std::array<char, buffer_capacity> _buffer{};
    size_t _length = 0;
};

} // namespace conpty
