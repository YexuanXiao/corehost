// api_011_title.cpp — 测试 Get/SetConsoleTitle
#include "manual_common.hpp"

int main()
{
    HANDLE hOut = init_console();
    title(L"api-011", L"Get/SetConsoleTitle — 窗口标题\n   测试读取和设置控制台窗口标题。\n   "
                      L"期望：先读取并显示当前标题。然后设置新标题为\n   '[API-011 Test] ConPTY 标题测试'。3 "
                      L"秒后恢复原标题。\n   观察 Windows Terminal 标签页标题是否变化。");

    wprint(L"\x1b[1;32m[期望结果]\x1b[0m\n");
    wprint(L"  旧标题: [读取并显示]\n");
    wprint(L"  新标题: '[API-011 Test] ConPTY 标题测试'\n");
    wprint(L"  WT 标签页标题应先变为新标题, 3秒后恢复\n");

    sep();
    wprint(L"\x1b[1;32m[实际输出]\x1b[0m\n\n");

    wchar_t oldTitle[256]{};
    GetConsoleTitleW(oldTitle, 256);
    wprint(L"  旧标题: %s\n", oldTitle);

    const wchar_t *newTitle = L"[API-011 Test] ConPTY 标题测试";
    SetConsoleTitleW(newTitle);
    wprint(L"  新标题已设置: %s\n", newTitle);
    wprint(L"  >>> 请查看 WT 标签页, 标题应为上述文字, 3 秒后恢复...\n");
    Sleep(3000);

    SetConsoleTitleW(oldTitle);
    wprint(L"  标题已恢复: %s\n", oldTitle);

    wait3s(L"检查 WT 标签页标题是否先变后恢复");
    return 0;
}
