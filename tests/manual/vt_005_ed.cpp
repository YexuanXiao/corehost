// vt_005_ed.cpp — 擦除显示 ED
#include "manual_common.hpp"

int main()
{
    HANDLE hOut = init_console();
    title(L"vt-005", L"ED — 擦除显示 (ESC[PsJ)\n   测试 ED2 (清全屏, ESC[2J) 和 ED0 (光标到屏尾, ESC[0J)。\n   "
                     L"期望：ED2 清除整个屏幕后重置光标。\n   ED0 擦除光标所在位置及之后的所有内容。");

    wprint(L"\x1b[1;32m[期望结果]\x1b[0m\n");
    wprint(L"  [ED2] 全屏清空, 光标回到 (0,0)\n");
    wprint(L"  [ED0] 从光标位置 (20, 当前行) 擦到屏幕末尾\n");

    sep();
    wprint(L"\x1b[1;32m[实际输出]\x1b[0m\n\n");

    wprint(L"  正在执行 ED2 (清除全屏) — ESC[2J...\n");
    Sleep(1000);
    vt(L"\x1b[2J\x1b[H");
    wprint(L"  ← 全屏已清除，光标在 (0,0)。\n");
    wprint(L"  下面写 3 行后执行 ED0:\n");
    for (int i = 0; i < 3; ++i)
        wprint(L"  这一行将在 ED0 中被擦除 ───────────────────────\n");
    vt(L"\x1b[s");
    vt(L"\x1b[20G");
    vt(L"\x1b[0J");
    vt(L"\x1b[u");
    wprint(L"  ← ED0 执行完毕。光标右侧及以下应被擦除(空白)。\n");

    wait3s(L"检查 ED2 清屏后 ED0 部分擦除效果");
    return 0;
}
