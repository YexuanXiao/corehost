#pragma once

#include "buffer.hpp"

#include "win32/handle.hpp"
#include "win32/io.hpp"

#include <cassert>
#include <cstdlib>
#include <cstddef>
#include <span>

namespace corehost::conptyio
{

// vt_out sink. The serializer buffer is retained across flushes, and bytes are
// written to the terminal pipe in batches rather than per VT sequence.
class terminal_writer final
{
  public:
    static constexpr std::size_t initial_capacity = 64 * 1024;

    explicit terminal_writer(win32::handle_view output) noexcept : output_{output}
    {
        buffer_.reserve(initial_capacity);
    }

    [[nodiscard]] vtserializer::byte_buffer &buffer() noexcept
    {
        return buffer_;
    }

    [[nodiscard]] bool empty() const noexcept
    {
        return buffer_.empty();
    }

    void flush() noexcept
    {
        assert(output_.valid());
        if (buffer_.empty())
        {
            return;
        }
        const auto result = win32::write_all(output_, buffer_.bytes());
        if (result.failed())
        {
            std::abort();
        }
        buffer_.clear();
    }

  private:
    vtserializer::byte_buffer buffer_;
    win32::handle_view output_;
};

} // namespace corehost::conptyio