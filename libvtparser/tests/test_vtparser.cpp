#include "vt_parser.hpp"

#include <cstdio>
#include <cstdlib>

namespace
{

int failures = 0;

void check(bool value, const char *message)
{
    if (!value)
    {
        std::fprintf(stderr, "FAIL: %s\n", message);
        ++failures;
    }
}

} // namespace

int main()
{
    using namespace corehost::vtparser;

    raw_u32_buffer raw;
    vt_parser parser{raw};
    auto result = parser.parse(U"abc");
    check(result.id == vt_message_id::continue_text, "ground text id");
    check(result.consumed == 3, "ground text consumed");
    check(result.message.payload.text == U"abc", "ground text payload");

    parser.reset();
    result = parser.parse(U"\x1b]2;corehost\x07");
    check(result.id == vt_message_id::set_window_title, "title id");
    check(result.message.payload.title == U"corehost", "title payload");

    parser.reset();
    result = parser.parse(U"\x1b[31m");
    check(result.id == vt_message_id::sgr, "sgr id");
    check(result.consumed == 5, "sgr consumed");

    parser.reset();
    result = parser.parse(U"\x1b[?1049h");
    check(result.id == vt_message_id::use_alternate_buffer, "alternate buffer id");

    parser.reset();
    result = parser.parse(U"\x1b]8;id=link1:group;https://example.test/\x07");
    check(result.id == vt_message_id::osc8_hyperlink, "hyperlink id");
    check(result.message.payload.hyperlink.id == U"link1", "hyperlink params id");
    check(result.message.payload.hyperlink.uri == U"https://example.test/", "hyperlink uri");

    parser.reset();
    result = parser.parse(U"\x1b]8;;\x07");
    check(result.id == vt_message_id::osc8_hyperlink, "hyperlink close id");
    check(result.message.payload.hyperlink.uri.empty(), "hyperlink close uri");

    parser.reset();
    result = parser.parse(U"\x1b#3");
    check(result.id == vt_message_id::set_line_rendition, "line rendition id");
    check(result.message.payload.line_rendition == 3, "line rendition payload");

    parser.reset();
    result = parser.parse(U"\x1b]133;D;0\x07");
    check(result.id == vt_message_id::semantic_prompt_mark, "semantic prompt id");
    check(result.message.payload.semantic.kind == u'D', "semantic prompt kind");
    check(result.message.payload.semantic.argument == U"0", "semantic prompt argument");

    parser.reset();
    result = parser.parse(U"\x1b_Gi=1;AAAA\x1b\\");
    check(result.id == vt_message_id::apc_sequence, "apc id");
    check(result.message.payload.text.starts_with(U"\x1b_G"), "apc payload");

    parser.reset();
    result = parser.parse(U"\x1bPqabc\x1b\\");
    check(result.id == vt_message_id::dcs_sequence, "dcs id");
    check(result.message.payload.text.starts_with(U"\x1bP"), "dcs payload");

    if (failures != 0)
    {
        std::fprintf(stderr, "%d failures\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("libvtparser tests passed\n");
    return EXIT_SUCCESS;
}
