// vt_002_sgr_16color.cpp — 16 色前景/背景
#include "manual_common.hpp"

int main()
{
    HANDLE hOut = init_console();
    title(L"vt-002", L"SGR — 16 色前景/背景\n   测试 ESC[30m-37m (8 标准前景色) 和 ESC[90m-97m (8 亮前景色)\n   以及 "
                     L"ESC[40m-47m (8 背景色)。\n   期望：每行前8色为黑红绿黄蓝品青白，后8色为亮色版本。\n   "
                     L"背景色行每段有明显底色。共 24 个色块。");

    wprint(L"\x1b[1;32m[期望结果]\x1b[0m\n");
    wprint(L"  行1 (30-37): 黑 红 绿 黄 蓝 品 青 白\n");
    wprint(L"  行2 (90-97): 亮黑 亮红 亮绿 亮黄 亮蓝 亮品 亮青 亮白\n");
    wprint(L"  行3 (40-47): 黑底 红底 绿底 黄底 蓝底 品底 青底 白底\n");

    sep();
    wprint(L"\x1b[1;32m[实际输出]\x1b[0m\n\n");

    wprint(L"  前景 8 色 (30-37):\n  ");
    for (int i = 0; i < 8; ++i)
        wprint(L"\x1b[%dm ■ Col%d \x1b[0m", 30 + i, i);
    wprint(L"\n  前景亮 8 色 (90-97):\n  ");
    for (int i = 0; i < 8; ++i)
        wprint(L"\x1b[%dm ■ Col%d \x1b[0m", 90 + i, i);
    wprint(L"\n  背景 8 色 (40-47):\n  ");
    for (int i = 0; i < 8; ++i)
        wprint(L"\x1b[%dm Bg%d \x1b[0m", 40 + i, i);

    wait3s(L"检查 16 色前景 + 8 色背景是否正确");
    return 0;
}
