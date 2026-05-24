// uni_007_zero_width.cpp — ZWJ/ZWNJ/ZWSP/WJ 零宽字符
#include "manual_common.hpp"

int main()
{
    HANDLE hOut = init_console();
    title(L"uni-007",
          L"零宽字符 — ZWJ / ZWNJ / ZWSP / WJ\n   测试零宽连接符 (ZWJ U+200D)、零宽断连符 (ZWNJ U+200C)、\n   零宽空格 "
          L"(ZWSP U+200B)、词连接符 (WJ U+2060)。\n   期望：ZWJ 连接 Emoji 形成合字。\n   ZWNJ 阻止波斯语连写。ZWSP "
          L"不可见但可断行。\n   WJ 不可见但阻止断行。所有零宽字符不占显示宽度。");

    wprint(L"\x1b[1;32m[期望结果]\x1b[0m\n");
    wprint(L"  ZWJ:   👨‍💻 = 男程序员 (单字符宽度合字)\n");
    wprint(L"  ZWNJ:  阻止波斯语字母连写\n");
    wprint(L"  ZWSP:  不可见, 可在此处换行\n");
    wprint(L"  WJ:    不可见, 禁止换行\n");

    sep();
    wprint(L"\x1b[1;32m[实际输出]\x1b[0m\n\n");

    wprint(L"  ZWJ 合字:\n");
    wprint(L"  👨 + ZWJ + 💻 = 👨\u200D💻 (男程序员)\n");
    wprint(L"  👩 + ZWJ + 🎓 = 👩\u200D🎓 (女毕业生)\n");
    wprint(L"  ❤ + ZWJ + 🔥 = ❤\u200D🔥 (心火)\n");

    wprint(L"\n  ZWNJ 断连 (波斯语):\n");
    wprint(L"  ﻧﻤﯽ\u200Cخواﻫﻢ (中间 ZWNJ 阻止连写)\n");

    wprint(L"\n  ZWSP 隐式断词点:\n");
    wprint(L"  Super\u200BCali\u200BFragi\u200Blistic\n");

    wprint(L"\n  WJ 词连接 (禁止断行):\n");
    wprint(L"  不\u2060可\u2060分\u2060割 (WJ 在字符间, 不应断开)\n");

    wait3s(L"检查 ZWJ 合字效果, ZWNJ/ZWSP/WJ 不占宽度");
    return 0;
}
