#pragma once

#include <libunicode/convert.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <span>
#include <string>

namespace corehost::vtparser
{

// Incremental UTF-8 decoder. Incomplete tails are kept in a four-byte fixed
// buffer, so pipe chunks can split any multi-byte sequence without allocating.
class utf8_decoder final
{
  public:
    template <typename String>
    void decode(std::span<const char8_t> bytes, String &output)
    {
        output.clear();

        constexpr std::uint64_t high_bits = 0x8080808080808080ull;
        std::size_t index = 0;

        while (index < bytes.size())
        {
            if (pending_size_ == 0 && index + 8 <= bytes.size())
            {
                std::uint64_t word = 0;
                std::memcpy(&word, bytes.data() + index, sizeof(word));
                if ((word & high_bits) == 0)
                {
                    const auto offset = output.size();
                    output.resize(offset + 8);

                    std::uint32_t first = 0;
                    std::uint32_t second = 0;
                    std::memcpy(&first, bytes.data() + index, sizeof(first));
                    std::memcpy(&second, bytes.data() + index + 4, sizeof(second));
                    const auto widen = [](std::uint32_t value) noexcept {
                        return static_cast<std::uint64_t>(value & 0x000000FFu) |
                               (static_cast<std::uint64_t>(value & 0x0000FF00u) << 8) |
                               (static_cast<std::uint64_t>(value & 0x00FF0000u) << 16) |
                               (static_cast<std::uint64_t>(value & 0xFF000000u) << 24);
                    };
                    const auto first_wide = widen(first);
                    const auto second_wide = widen(second);
                    std::memcpy(output.data() + offset, &first_wide, sizeof(first_wide));
                    std::memcpy(output.data() + offset + 4, &second_wide, sizeof(second_wide));
                    index += 8;
                    continue;
                }
            }
            consume(static_cast<std::uint8_t>(bytes[index]), output);
            ++index;
        }
    }

    [[nodiscard]] std::size_t pending_size() const noexcept
    {
        return pending_size_;
    }

  private:
    static constexpr char16_t replacement = u'\uFFFD';

    static std::size_t expected_length(std::uint8_t byte) noexcept
    {
        if (byte < 0x80)
        {
            return 1;
        }
        if ((byte & 0xE0) == 0xC0)
        {
            return 2;
        }
        if ((byte & 0xF0) == 0xE0)
        {
            return 3;
        }
        if ((byte & 0xF8) == 0xF0)
        {
            return 4;
        }
        return 0;
    }

    static bool is_continuation(std::uint8_t byte) noexcept
    {
        return (byte & 0xC0) == 0x80;
    }

    template <typename String>
static void append(String &output, char32_t codepoint)
    {
        unicode::encoder<char16_t>{}(codepoint, std::back_inserter(output));
    }

    template <typename String>
static bool append_sequence(std::span<const char8_t> sequence, String &output)
    {
        if (sequence.empty())
        {
            return false;
        }

        const auto lead = static_cast<std::uint8_t>(sequence[0]);
        char32_t codepoint = 0;
        switch (sequence.size())
        {
        case 1:
            codepoint = lead;
            break;
        case 2:
            codepoint = static_cast<char32_t>(lead & 0x1F) << 6;
            codepoint |= static_cast<std::uint8_t>(sequence[1]) & 0x3F;
            if (codepoint < 0x80)
            {
                return false;
            }
            break;
        case 3:
            codepoint = static_cast<char32_t>(lead & 0x0F) << 12;
            codepoint |= (static_cast<std::uint8_t>(sequence[1]) & 0x3F) << 6;
            codepoint |= static_cast<std::uint8_t>(sequence[2]) & 0x3F;
            if (codepoint < 0x800 || (codepoint >= 0xD800 && codepoint <= 0xDFFF))
            {
                return false;
            }
            break;
        case 4:
            codepoint = static_cast<char32_t>(lead & 0x07) << 18;
            codepoint |= (static_cast<std::uint8_t>(sequence[1]) & 0x3F) << 12;
            codepoint |= (static_cast<std::uint8_t>(sequence[2]) & 0x3F) << 6;
            codepoint |= static_cast<std::uint8_t>(sequence[3]) & 0x3F;
            if (codepoint < 0x10000 || codepoint > 0x10FFFF)
            {
                return false;
            }
            break;
        default:
            return false;
        }

        append(output, codepoint);
        return true;
    }

    void reset() noexcept
    {
        pending_size_ = 0;
    }

    template <typename String>
void consume(std::uint8_t byte, String &output)
    {
        if (pending_size_ == 0)
        {
            const auto length = expected_length(byte);
            if (length == 1)
            {
                append(output, byte);
                return;
            }
            if (length == 0)
            {
                output.push_back(replacement);
                return;
            }
            pending_[0] = byte;
            pending_size_ = 1;
            return;
        }

        if (!is_continuation(byte))
        {
            output.push_back(replacement);
            reset();
            consume(byte, output);
            return;
        }

        pending_[pending_size_++] = byte;
        const auto expected = expected_length(static_cast<std::uint8_t>(pending_[0]));
        if (pending_size_ < expected)
        {
            return;
        }

        if (!append_sequence(std::span<const char8_t>{pending_.data(), pending_size_}, output))
        {
            output.push_back(replacement);
        }
        reset();
    }

    std::array<char8_t, 4> pending_{};
    std::size_t pending_size_{};
};

template <typename String>
inline void convert_to_utf16(std::span<const char16_t> input, String &output)
{
    output.clear();
    unicode::convert_to<char16_t>(std::u16string_view{input.data(), input.size()}, std::back_inserter(output));
}

} // namespace corehost::vtparser
