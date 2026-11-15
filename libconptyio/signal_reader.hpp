#pragma once

#include "signal.hpp"

#include "win32/handle.hpp"
#include "win32/io.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace corehost::conptyio
{

// Reads one complete PtySignal message: 2-byte id followed by its fixed
// payload. The payload buffer is retained across reads.
class signal_reader final
{
  public:
    explicit signal_reader(win32::handle_view pipe) noexcept : pipe_{pipe} {}

    [[nodiscard]] bool read_one()
    {
        if (!win32::read_exact(pipe_, std::span<std::byte>{reinterpret_cast<std::byte *>(&id_), sizeof(id_)}))
        {
            return false;
        }

        const auto size = payload_size_for(id_);
        if (size == 0 || size > payload_.size())
        {
            return false;
        }

        if (!win32::read_exact(pipe_, std::span<std::byte>{payload_.data(), size}))
        {
            return false;
        }
        size_ = size;
        return true;
    }

    [[nodiscard]] std::uint16_t id() const noexcept
    {
        return id_;
    }

    [[nodiscard]] std::span<const std::byte> payload() const noexcept
    {
        return std::span<const std::byte>{payload_.data(), size_};
    }

  private:
    win32::handle_view pipe_;
    std::uint16_t id_{};
    std::array<std::byte, sizeof(std::uint64_t)> payload_{};
    std::size_t size_{};
};

} // namespace corehost::conptyio