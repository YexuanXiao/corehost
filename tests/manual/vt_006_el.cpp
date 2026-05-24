// vt_006_el.cpp — 擦除行 EL
#include "manual_common.hpp"

int main()
{
    HANDLE hOut = init_console();
    title(L"vt-006", L"EL — 擦除行 (ESC[PsK)\n   测试 EL0 (光标到行尾, ESC[0K)、EL1 (行首到光标, ESC[1K)、\n   EL2 "
                     L"(整行, ESC[2K)。\n   期望：EL0 擦除右侧文字，EL1 擦除左侧文字，EL2 清除整行。");

    wprint(L"\x1b[1;32m[期望结果]\x1b[0m\n");
    wprint(L"  EL0: 光标 (col 20) 右侧文字消失\n");
    wprint(L"  EL1: 光标 (col 20) 左侧文字消失，保留右侧\n");
    wprint(L"  EL2: 整行清空\n");

    sep();
    wprint(L"\x1b[1;32m[实际输出]\x1b[0m\n\n");

    wprint(L"  [EL0] 本行前一半保留 后一半擦除——");
    vt(L"\x1b[20G");
    vt(L"\x1b[0K");
    wprint(L"← 光标后应被擦除\n");
    Sleep(800);
    wprint(L"  [EL1] ");
    vt(L"\x1b[1K");
    wprint(L"← 本行前半应被擦除，只剩这段\n");
    Sleep(800);
    wprint(L"  [EL2] 这整行会被擦除");
    vt(L"\x1b[2K");
    wprint(L"← 擦除后这里出现\n");

    wait3s(L"检查 EL0/EL1/EL2 擦除效果");
    return 0;
}
