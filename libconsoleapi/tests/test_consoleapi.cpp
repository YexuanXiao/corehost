#include "console_api.hpp"

#include <cstdio>
#include <cstdlib>

int main()
{
    corehost::screen::screen_buffer buffer{8, 2};
    corehost::screen::console_state state{8, 2};
    corehost::consoleapi::console_api api{buffer, state};

    if (!api.write_text(u"AB"))
    {
        std::fprintf(stderr, "write_text failed\n");
        return EXIT_FAILURE;
    }
    if (buffer.at(0, 0).primary() != u'A' || state.cursor().x != 2)
    {
        std::fprintf(stderr, "write_text state failed\n");
        return EXIT_FAILURE;
    }

    corehost::interop::win32::char_info cells[2]{};
    if (api.read_cells(corehost::screen::rect{0, 0, 1, 0}, cells) != 2)
    {
        std::fprintf(stderr, "read_cells failed\n");
        return EXIT_FAILURE;
    }
    if (cells[0].char_data.unicode_char != u'A' || cells[1].char_data.unicode_char != u'B')
    {
        std::fprintf(stderr, "read_cells data failed\n");
        return EXIT_FAILURE;
    }

    api.fill_cells(corehost::screen::rect{2, 0, 4, 0}, u'X', 0x1F);
    if (buffer.at(2, 0).primary() != u'X' ||
        buffer.attr_at(corehost::screen::coord{4, 0}) != 0x1F)
    {
        std::fprintf(stderr, "fill_cells failed\n");
        return EXIT_FAILURE;
    }

    (void)buffer.write_scalar(0, 1, u'A', corehost::screen::cell_style{});
    api.scroll_cells(corehost::screen::rectangle{0, 1, 1, 1},
                     corehost::screen::rectangle{}, false, corehost::screen::coord{1, 1});
    if (buffer.at(1, 1).primary() != u'A')
    {
        std::fprintf(stderr, "scroll_cells failed\n");
        return EXIT_FAILURE;
    }

    std::printf("libconsoleapi tests passed\n");
    return EXIT_SUCCESS;
}