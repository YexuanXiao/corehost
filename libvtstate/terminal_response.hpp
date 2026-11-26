#pragma once

#include "console_state.hpp"
#include "control.hpp"
#include "vt_parser.hpp"

namespace corehost::vtstate
{

// Builds the terminal's reply to a VT query. This is the single place that
// maps a parsed query message to its reply; callers must not inspect raw
// sequences or maintain their own query dispatch.
template <typename String>
[[nodiscard]] bool append_terminal_response(vtparser::vt_message_id id, const screen::console_state &state,
                                            String &output)
{
    switch (id)
    {
    case vtparser::vt_message_id::report_cursor_position:
        vtserializer::append_cursor_position_response(output, static_cast<unsigned>(state.cursor().y),
                                                      static_cast<unsigned>(state.cursor().x));
        return true;
    case vtparser::vt_message_id::device_attributes:
        vtserializer::append_device_attributes_response(output);
        return true;
    default:
        return false;
    }
}

} // namespace corehost::vtstate
