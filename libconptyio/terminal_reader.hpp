#pragma once

#include "win32/handle.hpp"
#include "win32/io.hpp"

#include <libunicode/convert.h>

#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace corehost::conptyio
{

// Batched vt_in reader. Bytes are read into a reusable buffer, decoded into a
// reusable UTF-32 buffer, and incomplete UTF-8 tails are retained across reads.
class terminal_reader final
{
  public:
    explicit terminal_reader(win32::handle_view input) noexcept : input_{input} {}

    [[nodiscard]] bool read_batch()
    {
        const auto result = win32::read_some(input_, std::span<char>{bytes_});
        if (result.closed() || result.failed())
        {
            closed_ = result.closed();
            return false;
        }
        if (result.bytes == 0)
        {
            return false;
        }

        pending_.append(bytes_.data(), result.bytes);
        decode();
        return true;
    }

    [[nodiscard]] std::span<const char32_t> codepoints() const noexcept
    {
        return codepoints_;
    }

    [[nodiscard]] bool closed() const noexcept
    {
        return closed_;
    }

  private:
    void decode()
    {
        codepoints_.clear();
        std::size_t offset = 0;
        while (offset < pending_.size())
        {
            auto iterator = pending_.begin() + static_cast<std::ptrdiff_t>(offset);
            const auto start = iterator;
            const auto decoded = unicode::decoder<char>{}(iterator);
            if (decoded.has_value())
            {
                codepoints_.push_back(decoded.value());
                offset = static_cast<std::size_t>(iterator - pending_.begin());
                continue;
            }

            // Incomplete sequence at the end: retain it for the next read.
            if (iterator == pending_.end())
            {
                break;
            }

            codepoints_.push_back(0xFFFD);
            offset = static_cast<std::size_t>(iterator - pending_.begin());
            if (iterator == start)
            {
                ++offset;
            }
        }

        pending_.erase(0, offset);
    }

    win32::handle_view input_;
    std::vector<char> bytes_ = std::vector<char>(4096);
    std::string pending_;
    std::u32string codepoints_;
    bool closed_{};
};

} // namespace corehost::conptyio