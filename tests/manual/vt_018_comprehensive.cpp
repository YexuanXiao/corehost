// vt_018_comprehensive.cpp — 综合：彩色框 + 进度条
#include "manual_common.hpp"

int main()
{
    HANDLE hOut = init_console();
    title(L"vt-018", L"综合演示 — 彩色框 + 进度条\n   测试 256 色 + CUP + 文本结合的综合场景。\n   期望：5 "
                     L"行渐变色矩阵 (41×5)，每种颜色硬件索引递增。\n   随后 10 步进度条动画 (0→100%%)，每步 0.3 秒。\n "
                     L"  进度条使用绿底 + 灰色背景。");

    wprint(L"\x1b[1;32m[期望结果]\x1b[0m\n");
    wprint(L"  上部: 5行渐变色矩阵, 每格不同颜色\n");
    wprint(L"  下部: 进度条从 [          ] 0%% 到 [██████████] 100%%\n");

    sep();
    wprint(L"\x1b[1;32m[实际输出]\x1b[0m\n\n");

    // 彩色框
    wprint(L"  ┌─────────────────────────────────────────┐\n");
    for (int r = 0; r < 5; ++r)
    {
        wprint(L"  │");
        for (int c = 0; c < 41; ++c)
        {
            int hue = (c * 6 + r * 30) % 216 + 16;
            wprint(L"\x1b[48;5;%dm \x1b[0m", hue);
        }
        wprint(L"│\n");
    }
    wprint(L"  └─────────────────────────────────────────┘\n");

    // 进度条
    wprint(L"\n  进度条:\n");
    for (int p = 0; p <= 100; p += 10)
    {
        int bar = p / 2;
        wprint(L"  [\x1b[42m");
        for (int i = 0; i < bar; ++i)
            vt(L" ");
        vt(L"\x1b[0m");
        for (int i = bar; i < 50; ++i)
            vt(L" ");
        wprint(L"] %3d%%\n", p);
        Sleep(300);
    }

    wait3s(L"检查彩色渐变矩阵和进度条动画");
    return 0;
}
