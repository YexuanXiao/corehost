#pragma once

#include "win32/handle.hpp"
#include "win32/io.hpp"

#include "utf8.hpp"

#include <span>
#include <string>
#include <vector>

namespace corehost::conptyio
{

// Batched vt_in reader. Bytes are read into a reusable buffer, decoded into a
// reusable UTF-16 buffer, and incomplete UTF-8 tails are retained across reads.
class terminal_reader final
{
  public:
    explicit terminal_reader(win32::handle_view input) noexcept : input_{input} {}

    [[nodiscard]] bool read_batch()
    {
        const auto result = win32::read_some(input_, std::span<char8_t>{bytes_});
        if (result.closed())
        {
            closed_ = true;
            return false;
        }
        if (result.failed())
        {
            throw result.error;
        }
        if (result.bytes == 0)
        {
            closed_ = true;
            return false;
        }

        decoder_.decode(
            std::span<const char8_t>{bytes_.data(), result.bytes}, codepoints_);
        return true;
    }

    [[nodiscard]] std::span<const char16_t> codepoints() const noexcept
    {
        return codepoints_;
    }

    [[nodiscard]] bool closed() const noexcept
    {
        return closed_;
    }

  private:
    win32::handle_view input_;
    std::vector<char8_t> bytes_ = std::vector<char8_t>(4096);
    vtparser::utf8_decoder decoder_;
    std::u16string codepoints_;
    bool closed_{};
};

} // namespace corehost::conptyio
