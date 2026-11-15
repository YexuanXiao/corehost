#include "session.hpp"

#include <cstdio>
#include <cstdlib>

int main()
{
    corehost::corehost2::session session{8, 2};
    constexpr char32_t input[]{U'A', U'B'};
    session.feed(input);

    if (session.screen().at(0, 0).primary() != u'A' ||
        session.screen().at(1, 0).primary() != u'B')
    {
        std::fprintf(stderr, "session text failed\n");
        return EXIT_FAILURE;
    }
    if (session.state().cursor().x != 2)
    {
        std::fprintf(stderr, "session cursor failed\n");
        return EXIT_FAILURE;
    }

    std::printf("libcorehost2 tests passed\n");
    return EXIT_SUCCESS;
}