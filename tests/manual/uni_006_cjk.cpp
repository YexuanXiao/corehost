// uni_006_cjk.cpp — CJK 全角/半角 + 扩展汉字
#include "manual_common.hpp"

int main()
{
    HANDLE hOut = init_console();
    title(L"uni-006", L"CJK — 全角/半角 + 扩展汉字\n   测试全角 ASCII (FF01-FF5E)、半角片假名 (FF66-FF9F)、\n   CJK "
                      L"Ext-A (U+3400) 和 Ext-B (U+20000, surrogate pairs)。\n   期望：全角字符占 2 个英文字符宽度。\n "
                      L"  Ext-B 汉字通过代理对正确显示。\n   表格对齐测试：中文列与英文列对齐。");

    wprint(L"\x1b[1;32m[期望结果]\x1b[0m\n");
    wprint(L"  全角 ASCII 比半角宽一倍\n");
    wprint(L"  Ext-A/Ext-B 汉字正确显示\n");
    wprint(L"  表格边框用 ┌─┐ 等绘制, 内部中文对齐\n");

    sep();
    wprint(L"\x1b[1;32m[实际输出]\x1b[0m\n\n");

    wprint(L"  半角 ASCII:  ABCabc123\n");
    wprint(L"  全角 ASCII:  ＡＢＣａｂｃ１２３\n");

    wprint(L"\n  CJK Ext-A (U+3400):  㐀㐁㐂㐃㐄\n");
    wprint(L"  CJK Ext-B (U+20000): 𠀀𠀁𠀂𠀃𠀄 (surrogate pairs)\n");

    wprint(L"\n  ── 表格对齐测试 ──\n");
    wprint(L"  ┌────────────────────────────┐\n");
    wprint(L"  │ 姓名      │ 测试用户       │\n");
    wprint(L"  │ 日期      │ 2026-05-22     │\n");
    wprint(L"  │ 说明      │ CJK对齐测试    │\n");
    wprint(L"  └────────────────────────────┘\n");

    wprint(L"\n  宽窄混合对齐:\n");
    wprint(L"  AAA     中文   B\n");
    wprint(L"  BBB    文文文  C\n");
    wprint(L"  CCC   测试测试 D\n");

    wait3s(L"检查全角2倍宽 + Ext汉字 + 对齐正确");
    return 0;
}
