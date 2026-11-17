#pragma once

#include "session_loop.hpp"

#include "win32/handle.hpp"

#include <cassert>

namespace corehost::corepty
{

// Same external shape as libcorehost's conpty entry, with new native types.
void conpty_entry(win32::handle_view server, win32::handle_view event, win32::handle condrv_input,
                  win32::handle condrv_output, win32::handle_view vt_in, win32::handle_view vt_out,
                  win32::handle signal_pipe, short width, short height, bool inherit_cursor,
                  screen::text_measurement text_measurement, bool ambiguous_is_wide = false)
{
    assert(vt_in.valid());
    assert(vt_out.valid());

    conpty_session_config config;
    config.width = width;
    config.height = height;
    config.inherit_cursor = inherit_cursor;
    config.text_measurement = text_measurement;
    config.ambiguous_is_wide = ambiguous_is_wide;
    run_conpty_session(server, event, condrv_input, condrv_output, vt_in, vt_out, signal_pipe, config);
}

} // namespace corehost::corepty