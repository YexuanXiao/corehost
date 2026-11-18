#pragma once

#include "buffer.hpp"
#include "simd/legacy_sgr.hpp"

#include "win32/event.hpp"
#include "win32/handle.hpp"
#include "win32/io.hpp"

#include <algorithm>
#include <cassert>
#include <cstring>
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

    explicit terminal_writer(win32::handle_view output)
        : output_{output}, close_requested_{win32::create_tag, /*manual_reset=*/true, /*initial=*/false},
          find_legacy_sgr_{simd::select_legacy_sgr_backend()}
    {
        buffer_.reserve(initial_capacity);
    }

    // Signals that the output pipe disappeared. The session loop treats this
    // exactly like a terminal/window close.
    [[nodiscard]] win32::handle_view close_requested() const noexcept
    {
        return close_requested_.view();
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
        if (!output_.valid())
        {
            buffer_.clear();
            return;
        }
        if (buffer_.empty())
        {
            return;
        }
        normalize_legacy_default_sgr();
        const auto result = win32::write_all(output_, buffer_.bytes());
        if (result.closed())
        {
            // The terminal disappeared while output was pending. Match conhost:
            // report the broken output pipe to the session loop so it can send
            // CTRL_CLOSE_EVENT and tear the session down cleanly.
            output_ = {};
            (void)::SetEvent(close_requested_.get());
            buffer_.clear();
            return;
        }
        if (result.failed())
        {
            std::abort();
        }
        buffer_.clear();
    }

  private:
    // 0x07 默认属性在旧路径里可能被展开成显式 37;40；白底配色必须使用
    // 终端自己的默认背景，因此把这个等价默认序列归一化为 SGR 0。
    void normalize_legacy_default_sgr() noexcept
    {
        constexpr std::u8string_view canonical = u8"\x1b[0m";
        auto *data = buffer_.mutable_data();
        std::size_t size = buffer_.size();
        std::size_t offset = 0;
        while (const auto *match = find_legacy_sgr_(data + offset, size - offset))
        {
            const auto index = static_cast<std::size_t>(match - data);
            std::memmove(data + index + canonical.size(), data + index + 10, (size - index - 10) * sizeof(char8_t));
            std::copy(canonical.begin(), canonical.end(), data + index);
            size -= 10 - canonical.size();
            offset = index + canonical.size();
        }
        buffer_.resize(size);
    }
    vtserializer::byte_buffer buffer_;
    win32::handle_view output_;
    win32::event close_requested_;
    simd::find_legacy_sgr_function find_legacy_sgr_;
};

} // namespace corehost::conptyio