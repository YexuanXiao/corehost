#pragma once
#include <windows.h>
#include <cassert>
#include <charconv>
#include <span>
#include <string_view>
#include <vector>
#include "win32/handle.hpp"
#include "char_convert.hpp"
#include "perf_diag.hpp"
#include "utility/raw_byte_allocator.hpp"

namespace conpty
{

class vt_output_buffer
{
  public:
    vt_output_buffer()
    {
        _buffer.reserve(flush_threshold);
    }

    void set_output(win32::handle_view output) noexcept
    {
        _output = output;
    }

    size_t buffered_size() const noexcept
    {
        return _buffer.size();
    }

    bool should_flush() const noexcept
    {
        return _buffer.size() >= flush_threshold;
    }

    void flush()
    {
        COREHOST_PERF_SCOPE_AMOUNT(vt_output_flush, _buffer.size());
        if (_buffer.empty())
            return;
        DWORD written = 0;
        {
            COREHOST_PERF_SCOPE_AMOUNT(vt_output_write_file, _buffer.size());
            ::WriteFile(_output.get(), _buffer.data(), static_cast<DWORD>(_buffer.size()), &written, nullptr);
        }
        _buffer.clear();
    }

    void append(std::string_view text)
    {
        const auto bytes = std::span{reinterpret_cast<const char8_t *>(text.data()), text.size()};
        _buffer.append_range(bytes);
    }

    void append_utf32(std::u32string_view text)
    {
        if (text.empty())
            return;

        const auto offset = _buffer.size();
        _buffer.resize(offset + text.size() * 4);
        auto *first = reinterpret_cast<char *>(_buffer.data() + offset);
        auto *end = unicode::convert_to<char>(text, first);
        _buffer.resize(offset + static_cast<size_t>(end - first));
    }

    void append_utf16(std::wstring_view text)
    {
        if (text.empty())
            return;

        const auto offset = _buffer.size();
        _buffer.resize(offset + text.size() * 3);
        auto *first = reinterpret_cast<char *>(_buffer.data() + offset);
        auto *end = unicode::convert_to<char>(
            std::u16string_view{reinterpret_cast<const char16_t *>(text.data()), text.size()}, first);
        _buffer.resize(offset + static_cast<size_t>(end - first));
    }

    void append_gbk(std::string_view text)
    {
        if (text.empty())
            return;

        const auto offset = _buffer.size();
        _buffer.resize(offset + text.size() * 3);
        auto *first = reinterpret_cast<char *>(_buffer.data() + offset);
        auto *out = first;
        size_t input = 0;
        unicode::encoder<char> enc;
        while (input < text.size())
            out = enc(gbk_decode_next(text.data(), text.size(), input), out);
        _buffer.resize(offset + static_cast<size_t>(out - first));
    }

    void append(char ch)
    {
        _buffer.push_back(static_cast<char8_t>(static_cast<unsigned char>(ch)));
    }

    void append_int(int value)
    {
        constexpr size_t max_int_chars = 16;
        const auto offset = _buffer.size();
        _buffer.resize(offset + max_int_chars);
        auto *first = reinterpret_cast<char *>(_buffer.data() + offset);
        const auto [end, ec] = std::to_chars(first, first + max_int_chars, value);
        assert(ec == std::errc{});
        _buffer.resize(offset + static_cast<size_t>(end - first));
    }

    void append_cell(char32_t ch)
    {
        char bytes[8];
        const auto length = to_utf8_bytes(ch, bytes);
        append(std::string_view{bytes, static_cast<size_t>(length)});
    }

  private:
    static constexpr size_t flush_threshold = 64 * 1024;

    win32::handle_view _output;
    std::vector<char8_t, raw_byte_allocator<char8_t>> _buffer;
};

} // namespace conpty
