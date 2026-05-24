// vt_014_charset.cpp — DEC 特殊图形字符集
#include "manual_common.hpp"

int main()
{
    HANDLE hOut = init_console();
    title(L"vt-014", L"DEC 特殊图形字符集 (线条绘制)\n   测试 ESC(0 (切换到 DEC 特殊图形) 和 ESC(B (恢复 ASCII)。\n   "
                     L"期望：切换后字符 'jklmnqtuvwx' 映射为线条字符：\n   ┘┐┌└┼─├┤┴┬。这些字符在支持 DEC "
                     L"字符集的终端中显示为连线。");

    wprint(L"\x1b[1;32m[期望结果]\x1b[0m\n");
    wprint(L"  ASCII: j k l m n q t u v w x\n");
    wprint(L"  DEC:   ┘ ┐ ┌ └ ┼ ─ ├ ┤ ┴ ┬ │\n");
    wprint(L"  终端应在切换后显示线条图形\n");

    sep();
    wprint(L"\x1b[1;32m[实际输出]\x1b[0m\n\n");

    wprint(L"  ASCII 字符集: jklmnqtuvwx\n");
    wprint(L"  DEC 图形集:   ");
    vt(L"\x1b(0");
    wprint(L"jklmnqtuvwx");
    vt(L"\x1b(B");
    wprint(L"\n  ASCII 恢复:   jklmnqtuvwx\n");

    wprint(L"\n  \x1b[1;37m验证:\x1b[0m DEC 行应显示线条字符 (┘┐┌└┼─├┤┴┬│)\n");

    wait3s(L"检查 DEC 线条字符是否正确显示");
    return 0;
}
