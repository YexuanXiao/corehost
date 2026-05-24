// vt_007_decsc_decrc.cpp — DECSC/DECRC 保存恢复光标 + 滚动区域
#include "manual_common.hpp"

int main()
{
    HANDLE hOut = init_console();
    title(L"vt-007", L"DECSC/DECRC 光标保存恢复 + DECSTBM 滚动区域\n   测试 ESC7/ESC8 (DECSC/DECRC) "
                     L"保存恢复光标位置。\n   测试 ESC[top;bottom r 设置滚动区域 (DECSTBM)。\n   期望：ESC7 保存 → "
                     L"光标移走 → ESC8 恢复到原位。\n   DECSTBM 限制滚动仅在 5-15 行内发生。");

    wprint(L"\x1b[1;32m[期望结果]\x1b[0m\n");
    wprint(L"  DECSC/DECRC: 光标移走后恢复原位\n");
    wprint(L"  DECSTBM(5,15): 滚动仅在 5-15 行内, 写20行后仅该区域滚动\n");

    sep();
    wprint(L"\x1b[1;32m[实际输出]\x1b[0m\n\n");

    wprint(L"  [DECSC] 保存光标");
    vt(L"\x1b7");
    vt(L"\x1b[10;30H");
    wprint(L" ← 光标已移到 (30,10), 等1秒...");
    Sleep(1000);
    vt(L"\x1b8");
    wprint(L" ← DECRC 恢复, 光标应回到原位\n");

    wprint(L"\n  [DECSTBM] 设置滚动区域 (5-15), 写 20 行:\n");
    vt(L"\x1b[5;15r");
    for (int i = 0; i < 20; ++i)
        wprint(L"  第 %d 行 — 仅 5-15 区域应滚动\n", i + 1);
    vt(L"\x1b[r");
    wprint(L"  ← 滚动区域已恢复\n");

    wait3s(L"检查 DECSC/DECRC 恢复 和 滚动区域仅限5-15行");
    return 0;
}
