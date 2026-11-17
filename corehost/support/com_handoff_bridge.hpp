#pragma once

#include <cstdint>

namespace corehost::support
{

// Raw handles from libcorepty1's COM handoff. Ownership is transferred to the
// caller; the values are wrapped by libcorepty2's win32::handle at the entry.
struct com_handoff_raw final
{
    std::uintptr_t server{};
    std::uintptr_t event{};
    std::uintptr_t condrv_input{};
    std::uintptr_t condrv_output{};
    std::uintptr_t vt_in{};
    std::uintptr_t vt_out{};
    std::uintptr_t signal{};
    short width{};
    short height{};
};

[[nodiscard]] bool acquire_com_handoff(com_handoff_raw &result) noexcept;

} // namespace corehost::support