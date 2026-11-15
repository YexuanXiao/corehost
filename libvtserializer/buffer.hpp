#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace corehost::vtserializer
{

// Reusable serializer output buffer. clear() keeps capacity so callers can
// serialize batch after batch without reallocating.
class byte_buffer final
{
  public:
    void reserve(std::size_t capacity)
    {
        bytes_.reserve(capacity);
    }

    void clear() noexcept
    {
        bytes_.clear();
    }

    [[nodiscard]] bool empty() const noexcept
    {
        return bytes_.empty();
    }

    [[nodiscard]] std::size_t size() const noexcept
    {
        return bytes_.size();
    }

    [[nodiscard]] std::size_t capacity() const noexcept
    {
        return bytes_.capacity();
    }

    [[nodiscard]] std::span<const char8_t> bytes() const noexcept
    {
        return std::span<const char8_t>{bytes_};
    }

    void append(std::span<const char8_t> value)
    {
        bytes_.insert(bytes_.end(), value.begin(), value.end());
    }

    void append_text(std::u8string_view value)
    {
        append(std::span<const char8_t>{value.data(), value.size()});
    }

    void push_back(char8_t value)
    {
        bytes_.push_back(value);
    }

  private:
    std::vector<char8_t> bytes_;
};

} // namespace corehost::vtserializer