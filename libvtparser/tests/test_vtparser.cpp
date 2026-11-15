#include "kitty.hpp"
#include "sixel.hpp"
#include "utf8.hpp"
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

    raw_u16_buffer raw;
    vt_parser parser{raw};
    auto result = parser.parse(u"abc");
    check(result.id == vt_message_id::continue_text, "ground text id");
    check(result.consumed == 3, "ground text consumed");
    check(result.message.payload.text == u"abc", "ground text payload");

    parser.reset();
    result = parser.parse(u"\x1b]2;corehost\x07");
    check(result.id == vt_message_id::set_window_title, "title id");
    check(result.message.payload.title == u"corehost", "title payload");

    parser.reset();
    result = parser.parse(u"\x1b[31m");
    check(result.id == vt_message_id::sgr, "sgr id");
    check(result.consumed == 5, "sgr consumed");

    parser.reset();
    result = parser.parse(u"\x1b[?1049h");
    check(result.id == vt_message_id::use_alternate_buffer, "alternate buffer id");

    parser.reset();
    result = parser.parse(u"\x1b]8;id=link1:group;https://example.test/\x07");
    check(result.id == vt_message_id::osc8_hyperlink, "hyperlink id");
    check(result.message.payload.hyperlink.id == u"link1", "hyperlink params id");
    check(result.message.payload.hyperlink.uri == u"https://example.test/", "hyperlink uri");

    parser.reset();
    result = parser.parse(u"\x1b]8;;\x07");
    check(result.id == vt_message_id::osc8_hyperlink, "hyperlink close id");
    check(result.message.payload.hyperlink.uri.empty(), "hyperlink close uri");

    parser.reset();
    result = parser.parse(u"\x1b#3");
    check(result.id == vt_message_id::set_line_rendition, "line rendition id");
    check(result.message.payload.line_rendition == 3, "line rendition payload");

    parser.reset();
    result = parser.parse(u"\x1b]133;D;0\x07");
    check(result.id == vt_message_id::semantic_prompt_mark, "semantic prompt id");
    check(result.message.payload.semantic.kind == u'D', "semantic prompt kind");
    check(result.message.payload.semantic.argument == u"0", "semantic prompt argument");

    parser.reset();
    result = parser.parse(u"\x1b_Gi=1;AAAA\x1b\\");
    check(result.id == vt_message_id::apc_sequence, "apc id");
    check(result.message.payload.text.starts_with(u"\x1b_G"), "apc payload");

    parser.reset();
    result = parser.parse(u"\x1bPqabc\x1b\\");
    check(result.id == vt_message_id::dcs_sequence, "dcs id");
    check(result.message.payload.text.starts_with(u"\x1bP"), "dcs payload");

    const auto image = parse_kitty_graphics(u"\x1b_Ga=T,f=100,s=2,v=3,i=7;AAAA\x1b\\");
    check(image.has_value(), "kitty image parsed");
    if (image.has_value())
    {
        check(image->action == u'T', "kitty action");
        check(image->image_id == 7, "kitty id");
        check(image->width == 2 && image->height == 3, "kitty dimensions");
        check(image->payload == u"AAAA", "kitty payload");
    }

    const auto sixel = parse_sixel(u"\x1bP0;1qabc\x1b\\");
    check(sixel.has_value(), "sixel parsed");
    if (sixel.has_value())
    {
        check(sixel->parameters == u"0;1", "sixel parameters");
        check(sixel->data == u"abc", "sixel data");
    }

    std::u16string decoded;
    std::string pending;
    constexpr char8_t utf8_text[]{0x41, 0xE4, 0xB8, 0xAD};
    decode_utf8(utf8_text, decoded, pending);
    check(decoded == u"A\u4E2D", "utf8 decode");
    check(pending.empty(), "utf8 pending empty");

    if (failures != 0)
    {
        std::fprintf(stderr, "%d failures\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("libvtparser tests passed\n");
    return EXIT_SUCCESS;
}
