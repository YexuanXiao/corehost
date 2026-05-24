// vt_017_resize.cpp — 窗口大小调整
#include "manual_common.hpp"

int main()
{
    HANDLE hOut = init_console();
    title(L"vt-017", L"窗口大小调整 — ESC[8;h;w t\n   测试 ESC[8;30;100t (请求 30行×100列)。\n   期望：发送序列后 WT "
                     L"窗口应调整为接近 30×100。\n   3秒后恢复为 25×80。");

    wprint(L"\x1b[1;32m[期望结果]\x1b[0m\n");
    wprint(L"  调整1: 30行 × 100列 (WT窗口变大)\n");
    wprint(L"  调整2: 25行 × 80列 (恢复到默认)\n");

    sep();
    wprint(L"\x1b[1;32m[实际输出]\x1b[0m\n\n");

    CONSOLE_SCREEN_BUFFER_INFO csbi{};
    GetConsoleScreenBufferInfo(hOut, &csbi);
    wprint(L"  当前尺寸: %d×%d\n", csbi.dwSize.X, csbi.dwSize.Y);

    wprint(L"  发送 ESC[8;30;100t...\n");
    vt(L"\x1b[8;30;100t");
    Sleep(3000);

    GetConsoleScreenBufferInfo(hOut, &csbi);
    wprint(L"  调整后尺寸: %d×%d\n", csbi.dwSize.X, csbi.dwSize.Y);

    wprint(L"  恢复 ESC[8;25;80t...\n");
    vt(L"\x1b[8;25;80t");

    wait3s(L"检查窗口大小变化：100×30 → 80×25");
    return 0;
}
