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
    case vtparser::vt_message_id::use_alternate_buffer:
        applier.apply_alternate_buffer(true, buffer, state);
        return true;
    case vtparser::vt_message_id::use_main_buffer:
        applier.apply_alternate_buffer(false, buffer, state);
        return true;
    case vtparser::vt_message_id::cursor_show:
        applier.apply_cursor_visibility(true, state);
        return true;
    case vtparser::vt_message_id::cursor_hide:
        applier.apply_cursor_visibility(false, state);
        return true;
    case vtparser::vt_message_id::save_cursor:
    case vtparser::vt_message_id::ansi_save_cursor:
        applier.apply_save_cursor(state);
        return true;
    case vtparser::vt_message_id::restore_cursor:
    case vtparser::vt_message_id::ansi_restore_cursor:
        applier.apply_restore_cursor(state);
        return true;
    case vtparser::vt_message_id::set_palette_color:
        applier.apply_palette_color(message, state);
        return true;
    case vtparser::vt_message_id::insert_lines:
        applier.apply_insert_lines(message, buffer, state);
        return true;
    case vtparser::vt_message_id::delete_lines:
        applier.apply_delete_lines(message, buffer, state);
        return true;
    case vtparser::vt_message_id::insert_characters:
        applier.apply_insert_characters(message, buffer, state);
        return true;
    case vtparser::vt_message_id::delete_characters:
        applier.apply_delete_characters(message, buffer, state);
        return true;
    case vtparser::vt_message_id::sgr:
        applier.apply_sgr(message, buffer, state);
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