// uni_012_control_pictures.cpp — 控制字符图形
#include "manual_common.hpp"

int main()
{
    HANDLE hOut = init_console();
    title(L"uni-012", L"控制字符图形 (U+2400-U+243F)\n   测试 ASCII 控制字符的可视图标。\n   "
                      L"期望：NUL/SOH/STX/ETX/BEL/BS/HT/LF/CR/ESC/DEL/SP\n   各字符显示其标准缩写图形 (如 ␀␁␂␃...)。\n "
                      L"  这些用于可视化不可打印的控制字符。");

    wprint(L"\x1b[1;32m[期望结果]\x1b[0m\n");
    wprint(L"  每个控制字符显示其标准图形:\n");
    wprint(L"  NUL=␀ SOH=␁ STX=␂ ETX=␃ BEL=␇ BS=␈\n");
    wprint(L"  HT=␉ LF=␊ CR=␍ ESC=␛ DEL=␡ SP=␠\n");

    sep();
    wprint(L"\x1b[1;32m[实际输出]\x1b[0m\n\n");

    wprint(L"  NUL=\u2400  SOH=\u2401  STX=\u2402  ETX=\u2403\n");
    wprint(L"  BEL=\u2407  BS=\u2408   HT=\u2409   LF=\u240A\n");
    wprint(L"  CR=\u240D  ESC=\u241B  DEL=\u2421  SP=\u2420\n");
    wprint(L"  SYN=\u2416  ETB=\u2417  CAN=\u2418  EM=\u2419\n");

    wprint(L"\n  \x1b[1;37m验证:\x1b[0m 每个应为两字符的缩写图标, 不是空白\n");

    wait3s(L"检查控制字符图形图标");
    return 0;
}
