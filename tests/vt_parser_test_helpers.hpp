#pragma once

#include "conpty_vt_parser.hpp"

namespace conpty::test
{

inline void reset_test_vt_parser_message(vt_parser &parser, vt_message_id id)
{
    switch (id)
    {
    case vt_message_id::cursor_up:
        parser.reset<vt_message_id::cursor_up>();
        break;
    case vt_message_id::cursor_down:
        parser.reset<vt_message_id::cursor_down>();
        break;
    case vt_message_id::cursor_forward:
        parser.reset<vt_message_id::cursor_forward>();
        break;
    case vt_message_id::cursor_backward:
        parser.reset<vt_message_id::cursor_backward>();
        break;
    case vt_message_id::cursor_next_line:
        parser.reset<vt_message_id::cursor_next_line>();
        break;
    case vt_message_id::cursor_prev_line:
        parser.reset<vt_message_id::cursor_prev_line>();
        break;
    case vt_message_id::scroll_up:
        parser.reset<vt_message_id::scroll_up>();
        break;
    case vt_message_id::scroll_down:
        parser.reset<vt_message_id::scroll_down>();
        break;
    case vt_message_id::insert_characters:
        parser.reset<vt_message_id::insert_characters>();
        break;
    case vt_message_id::delete_characters:
        parser.reset<vt_message_id::delete_characters>();
        break;
    case vt_message_id::erase_characters:
        parser.reset<vt_message_id::erase_characters>();
        break;
    case vt_message_id::insert_lines:
        parser.reset<vt_message_id::insert_lines>();
        break;
    case vt_message_id::delete_lines:
        parser.reset<vt_message_id::delete_lines>();
        break;
    case vt_message_id::cursor_forward_tab:
        parser.reset<vt_message_id::cursor_forward_tab>();
        break;
    case vt_message_id::cursor_backward_tab:
        parser.reset<vt_message_id::cursor_backward_tab>();
        break;
    case vt_message_id::cursor_vert_absolute:
        parser.reset<vt_message_id::cursor_vert_absolute>();
        break;
    case vt_message_id::cursor_horiz_absolute:
        parser.reset<vt_message_id::cursor_horiz_absolute>();
        break;
    case vt_message_id::cursor_position:
        parser.reset<vt_message_id::cursor_position>();
        break;
    case vt_message_id::set_cursor_shape:
        parser.reset<vt_message_id::set_cursor_shape>();
        break;
    case vt_message_id::erase_in_display:
        parser.reset<vt_message_id::erase_in_display>();
        break;
    case vt_message_id::erase_in_line:
        parser.reset<vt_message_id::erase_in_line>();
        break;
    case vt_message_id::set_palette_color:
        parser.reset<vt_message_id::set_palette_color>();
        break;
    case vt_message_id::set_scrolling_region:
        parser.reset<vt_message_id::set_scrolling_region>();
        break;
    case vt_message_id::set_columns_132:
        parser.reset<vt_message_id::set_columns_132>();
        break;
    case vt_message_id::set_columns_80:
        parser.reset<vt_message_id::set_columns_80>();
        break;
    case vt_message_id::resize_window:
        parser.reset<vt_message_id::resize_window>();
        break;
    case vt_message_id::win32_input_key:
        parser.reset<vt_message_id::win32_input_key>();
        break;
    case vt_message_id::cpr_response:
        parser.reset<vt_message_id::cpr_response>();
        break;
    case vt_message_id::sgr:
        parser.reset<vt_message_id::sgr>();
        break;
    case vt_message_id::text:
        parser.reset<vt_message_id::text>();
        break;
    case vt_message_id::unknown_sequence:
        parser.reset<vt_message_id::unknown_sequence>();
        break;
    case vt_message_id::continue_text:
        parser.reset<vt_message_id::continue_text>();
        break;
    case vt_message_id::continue_:
    default:
        parser.reset<vt_message_id::continue_>();
        break;
    }
}

} // namespace conpty::test
