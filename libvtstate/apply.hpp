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
    case vtparser::vt_message_id::resize_window:
        applier.apply_resize_window(message, buffer, state);
        return true;
    case vtparser::vt_message_id::use_alternate_buffer:
        applier.apply_alternate_buffer(true, buffer, state);
        return true;
    case vtparser::vt_message_id::use_main_buffer:
        applier.apply_alternate_buffer(false, buffer, state);
        return true;
    case vtparser::vt_message_id::cursor_horiz_absolute:
        applier.apply_cursor_horizontal_absolute(message, state);
        return true;
    case vtparser::vt_message_id::cursor_vert_absolute:
        applier.apply_cursor_vertical_absolute(message, state);
        return true;
    case vtparser::vt_message_id::cpr_response:
        applier.apply_cpr(message, state);
        return true;
    case vtparser::vt_message_id::cursor_up:
        applier.apply_cursor_relative(message, state, 0, -1);
        return true;
    case vtparser::vt_message_id::cursor_down:
        applier.apply_cursor_relative(message, state, 0, 1);
        return true;
    case vtparser::vt_message_id::cursor_forward:
        applier.apply_cursor_relative(message, state, 1, 0);
        return true;
    case vtparser::vt_message_id::cursor_backward:
        applier.apply_cursor_relative(message, state, -1, 0);
        return true;
    case vtparser::vt_message_id::cursor_next_line:
        applier.apply_cursor_relative(message, state, 0, 1);
        state.set_cursor(0, state.cursor().y);
        return true;
    case vtparser::vt_message_id::cursor_prev_line:
        applier.apply_cursor_relative(message, state, 0, -1);
        state.set_cursor(0, state.cursor().y);
        return true;
    case vtparser::vt_message_id::carriage_return:
        state.set_cursor(0, state.cursor().y);
        return true;
    case vtparser::vt_message_id::cursor_forward_tab:
        state.set_cursor(state.next_tab_stop(state.cursor().x), state.cursor().y);
        return true;
    case vtparser::vt_message_id::set_window_title:
        applier.apply_window_title(message, state);
        return true;
    case vtparser::vt_message_id::set_cursor_shape:
        applier.apply_cursor_shape(message, state);
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
    case vtparser::vt_message_id::set_scrolling_region:
        applier.apply_scroll_region(message, state);
        return true;
    case vtparser::vt_message_id::scroll_up:
        applier.apply_scroll(message, buffer, state, true);
        return true;
    case vtparser::vt_message_id::scroll_down:
        applier.apply_scroll(message, buffer, state, false);
        return true;
    case vtparser::vt_message_id::erase_characters:
        applier.apply_erase_characters(message, buffer, state);
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