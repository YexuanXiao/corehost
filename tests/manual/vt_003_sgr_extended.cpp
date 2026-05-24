// vt_003_sgr_extended.cpp — 256 色 + True Color
#include "manual_common.hpp"

int main()
{
    HANDLE hOut = init_console();
    title(L"vt-003", L"SGR — 256 色 + True Color (RGB)\n   测试 ESC[38;5;Nm (256色前景) 和 ESC[48;5;Nm (256色背景)\n   "
                     L"以及 ESC[38;2;R;G;Bm / ESC[48;2;R;G;Bm (24位真彩色)。\n   期望：16 个系统色 (0-15)、36 个连续色 "
                     L"(196-231)、\n   3 行 True Color RGB 前景和背景。颜色应平滑过渡。");

    wprint(L"\x1b[1;32m[期望结果]\x1b[0m\n");
    wprint(L"  行1: 16 个系统色 (256色索引 0-15)\n");
    wprint(L"  行2: 36 个连续色块 (256色索引 196-231)\n");
    wprint(L"  行3: RGB(255,128,64) 暖橙色文本\n");
    wprint(L"  行4: RGB(64,128,255) 浅蓝色文本\n");
    wprint(L"  行5-7: RGB 背景色条 (红/黄/蓝)\n");

    sep();
    wprint(L"\x1b[1;32m[实际输出]\x1b[0m\n\n");

    wprint(L"  256 色系统色 (0-15):\n  ");
    for (int i = 0; i < 16; ++i)
        wprint(L"\x1b[38;5;%dm ■ \x1b[0m", i);
    wprint(L"\n  256 色连续色 (196-231):\n  ");
    for (int i = 196; i <= 231; ++i)
        wprint(L"\x1b[48;5;%dm  \x1b[0m", i);
    wprint(L"\n  True Color 前景:\n  ");
    wprint(L"\x1b[38;2;255;128;64m ■ RGB(255,128,64) 暖橙色 \x1b[0m\n");
    wprint(L"  \x1b[38;2;64;128;255m ■ RGB(64,128,255) 浅蓝色 \x1b[0m\n");
    wprint(L"  True Color 背景条:\n  ");
    wprint(L"\x1b[48;2;200;50;50m                                \x1b[0m\n  ");
    wprint(L"\x1b[48;2;255;200;0m                                \x1b[0m\n  ");
    wprint(L"\x1b[48;2;0;150;255m                                \x1b[0m");

    wait3s(L"检查 256 色和 True Color 是否正确渲染");
    return 0;
}
