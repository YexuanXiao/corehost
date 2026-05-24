// uni_018_long_text_wrap.cpp — 长文本 Unicode 换行正确性
#include "manual_common.hpp"

int main()
{
    HANDLE hOut = init_console();
    title(L"uni-018",
          L"长文本换行 — Unicode 行宽计算\n   测试在 80 列终端中纯 ASCII、纯中文、中英混合文本的换行行为。\n   "
          L"期望：80 个 '=' 恰好填满一行不换行。\n   40 个中文 '测' (80列) 恰好填满一行。\n   中英混合 'A' + '中' 交替 "
          L"60 个字符在 80 列边界正确换行。\n   换行点应在英文单词边界或列满时。");

    wprint(L"\x1b[1;32m[期望结果]\x1b[0m\n");
    wprint(L"  纯 ASCII: 80 个 '=', 恰好一行, 无多余换行\n");
    wprint(L"  纯中文: 40 个 '测' (80列), 恰好一行\n");
    wprint(L"  中英混合: 正确在 80 列边界换行, 不截断中文字符\n");

    sep();
    wprint(L"\x1b[1;32m[实际输出]\x1b[0m\n\n");

    // 纯 ASCII 80列
    wprint(L"  纯 ASCII 80 列边界 — 应恰好填满一行:\n  ");
    for (int i = 0; i < 80; ++i)
        wprint(L"=");
    wprint(L"\n  ← 上面一行应有 80 个 '=', 恰好填满, 无多余换行\n");

    // 纯中文 40字 = 80列
    wprint(L"\n  纯中文 40 字 = 80 列 (每汉字2列):\n  ");
    for (int i = 0; i < 40; ++i)
        wprint(L"测");
    wprint(L"\n  ← 上面一行应有 40 个 '测', 恰好填满 80 列\n");

    // 中英混合
    wprint(L"\n  中英混合 (A + 中 交替, 60个字符):\n  ");
    for (int i = 0; i < 60; ++i)
        wprint(i % 2 ? L"中" : L"A");
    wprint(L"\n  ← 混合文本换行位置应正确\n");

    wprint(L"\n  \x1b[1;37m验证:\x1b[0m 每行恰好填满 80 列, 中文字符不截断\n");

    wait3s(L"检查换行边界：ASCII/中文/混合 均在80列正确换行");
    return 0;
}
