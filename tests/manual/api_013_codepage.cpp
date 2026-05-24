// api_013_codepage.cpp — 测试 GetConsoleCP / GetConsoleOutputCP
#include "manual_common.hpp"

int main()
{
    HANDLE hOut = init_console();
    title(L"api-013", L"GetConsoleCP / GetConsoleOutputCP — 代码页\n   测试获取控制台输入和输出代码页。\n   "
                      L"期望：显示输入 CP 和输出 CP 数值。\n   在 ConPTY 下通常为 65001 (UTF-8)。\n   常见 CP: "
                      L"936=GBK, 932=SJIS, 949=Korean, 950=BIG5, 65001=UTF-8。");

    wprint(L"\x1b[1;32m[期望结果]\x1b[0m\n");
    wprint(L"  GetConsoleCP()      返回输入代码页\n");
    wprint(L"  GetConsoleOutputCP() 返回输出代码页\n");
    wprint(L"  ConPTY 下通常为 65001 (UTF-8)\n");

    sep();
    wprint(L"\x1b[1;32m[实际输出]\x1b[0m\n\n");

    UINT cpIn = GetConsoleCP();
    UINT cpOut = GetConsoleOutputCP();
    wprint(L"  输入代码页 (CP):  %u", cpIn);
    if (cpIn == 65001)
        wprint(L" ← UTF-8 ✓");
    else if (cpIn == 936)
        wprint(L" ← GBK");
    wprint(L"\n");
    wprint(L"  输出代码页 (CP):  %u", cpOut);
    if (cpOut == 65001)
        wprint(L" ← UTF-8 ✓");
    wprint(L"\n");

    wait3s(L"检查代码页数值");
    return 0;
}
