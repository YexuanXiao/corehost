#include "manual_common.hpp"

int wmain()
{
    init_manual_console();
    section(L"manual_002_vt_cursor_erase",
            L"Exercises VT cursor movement, save/restore, line erase and display erase.");

    wprint(L"Top line remains visible.\n");
    vt(L"\x1b[6;10H");
    wprint(L"A at row 6 col 10");
    vt(L"\x1b" L"7");
    vt(L"\x1b[8;20H");
    wprint(L"B at row 8 col 20");
    vt(L"\x1b" L"8");
    wprint(L" + restored cursor text");

    vt(L"\x1b[10;1H");
    wprint(L"This line will be partially erased after the marker >>> keep");
    vt(L"\x1b[10;49H\x1b[K");
    vt(L"\x1b[12;1H");
    wprint(L"The screen was cleared at start with ED 2; this line uses CUP.");

    check_line(L"Look at rows 6, 8, 10 and 12.",
               L"Row 6 contains restored cursor text, row 8 contains B, row 10 stops after >>>.");
    pause_briefly();
    return 0;
}
