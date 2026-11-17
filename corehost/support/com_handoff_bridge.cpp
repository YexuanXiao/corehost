#include "com_handoff_bridge.hpp"

#include "comserver/com_server.hpp"

#include <cstdint>

namespace corehost::support
{

bool acquire_com_handoff(com_handoff_raw &result) noexcept
{
    auto handoff = corehost::comserver::com_server_entry();
    if (!handoff.server.valid())
    {
        return false;
    }

    result.server = reinterpret_cast<std::uintptr_t>(handoff.server.get());
    result.event = reinterpret_cast<std::uintptr_t>(handoff.event.get());
    result.condrv_input = reinterpret_cast<std::uintptr_t>(handoff.condrv_input.get());
    result.condrv_output = reinterpret_cast<std::uintptr_t>(handoff.condrv_output.get());
    result.vt_in = reinterpret_cast<std::uintptr_t>(handoff.vt_in.get());
    result.vt_out = reinterpret_cast<std::uintptr_t>(handoff.vt_out.get());
    result.signal = reinterpret_cast<std::uintptr_t>(handoff.signal.get());
    result.width = handoff.width;
    result.height = handoff.height;

    // The raw handles are now owned by the caller.
    (void)handoff.server.release();
    (void)handoff.event.release();
    (void)handoff.condrv_input.release();
    (void)handoff.condrv_output.release();
    (void)handoff.vt_in.release();
    (void)handoff.vt_out.release();
    (void)handoff.signal.release();
    return true;
}

} // namespace corehost::support