#include "manual_common.hpp"

int wmain()
{
    init_manual_console();
    section(L"manual_005_unicode_width",
            L"Exercises Unicode output width: ASCII, CJK full-width, combining marks, emoji and line wrapping.");

    wprint(L"ASCII boundary: ");
    for (int i = 0; i < 60; ++i)
        wprint(L"=");
    wprint(L"\n");

    wprint(L"CJK full-width: ");
    for (int i = 0; i < 20; ++i)
        wprint(L"测");
    wprint(L"\n");

    wprint(L"Combining mark: cafe\u0301 should render as one accented word.\n");
    wprint(L"Emoji and VS16: text \U0001F600 \U0001F469\u200D\U0001F4BB \u2600\uFE0F end\n");

    wprint(L"Wrap check: ");
    for (int i = 0; i < 50; ++i)
        wprint((i % 2) ? L"中" : L"A");
    wprint(L"\n");

    check_line(L"CJK and emoji should not leave broken trailing cells or corrupt following text.",
               L"Wide cells, combining marks, surrogate pairs and grapheme-width handling look coherent.");
    pause_briefly();
    return 0;
}
