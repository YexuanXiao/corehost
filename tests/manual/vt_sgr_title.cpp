#include "manual_common.hpp"

int wmain()
{
    init_manual_console();
    section(L"manual_003_vt_sgr_title", L"Exercises SGR colors/attributes and OSC title setting.");

    vt(L"\x1b]0;manual vt sgr title\x07");
    wprint(L"Title was set with OSC 0.\n\n");

    vt(L"\x1b[0m");
    wprint(L"Normal text\n");
    vt(L"\x1b[1;31m");
    wprint(L"Bold red foreground\n");
    vt(L"\x1b[4;32m");
    wprint(L"Underlined green foreground\n");
    vt(L"\x1b[7;33m");
    wprint(L"Reverse-video yellow foreground\n");
    vt(L"\x1b[38;2;255;128;0;48;2;20;20;20m");
    wprint(L"RGB orange foreground on dark background\n");
    vt(L"\x1b[0m");

    check_line(L"The window/tab title and four styled lines should update.",
               L"16-color SGR, RGB SGR, reset and OSC title are visible.");
    pause_briefly();
    return 0;
}
