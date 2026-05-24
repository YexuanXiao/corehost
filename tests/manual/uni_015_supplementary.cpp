// uni_015_supplementary.cpp — 补充平面 Surrogate Pairs
#include "manual_common.hpp"

int main()
{
    HANDLE hOut = init_console();
    title(L"uni-015",
          L"补充平面字符 — Surrogate Pairs (U+10000+)\n   测试 UTF-16 代理对表示的高平面字符。\n   WriteConsoleW 接收 "
          L"wchar_t 数组, 补充平面字符\n   使用代理对 (高代理 + 低代理) 编码。\n   期望：Emoji (U+1F600+)、Ext-B 汉字 "
          L"(U+20000+)、\n   音乐符号 (U+1D100+) 均正确显示，一个字符占 2 个 wchar_t。");

    wprint(L"\x1b[1;32m[期望结果]\x1b[0m\n");
    wprint(L"  Emoji 每字符占 2 个 wchar_t (代理对)\n");
    wprint(L"  Ext-B 汉字同样通过代理对表示\n");
    wprint(L"  音乐符号 (G谱号等) 1 字符 = 2 wchar_t\n");

    sep();
    wprint(L"\x1b[1;32m[实际输出]\x1b[0m\n\n");

    wprint(L"  Emoji (U+1F600+, 每字符=2 wchar_t):\n  ");
    wprint(L"😀😃😄😁😅😂🤣😊😇🙂🙃😉😌😍🥰😘😗😙😚😋😛😝\n");

    wprint(L"\n  CJK Ext-B (U+20000+):\n  ");
    wprint(L"𠀀𠀁𠀂𠀃𠀄𠀅𠀆𠀇𠀈𠀉𠀊𠀋𠀌𠀍𠀎𠀏\n");

    wprint(L"\n  音乐符号 (U+1D100+):\n  ");
    wprint(L"𝄞 (G谱号)  𝄢 (F谱号)  𝄫 (降号)  𝄪 (重升号)\n");
    wprint(L"  𝅗𝅥 (八分音符)  𝄽 (休止符)\n");

    wprint(L"\n  \x1b[1;37m验证:\x1b[0m 所有补充平面字符应完整显示, 不出现孤立代理\n");

    wait3s(L"检查补充平面 Emoji/CJK/音乐符号");
    return 0;
}
