#pragma once

#include "console_state.hpp"
#include "pseudo_window.hpp"
#include "screen_buffer.hpp"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>

namespace corehost::conptyio
{

enum class pty_signal : std::uint16_t
{
    show_hide_window = 1,
    clear_buffer = 2,
    set_parent = 3,
    resize_window = 8,
};

[[nodiscard]] inline std::size_t payload_size_for(std::uint16_t id) noexcept
{
    switch (static_cast<pty_signal>(id))
    {
    case pty_signal::show_hide_window:
    case pty_signal::clear_buffer:
        return sizeof(std::uint16_t);
    case pty_signal::set_parent:
        return sizeof(std::uint64_t);
    case pty_signal::resize_window:
        return sizeof(std::uint32_t);
    default:
        return 0;
    }
}

// Applies one complete PtySignal payload. No globals, no Windows handles; the
// session pump owns reading and synchronization.
inline void apply_signal(std::uint16_t id,
                         std::span<const std::byte> payload,
                         screen::screen_buffer &buffer,
                         screen::console_state &state,
                         pseudo_window &window)
{
    assert(payload.size() == payload_size_for(id));

    switch (static_cast<pty_signal>(id))
    {
    case pty_signal::clear_buffer:
        buffer.clear(state.default_style);
        state.set_cursor(0, 0);
        break;
    case pty_signal::resize_window:
    {
        std::uint32_t packed = 0;
        std::memcpy(&packed, payload.data(), sizeof(packed));
        const auto width = static_cast<std::uint16_t>(packed & 0xFFFFu);
        const auto height = static_cast<std::uint16_t>(packed >> 16);
        if (width != 0 && height != 0)
        {
            state.resize(width, height);
            buffer.resize(width, height, state.default_style);
        }
        break;
    }
    case pty_signal::show_hide_window:
        {
            std::uint16_t visible = 0;
            std::memcpy(&visible, payload.data(), sizeof(visible));
            window.show(visible != 0);
        }
        break;
    case pty_signal::set_parent:
        {
            std::uint64_t parent = 0;
            std::memcpy(&parent, payload.data(), sizeof(parent));
            window.set_parent(reinterpret_cast<HWND>(parent));
        }
        break;
    default:
        break;
    }
}

} // namespace corehost::conptyio