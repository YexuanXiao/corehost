#pragma once

#include <libunicode/convert.h>

#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <iterator>

namespace corehost::vtparser
{

// Incremental UTF-8 decode into a reusable UTF-32 buffer. Incomplete tails are
// retained in pending; callers can feed pipe chunks without losing sequences.
inline void decode_utf8(std::span<const char8_t> bytes,
                        std::u16string &output,
                        std::string &pending)
{
    output.clear();
    pending.append(reinterpret_cast<const char *>(bytes.data()), bytes.size());

    std::size_t offset = 0;
    while (offset < pending.size())
    {
        auto iterator = pending.begin() + static_cast<std::ptrdiff_t>(offset);
        const auto start = iterator;
        const auto decoded = unicode::decoder<char>{}(iterator);
        if (decoded.has_value())
        {
            unicode::encoder<char16_t>{}(decoded.value(), std::back_inserter(output));
            offset = static_cast<std::size_t>(iterator - pending.begin());
            continue;
        }

        if (iterator == pending.end())
        {
            break;
        }

        output.push_back(0xFFFD);
        offset = static_cast<std::size_t>(iterator - pending.begin());
        if (iterator == start)
        {
            ++offset;
        }
    }

    pending.erase(0, offset);
}

inline void convert_to_utf16(std::span<const char16_t> input, std::u16string &output)
{
    output.clear();
    unicode::convert_to<char16_t>(std::u16string_view{input.data(), input.size()},
                                  std::back_inserter(output));
}

} // namespace corehost::vtparser