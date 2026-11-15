#pragma once

#include "condrv_io.hpp"

#include "win32/handle.hpp"

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <span>
#include <vector>

namespace corehost::coreio
{

// Reads the full variable-length input payload of an io_msg. ConDrv may place
// payload bytes beyond the local io_msg body; those bytes are fetched with
// IOCTL_READ_INPUT. Returned spans remain valid until the next read().
class input_payload_reader final
{
  public:
    explicit input_payload_reader(win32::handle_view server) noexcept : server_{server} {}

    [[nodiscard]] std::span<const std::byte> read(io_msg &message, std::size_t offset)
    {
        const auto total = static_cast<std::size_t>(message.descriptor.InputSize);
        if (offset >= total || offset >= sizeof(message.body))
        {
            return {};
        }

        const auto local = std::min(total - offset, sizeof(message.body) - offset);
        if (local == total - offset)
        {
            return std::span<const std::byte>{
                reinterpret_cast<const std::byte *>(message.body) + offset, local};
        }

        buffer_.resize(total - offset);
        std::memcpy(buffer_.data(), message.body + offset, local);
        read_input(server_, message.descriptor.Identifier, static_cast<ULONG>(offset + local),
                   std::span<BYTE>{reinterpret_cast<BYTE *>(buffer_.data() + local),
                                   buffer_.size() - local});
        return buffer_;
    }

  private:
    win32::handle_view server_;
    std::vector<std::byte> buffer_;
};

} // namespace corehost::coreio
