#pragma once

#include "input.hpp"
#include <Windows.h>

namespace corehost::consoleinput
{

[[nodiscard]] inline key_event key_from_character(char16_t character, bool key_down = true) noexcept
{
    key_event event;
    event.character = character;
    event.key_down = key_down;
    if (character != 0)
    {
        const auto scan = ::VkKeyScanW(static_cast<wchar_t>(character));
        if (scan != -1)
        {
            event.key = static_cast<virtual_key>(LOBYTE(scan));
            const auto shift = HIBYTE(scan);
            event.control_state = (shift & 1) != 0 ? 0x0010 : 0;
            event.control_state |= (shift & 2) != 0 ? 0x0008 : 0;
            event.control_state |= (shift & 4) != 0 ? 0x0004 : 0;
            event.scan_code = static_cast<std::uint16_t>(
                ::MapVirtualKeyW(LOBYTE(scan), MAPVK_VK_TO_VSC));
        }
    }
    return event;
}

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
    case vtparser::vt_message_id::key_ctrl_up:
        event.key = virtual_key::up;
        event.control_state = 0x0008;
        break;
    case vtparser::vt_message_id::key_ctrl_down:
        event.key = virtual_key::down;
        event.control_state = 0x0008;
        break;
    case vtparser::vt_message_id::key_ctrl_left:
        event.key = virtual_key::left;
        event.control_state = 0x0008;
        break;
    case vtparser::vt_message_id::key_ctrl_right:
        event.key = virtual_key::right;
        event.control_state = 0x0008;
        break;
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

[[nodiscard]] inline std::optional<mouse_event>
mouse_from_message(vtparser::vt_message_id id, const vtparser::vt_message &message) noexcept
{
    if (id != vtparser::vt_message_id::mouse_sgr)
    {
        return std::nullopt;
    }
    mouse_event event;
    event.x = message.payload.mouse.x;
    event.y = message.payload.mouse.y;
    const auto encoding = message.payload.mouse.encoding;
    if (!message.payload.mouse.release && (encoding & 0x40) == 0)
    {
        switch (encoding & 0x03)
        {
        case 0:
            event.button_state = 0x0001;
            break;
        case 1:
            event.button_state = 0x0004;
            break;
        case 2:
            event.button_state = 0x0002;
            break;
        default:
            break;
        }
    }
    if ((encoding & 0x04) != 0)
    {
        event.control_state |= 0x0010;
    }
    if ((encoding & 0x08) != 0)
    {
        event.control_state |= 0x0002;
    }
    if ((encoding & 0x10) != 0)
    {
        event.control_state |= 0x0008;
    }
    if ((encoding & 0x20) != 0)
    {
        event.event_flags |= 0x0001;
    }
    if ((encoding & 0x40) != 0)
    {
        event.event_flags |= 0x0004;
    }
    return event;
}

} // namespace corehost::consoleinput