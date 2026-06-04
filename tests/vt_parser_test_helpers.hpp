#pragma once

#include "vt_parser.hpp"

namespace corehost::conpty::test
{

inline vt_parse_result parse_one(vt_parser &parser, char32_t ch)
{
    return parser.parse({&ch, 1});
}

inline vt_message_id parse_one_id(vt_parser &parser, char32_t ch)
{
    return parse_one(parser, ch).id;
}

inline bool is_parse_continue(vt_message_id id) noexcept
{
    return id == vt_message_id::continue_ || id == vt_message_id::continue_text;
}

inline bool is_parse_continue(const vt_parse_result &result) noexcept
{
    return is_parse_continue(result.id);
}

} // namespace corehost::conpty::test
