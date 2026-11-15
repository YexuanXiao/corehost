#include "vtstate.hpp"

#include <cstdio>
#include <cstdlib>

int main()
{
    namespace vtparser = corehost::vtparser;
    namespace screen = corehost::screen;

    vtparser::raw_u32_buffer raw;
    vtparser::vt_parser parser{raw};
    auto result = parser.parse(U"AB\u4E2D");
    if (result.id != vtparser::vt_message_id::continue_text)
    {
        std::fprintf(stderr, "unexpected parse result\n");
        return EXIT_FAILURE;
    }

    screen::screen_buffer buffer{10, 2};
    screen::console_state state{10, 2};
    corehost::vtstate::state_applier applier;
    applier.apply_text(result.message, buffer, state);

    if (buffer.at(0, 0).primary() != u'A' || buffer.at(1, 0).primary() != u'B')
    {
        std::fprintf(stderr, "text cells wrong\n");
        return EXIT_FAILURE;
    }
    if (!buffer.at(2, 0).is_wide() || !buffer.at(3, 0).is_continuation())
    {
        std::fprintf(stderr, "wide cells wrong\n");
        return EXIT_FAILURE;
    }
    if (state.cursor().x != 4 || state.cursor().y != 0)
    {
        std::fprintf(stderr, "cursor wrong\n");
        return EXIT_FAILURE;
    }
    if (applier.last_consumed() != 3)
    {
        std::fprintf(stderr, "consumed wrong\n");
        return EXIT_FAILURE;
    }

    std::printf("libvtstate tests passed\n");
    return EXIT_SUCCESS;
}