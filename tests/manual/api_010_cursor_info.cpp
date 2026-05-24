// api_010_cursor_info.cpp — 测试 Get/SetConsoleCursorInfo
#include "manual_common.hpp"

int main()
{
    HANDLE hOut = init_console();
    title(L"api-010", L"Get/SetConsoleCursorInfo — 光标外观\n   测试读取和设置光标的大小和可见性。\n   "
                      L"期望：先读取当前光标信息并显示。然后依次设置为：\n   100%%大光标 → 25%%小光标 → 隐藏光标 → "
                      L"恢复默认(25%%)。\n   每个状态持续约 1 秒可见。");

    wprint(L"\x1b[1;32m[期望结果]\x1b[0m\n");
    wprint(L"  阶段1: 光标大小 100%% (填满整个字符格)\n");
    wprint(L"  阶段2: 光标大小 25%% (底部下划线样式)\n");
    wprint(L"  阶段3: 光标隐藏 (完全不可见)\n");
    wprint(L"  阶段4: 光标恢复 (25%%, 可见)\n");

    sep();
    wprint(L"\x1b[1;32m[实际输出]\x1b[0m\n\n");

    CONSOLE_CURSOR_INFO ci{};
    GetConsoleCursorInfo(hOut, &ci);
    wprint(L"  当前光标: 大小=%lu%%, 可见=%s\n", ci.dwSize, ci.bVisible ? L"是" : L"否");

    wprint(L"  [1] 设置 100%% 大光标...\n");
    ci.dwSize = 100;
    ci.bVisible = TRUE;
    SetConsoleCursorInfo(hOut, &ci);
    Sleep(1500);

    wprint(L"  [2] 设置 25%% 小光标...\n");
    ci.dwSize = 25;
    SetConsoleCursorInfo(hOut, &ci);
    Sleep(1500);

    wprint(L"  [3] 隐藏光标...\n");
    ci.bVisible = FALSE;
    SetConsoleCursorInfo(hOut, &ci);
    Sleep(1500);

    wprint(L"  [4] 恢复默认 (25%%, 可见)...\n");
    ci.bVisible = TRUE;
    ci.dwSize = 25;
    SetConsoleCursorInfo(hOut, &ci);

    wait3s(L"检查光标变化：大→小→隐藏→恢复");
    return 0;
}
