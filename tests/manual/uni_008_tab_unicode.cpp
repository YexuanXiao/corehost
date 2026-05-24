// uni_008_tab_unicode.cpp — Tab 停止位 + Unicode
#include "manual_common.hpp"

int main()
{
    HANDLE hOut = init_console();
    title(L"uni-008", L"Tab 停止位 + Unicode 双宽字符\n   测试 Tab 字符在包含中文 (双宽字符) 时的对齐行为。\n   "
                      L"期望：每个 Tab 跳到下一个 8 的倍数列，\n   正确考虑中文字符的双宽特性。混合语言列应对齐。");

    wprint(L"\x1b[1;32m[期望结果]\x1b[0m\n");
    wprint(L"  每行通过 Tab 分隔的列应对齐到相同列号\n");
    wprint(L"  英\t英  → 第8列对齐\n");
    wprint(L"  中\t英  → 中占2列, Tab跳到第10列\n");
    wprint(L"  中中\t英 → 中中占4列, Tab跳到第12列\n");

    sep();
    wprint(L"\x1b[1;32m[实际输出]\x1b[0m\n\n");

    wprint(L"  Tab 位 (每 8 列):\n");
    wprint(L"  Col: 0       8       16      24      32\n");
    wprint(L"  A\tB\tC\tD\tE\n");
    wprint(L"  AB\tCD\tEF\tGH\tIJ\n");
    wprint(L"  文\t字\t测\t试\t!\n");
    wprint(L"  中文\tAB\t混合\t测试\tTab\n");
    wprint(L"  한글\t문자\t테스트\t!\n");

    wprint(L"\n  \x1b[1;37m验证:\x1b[0m 每行 Tab 后的列应对齐 (最左列/第8列/第16列...)\n");

    wait3s(L"检查 Tab 对齐 (含中文, 每8列一个停止位)");
    return 0;
}
