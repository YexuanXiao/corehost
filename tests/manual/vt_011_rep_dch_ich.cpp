// vt_011_rep_dch_ich.cpp — REP 重复字符 / DCH / ICH
#include "manual_common.hpp"

int main()
{
    HANDLE hOut = init_console();
    title(L"vt-011", L"REP 重复字符 / DCH 删除字符 / ICH 插入空白字符\n   测试 ESC[Ps b (REP 重复前一字符), ESC[Ps P "
                     L"(DCH 删除字符),\n   ESC[Ps @ (ICH 插入空白字符)。\n   期望：REP(10) 输出 10 个 'A'。\n   ICH(5) "
                     L"在 'ABCDE' 后插入 5 空白 → 文字右移。\n   DCH(5) 删除 5 字符 → 文字左移恢复。");

    wprint(L"\x1b[1;32m[期望结果]\x1b[0m\n");
    wprint(L"  REP(10): AAAAAAAAAA (10个A)\n");
    wprint(L"  ICH(5): 在 ABCDE 后插入5空白 → 后续文字右移\n");
    wprint(L"  DCH(5): 删除5字符 → 右移的文字恢复\n");

    sep();
    wprint(L"\x1b[1;32m[实际输出]\x1b[0m\n\n");

    wprint(L"  [REP 10] 重复前一字符 10 次 → A");
    vt(L"\x1b[10b");
    wprint(L" ← 应有 10 个 A\n");

    wprint(L"  [ICH 5] 插入 5 空白字符 (在 'ABCDE' 后):\n  ABCDE");
    vt(L"\x1b[5@");
    wprint(L" ← 'FGHI' 被右移了\n");

    Sleep(1000);
    wprint(L"  [DCH 5] 删除 5 字符:\n  ");
    vt(L"\x1b[5P");
    wprint(L"← 右移内容恢复\n");

    wait3s(L"检查 REP(10个A), ICH(插入空白), DCH(删除)效果");
    return 0;
}
