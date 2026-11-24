#pragma once

#include "cell.hpp"
#include "raw_buffer.hpp"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>

namespace corehost::screen
{

// Screen-wide UTF-16 cluster store. Cells address it with a 24-bit offset and
// an 8-bit length. The store is append-only between explicit rebuilds; the
// caller owns capacity policy.
class grapheme_store final
{
  public:
    static constexpr std::uint32_t max_offset = 0xFFFFFFu;
    static constexpr std::uint32_t max_units = max_offset + 1u; // 16,777,216
    static constexpr std::uint8_t max_cluster_length = cell::max_cluster_length;

    struct ref final
    {
        cluster_offset offset{};
        std::uint8_t length{};
    };

    void reserve(std::size_t units)
    {
        units_.reserve(units);
    }

    void clear() noexcept
    {
        units_.clear();
    }

    [[nodiscard]] bool can_append(std::size_t units) const noexcept
    {
        return units > 0 && units <= max_cluster_length && units <= static_cast<std::size_t>(max_units - units_.size());
    }

    // The caller must call can_append() first. append() deliberately does not
    // rebuild or truncate; it only writes and returns the ref.
    [[nodiscard]] ref append(std::span<const char16_t> cluster)
    {
        assert(!cluster.empty());
        assert(can_append(cluster.size()));

        const auto offset = static_cast<cluster_offset>(units_.size());
        units_.append_range(cluster);
        return ref{offset, static_cast<std::uint8_t>(cluster.size())};
    }

    [[nodiscard]] std::span<const char16_t> view(ref value) const noexcept
    {
        assert(value.length != 0);
        assert(static_cast<std::size_t>(value.offset) + value.length <= units_.size());
        return std::span<const char16_t>{units_}.subspan(value.offset, value.length);
    }

    [[nodiscard]] bool needs_compaction() const noexcept
    {
        return !can_append(max_cluster_length);
    }

    [[nodiscard]] std::size_t size() const noexcept
    {
        return units_.size();
    }

  private:
    vtparser::raw_byte_vector<char16_t> units_;
};

} // namespace corehost::screen
