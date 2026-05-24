// uni_013_spaces.cpp — Unicode 各种空白字符
#include "manual_common.hpp"

int main()
{
    HANDLE hOut = init_console();
    title(L"uni-013", L"Unicode 各种空白字符\n   测试不同宽度的 Unicode 空格字符。\n   期望：SP (U+0020) "
                      L"标准宽度、NBSP (U+00A0) 同宽不间断、\n   NNBSP (U+202F) 窄、MMSP (U+205F) 数学空格、\n   EMSP "
                      L"(U+2003) 全角宽、ENSP (U+2002) 半角宽、\n   TS (U+2009) 窄空格、HS (U+200A) 发丝空格、\n   "
                      L"IDSP (U+3000) 表意空格 (全角)。\n   用括号标记观察各空格宽度差异。");

    wprint(L"\x1b[1;32m[期望结果]\x1b[0m\n");
    wprint(L"  用 '[' ']' 括起每种空格, 观察不同宽度的间隔\n");

    sep();
    wprint(L"\x1b[1;32m[实际输出]\x1b[0m\n\n");

    wprint(L"  各种宽度的空格 ([' '] 标记):\n");
    wprint(L"  SP  (U+0020): [' ']\n");
    wprint(L"  NBSP(U+00A0): ['\u00A0']\n");
    wprint(L"  NNBSP(U+202F):['\u202F']\n");
    wprint(L"  MMSP(U+205F): ['\u205F']\n");
    wprint(L"  EMSP(U+2003): ['\u2003']\n");
    wprint(L"  ENSP(U+2002): ['\u2002']\n");
    wprint(L"  TS  (U+2009): ['\u2009']\n");
    wprint(L"  HS  (U+200A): ['\u200A']\n");
    wprint(L"  IDSP(U+3000): ['\u3000']\n");

    wprint(L"\n  \x1b[1;37m验证:\x1b[0m 不同空格宽度应不同 (EM最宽, HS最窄)\n");

    wait3s(L"检查各种空格宽度差异");
    return 0;
}
