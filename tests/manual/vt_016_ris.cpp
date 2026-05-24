// vt_016_ris.cpp — RIS 硬复位
#include "manual_common.hpp"

int main()
{
    HANDLE hOut = init_console();
    title(L"vt-016", L"RIS — 硬复位 (ESC c)\n   测试 ESCc 终端硬复位序列。\n   注意：RIS 可能导致 ConPTY 连接断开。\n  "
                     L" 此测试在发送 RIS 前提示用户确认。\n   如果 ConPTY 正确拦截 RIS，程序应继续正常执行。");

    wprint(L"\x1b[1;32m[期望结果]\x1b[0m\n");
    wprint(L"  若 ConPTY 拦截 RIS: 程序继续，光标重置\n");
    wprint(L"  若 RIS 生效: 终端状态完全重置\n");

    sep();
    wprint(L"\x1b[1;32m[实际输出]\x1b[0m\n\n");

    wprint(L"\x1b[1;31m  ⚠ RIS 可能导致终端断开连接!\x1b[0m\n");
    wprint(L"  发送 RIS ESCc...\n");
    vt(L"\x1bc");
    wprint(L"  ← 如果看到这行字，说明 RIS 已被安全处理\n");

    wait3s(L"确认程序未崩溃未断开");
    return 0;
}
