// uni_011_line_break.cpp — 换行控制字符 SHY/NBSP/NNBSP
#include "manual_common.hpp"

int main()
{
    HANDLE hOut = init_console();
    title(L"uni-011", L"换行控制字符 — SHY / NBSP / NNBSP\n   测试软连字符 SHY (U+00AD)、不间断空格 NBSP (U+00A0)、\n  "
                      L" 窄不间断空格 NNBSP (U+202F)。\n   期望：SHY 在行末断行时才显示为连字符。\n   NBSP "
                      L"阻止数字和单位之间断行。\n   NNBSP 作为窄空格阻止断行。");

    wprint(L"\x1b[1;32m[期望结果]\x1b[0m\n");
    wprint(L"  SHY: 正常行不显示, 换行时显示为连字符\n");
    wprint(L"  NBSP: 数字\u00A0100\u00A0万 不分离断行\n");
    wprint(L"  NNBSP: 1\u202F000\u202F000 数字千分位\n");

    sep();
    wprint(L"\x1b[1;32m[实际输出]\x1b[0m\n\n");

    wprint(L"  SHY (U+00AD) 软连字符:\n");
    wprint(L"  长单词\u00AD断行\u00AD测试\u00AD点 → 行末才显示连字符\n");

    wprint(L"\n  NBSP (U+00A0) 不间断空格:\n");
    wprint(L"  [100\u00A0万] — 数字和单位不应分两行\n");
    wprint(L"  [50\u00A0km/h] — 同上\n");

    wprint(L"\n  NNBSP (U+202F) 窄不间断空格:\n");
    wprint(L"  [1\u202F000\u202F000] — 数字千分位\n");

    wait3s(L"检查 SHY/NBSP/NNBSP 行为 (缩小窗口看换行)");
    return 0;
}
