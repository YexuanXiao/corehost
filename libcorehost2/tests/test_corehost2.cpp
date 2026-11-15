#include "conpty.hpp"
#include "conpty.hpp"
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

    HANDLE input_read = nullptr;
    HANDLE input_write = nullptr;
    if (!::CreatePipe(&input_read, &input_write, nullptr, 0))
    {
        std::fprintf(stderr, "create input pipe failed\n");
        return EXIT_FAILURE;
    }
    ::CloseHandle(input_write);

    HANDLE output = ::CreateFileW(L"NUL", GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
    if (output == INVALID_HANDLE_VALUE)
    {
        std::fprintf(stderr, "open NUL failed\n");
        return EXIT_FAILURE;
    }

    corehost::corehost2::conpty_entry(win32::handle_view{}, win32::handle_view{}, {}, {},
                                      win32::handle_view{input_read}, win32::handle_view{output}, {},
                                      8, 2, false, corehost::screen::text_measurement::console);

    ::CloseHandle(output);
    ::CloseHandle(input_read);

    std::printf("libcorehost2 tests passed\n");
    return EXIT_SUCCESS;
}