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

    if (failures != 0)
    {
        std::fprintf(stderr, "%d failures\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("libvtparser tests passed\n");
    return EXIT_SUCCESS;
}
