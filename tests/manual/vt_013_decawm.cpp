// vt_013_decawm.cpp — DEC 私有模式 自动换行
#include "manual_common.hpp"

int main()
{
    HANDLE hOut = init_console();
    title(
        L"vt-013",
        L"DEC 私有模式 — DECAWM 自动换行\n   测试 ESC[?7h (DECAWM 开启自动换行) 和 ESC[?7l (关闭)。\n   期望：开启时写 "
        L"80 个 'X' 到行尾后自动换到下一行。\n   关闭时写 80 个 'Y' 全部覆盖在行尾不换行。\n   最后恢复自动换行。");

    wprint(L"\x1b[1;32m[期望结果]\x1b[0m\n");
    wprint(L"  DECAWM ON: 80个X → 行尾自动换行, 最后字符在下一行\n");
    wprint(L"  DECAWM OFF: 80个Y → 全部在同行, 行尾仅显示最后一个Y\n");
    wprint(L"  恢复 ON: 之后文字正常换行\n");

    sep();
    wprint(L"\x1b[1;32m[实际输出]\x1b[0m\n\n");

    wprint(L"  [DECAWM ON] 写 80 个 X:\n  ");
    for (int i = 0; i < 80; ++i)
        wprint(L"X");
    wprint(L" ← 自动换到了下一行\n");

    wprint(L"  [DECAWM OFF] 写 80 个 Y:\n  ");
    vt(L"\x1b[?7l");
    for (int i = 0; i < 80; ++i)
        wprint(L"Y");
    wprint(L" ← 应在行尾, 没有换行\n");
    vt(L"\x1b[?7h");
    wprint(L"  [DECAWM ON] 自动换行已恢复\n");

    wait3s(L"检查自动换行 ON/OFF 效果：X行换行, Y行不换行");
    return 0;
}
