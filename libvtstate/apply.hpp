#pragma once

#include "vtstate.hpp"

namespace corehost::vtstate
{

// Applies the parser messages implemented so far. Returns false when the
// message is not a screen/console state change handled by this module.
[[nodiscard]] inline bool apply(vtparser::vt_message_id id,
                                const vtparser::vt_message &message,
                                screen::screen_buffer &buffer,
                                screen::console_state &state,
                                state_applier &applier)
{
    switch (id)
    {
    case vtparser::vt_message_id::continue_text:
    case vtparser::vt_message_id::text:
        applier.apply_text(message, buffer, state);
        return true;
    case vtparser::vt_message_id::cursor_position:
        applier.apply_cursor_position(message, state);
        return true;
    case vtparser::vt_message_id::line_feed:
        applier.apply_line_feed(buffer, state);
        return true;
    case vtparser::vt_message_id::set_line_rendition:
        applier.apply_line_rendition(message, buffer, state);
        return true;
    case vtparser::vt_message_id::osc8_hyperlink:
        applier.apply_hyperlink(message, buffer, state);
        return true;
    case vtparser::vt_message_id::semantic_prompt_mark:
        applier.apply_semantic_prompt(message, buffer, state);
        return true;
    case vtparser::vt_message_id::erase_in_line:
        applier.apply_erase_in_line(message, buffer, state);
        return true;
    case vtparser::vt_message_id::erase_in_display:
        applier.apply_erase_in_display(message, buffer, state);
        return true;
    default:
        return false;
    }
}

} // namespace corehost::vtstate