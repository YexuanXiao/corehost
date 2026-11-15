#pragma once

#include "input.hpp"

namespace corehost::consoleinput
{

inline std::optional<key_event>
key_from_message(vtparser::vt_message_id id, const vtparser::vt_message &message) noexcept
{
    if (id == vtparser::vt_message_id::win32_input_key)
    {
        return key_event{
            message.payload.win32_key.uc,
            static_cast<virtual_key>(message.payload.win32_key.vk),
            message.payload.win32_key.sc,
            message.payload.win32_key.control_state,
            message.payload.win32_key.repeat_count,
            message.payload.win32_key.key_down,
        };
    }

    key_event event;
    switch (id)
    {
    case vtparser::vt_message_id::key_up:
        event.key = virtual_key::up;
        break;
    case vtparser::vt_message_id::key_down:
        event.key = virtual_key::down;
        break;
    case vtparser::vt_message_id::key_left:
        event.key = virtual_key::left;
        break;
    case vtparser::vt_message_id::key_right:
        event.key = virtual_key::right;
        break;
    case vtparser::vt_message_id::key_home:
        event.key = virtual_key::home;
        break;
    case vtparser::vt_message_id::key_end:
        event.key = virtual_key::end;
        break;
    case vtparser::vt_message_id::key_insert:
        event.key = virtual_key::insert;
        break;
    case vtparser::vt_message_id::key_delete:
        event.key = virtual_key::del;
        break;
    case vtparser::vt_message_id::key_page_up:
        event.key = virtual_key::page_up;
        break;
    case vtparser::vt_message_id::key_page_down:
        event.key = virtual_key::page_down;
        break;
    case vtparser::vt_message_id::key_f1:
        event.key = virtual_key::f1;
        break;
    case vtparser::vt_message_id::key_f2:
        event.key = virtual_key::f2;
        break;
    case vtparser::vt_message_id::key_f3:
        event.key = virtual_key::f3;
        break;
    case vtparser::vt_message_id::key_f4:
        event.key = virtual_key::f4;
        break;
    case vtparser::vt_message_id::key_f5:
        event.key = virtual_key::f5;
        break;
    case vtparser::vt_message_id::key_f6:
        event.key = virtual_key::f6;
        break;
    case vtparser::vt_message_id::key_f7:
        event.key = virtual_key::f7;
        break;
    case vtparser::vt_message_id::key_f8:
        event.key = virtual_key::f8;
        break;
    case vtparser::vt_message_id::key_f9:
        event.key = virtual_key::f9;
        break;
    case vtparser::vt_message_id::key_f10:
        event.key = virtual_key::f10;
        break;
    case vtparser::vt_message_id::key_f11:
        event.key = virtual_key::f11;
        break;
    case vtparser::vt_message_id::key_f12:
        event.key = virtual_key::f12;
        break;
    default:
        return std::nullopt;
    }
    return event;
}

} // namespace corehost::consoleinput