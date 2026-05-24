// vt_012_da_dsr.cpp — 设备属性 DA / 状态报告 DSR
#include "manual_common.hpp"

int main()
{
    HANDLE hOut = init_console();
    title(L"vt-012",
          L"设备报告 — DA / DSR\n   测试 ESC[c (DA 请求设备属性) 和 ESC[6n (DSR 请求光标位置)。\n   期望：DA "
          L"请求后终端通过输入流回复设备属性字符串。\n   DSR 6 请求光标位置，终端回复 ESC[Pn;PnR。\n   在 ConPTY "
          L"中这些由 corehost 内部处理，肉眼不可直接观察。\n   此测试验证序列发送不会导致崩溃或挂起。");

    wprint(L"\x1b[1;32m[期望结果]\x1b[0m\n");
    wprint(L"  DA (ESC[c): 程序发送后正常继续，终端回复在输入流\n");
    wprint(L"  DSR6 (ESC[6n): 程序发送后正常继续\n");
    wprint(L"  ✓ 无崩溃、无挂起即为通过\n");

    sep();
    wprint(L"\x1b[1;32m[实际输出]\x1b[0m\n\n");

    wprint(L"  [DA] 发送 ESC[c ... ");
    vt(L"\x1b[c");
    wprint(L"OK (程序继续执行)\n");

    wprint(L"  [DSR6] 发送 ESC[6n ... ");
    vt(L"\x1b[6n");
    wprint(L"OK (程序继续执行)\n");

    wprint(L"\n  \x1b[1;37m验证:\x1b[0m 如果程序没有卡住，说明 DA/DSR 被正确处理\n");

    wait3s(L"确认程序无崩溃无挂起");
    return 0;
}
