#pragma once

#include "buffer.hpp"

#include "win32/handle.hpp"
#include "win32/io.hpp"

#include <cstddef>
#include <span>

namespace corehost::conptyio
{

// vt_out sink. The serializer buffer is retained across flushes, and bytes are
// written to the terminal pipe in batches rather than per VT sequence.
class terminal_writer final
{
  public:
    static constexpr std::size_t flush_threshold = 64 * 1024;

    explicit terminal_writer(win32::handle_view output) noexcept : output_{output}
    {
        buffer_.reserve(flush_threshold);
    }

    [[nodiscard]] vtserializer::byte_buffer &buffer() noexcept
    {
        return buffer_;
    }

    [[nodiscard]] bool should_flush() const noexcept
    {
        return buffer_.size() >= flush_threshold;
    }

    [[nodiscard]] bool empty() const noexcept
    {
        return buffer_.empty();
    }

    [[nodiscard]] bool flush() noexcept
    {
        if (buffer_.empty())
        {
            return true;
        }

        const auto result = win32::write_all(output_, buffer_.bytes());
        if (!result.success() && !result.empty())
        {
            return false;
        }
        buffer_.clear();
        return true;
    }

  private:
    vtserializer::byte_buffer buffer_;
    win32::handle_view output_;
};

} // namespace corehost::conptyio