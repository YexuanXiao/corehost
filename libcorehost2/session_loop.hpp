#pragma once

#include "session.hpp"

#include "terminal_reader.hpp"
#include "terminal_writer.hpp"
#include "viewport.hpp"

#include "win32/handle.hpp"

#include <cstddef>
#include <span>

namespace corehost::corehost2
{

struct conpty_session_config final
{
    short width{};
    short height{};
    bool inherit_cursor{};
    screen::text_measurement text_measurement{screen::text_measurement::graphemes};
    bool ambiguous_is_wide{};
    unsigned long attached_process_id{};
};

// Minimal vt_in -> parser -> libscreen -> vt_out loop. ConDrv server and signal
// handling are added by later modules; this establishes the new session entry.
inline void run_conpty_session(win32::handle_view vt_in,
                               win32::handle_view vt_out,
                               const conpty_session_config &config)
{
    const auto width = config.width > 0 ? static_cast<std::size_t>(config.width) : std::size_t{120};
    const auto height = config.height > 0 ? static_cast<std::size_t>(config.height) : std::size_t{30};

    session state{width, height};
    conptyio::terminal_reader reader{vt_in};
    conptyio::terminal_writer writer{vt_out};

    while (reader.read_batch())
    {
        state.feed(reader.codepoints());

        const auto cursor = screen::coord{
            static_cast<std::int32_t>(state.state().cursor().x),
            static_cast<std::int32_t>(state.state().cursor().y),
        };
        vtserializer::render_damage(writer.buffer(), state.screen(), cursor);
        state.screen().clear_damage();

        if (!writer.flush())
        {
            break;
        }
    }
}

} // namespace corehost::corehost2