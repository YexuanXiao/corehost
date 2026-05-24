// vt_008_cursor_style.cpp — 光标显示/隐藏/形状 DECSCUSR
#include "manual_common.hpp"

int main()
{
    HANDLE hOut = init_console();
    title(L"vt-008", L"光标显示/隐藏/形状 (DECSCUSR)\n   测试 ESC[?25l (隐藏光标), ESC[?25h (显示光标),\n   以及 "
                     L"ESC[Ps q (DECSCUSR 光标形状)。\n   期望：光标先隐藏再显示。形状依次切换：\n   "
                     L"0=默认,1=闪烁块,2=稳态块,3=闪烁下划线,4=稳态下划线,5=闪烁竖线,6=稳态竖线。");

    wprint(L"\x1b[1;32m[期望结果]\x1b[0m\n");
    wprint(L"  隐藏→恢复→7 种光标形状 (每种持续约 1s)\n");

    sep();
    wprint(L"\x1b[1;32m[实际输出]\x1b[0m\n\n");

    wprint(L"  隐藏光标 → ");
    vt(L"\x1b[?25l");
    Sleep(800);
    wprint(L"(隐藏) → 显示 → ");
    vt(L"\x1b[?25h");
    wprint(L"(恢复)\n");

    wprint(L"  DECSCUSR 0 (默认): ");
    vt(L"\x1b[0 q");
    Sleep(1000);
    wprint(L"\n  DECSCUSR 1 (闪烁块): ");
    vt(L"\x1b[1 q");
    Sleep(1000);
    wprint(L"\n  DECSCUSR 2 (稳态块): ");
    vt(L"\x1b[2 q");
    Sleep(1000);
    wprint(L"\n  DECSCUSR 3 (闪烁下划线): ");
    vt(L"\x1b[3 q");
    Sleep(1000);
    wprint(L"\n  DECSCUSR 4 (稳态下划线): ");
    vt(L"\x1b[4 q");
    Sleep(1000);
    wprint(L"\n  DECSCUSR 5 (闪烁竖线): ");
    vt(L"\x1b[5 q");
    Sleep(1000);
    wprint(L"\n  DECSCUSR 6 (稳态竖线): ");
    vt(L"\x1b[6 q");
    Sleep(1000);
    vt(L"\x1b[0 q");
    wprint(L"\n  恢复默认\n");

    wait3s(L"检查光标形状变化：块/下划线/竖线");
    return 0;
}
