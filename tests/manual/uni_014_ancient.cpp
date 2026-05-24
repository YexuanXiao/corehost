// uni_014_ancient.cpp — 古文字 + 罕见文字
#include "manual_common.hpp"

int main()
{
    HANDLE hOut = init_console();
    title(L"uni-014",
          L"古文字 + 罕见文字\n   测试高平面古文字字符 (取决于字体支持)。\n   包括：埃及圣书体 (U+13000)、楔形文字 "
          L"(U+12000)、\n   线形文字 B (U+10000)、哥特体 (U+10330)、如尼文 (U+16A0)。\n   "
          L"期望：有对应字体时显示正确字形，无字体时显示 □ (豆腐块)。\n   豆腐块不是 ConPTY bug。");

    wprint(L"\x1b[1;32m[期望结果]\x1b[0m\n");
    wprint(L"  有字体: 显示古文字形\n");
    wprint(L"  无字体: 显示 □ (豆腐块, 非 ConPTY bug)\n");

    sep();
    wprint(L"\x1b[1;32m[实际输出]\x1b[0m\n\n");

    wprint(L"  埃及圣书体 (U+13000):  𓀀𓀁𓀂𓀃𓀄\n");
    wprint(L"  楔形文字 (U+12000):    𒀀𒀁𒀂𒀃\n");
    wprint(L"  线形文字B (U+10000):   𐀀𐀁𐀂𐀃𐀄\n");
    wprint(L"  哥特体 (U+10330):      𐍐𐍑𐍒𐍓𐍔\n");
    wprint(L"  如尼文 (U+16A0):       ᚠᚡᚢᚣᚤᚥ\n");
    wprint(L"  Deseret (U+10400):     𐐀𐐁𐐂𐐃\n");

    wprint(L"\n  \x1b[1;37m注:\x1b[0m □ 豆腐块 = 终端缺字体, 非 ConPTY bug\n");

    wait3s(L"检查古文字 (取决于字体, 豆腐块可接受)");
    return 0;
}
