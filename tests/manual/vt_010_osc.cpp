// vt_010_osc.cpp — OSC 窗口标题
#include "manual_common.hpp"

int main()
{
    HANDLE hOut = init_console();
    title(L"vt-010", L"OSC — 窗口标题\n   测试 ESC]2;...ST (OSC 2 设置窗口标题) 和\n   ESC]0;...ST (OSC 0 "
                     L"设置图标名+标题)。\n   期望：OSC 2 修改 WT 标签页标题为 'VT-010 标题测试'。\n   OSC 0 修改为 "
                     L"'VT-010 Icon 测试'。3 秒后恢复原标题。");

    wprint(L"\x1b[1;32m[期望结果]\x1b[0m\n");
    wprint(L"  标题1: 'VT-010 标题测试' (OSC 2)\n");
    wprint(L"  标题2: 'VT-010 Icon 测试' (OSC 0)\n");
    wprint(L"  3秒后恢复原标题\n");

    sep();
    wprint(L"\x1b[1;32m[实际输出]\x1b[0m\n\n");

    wchar_t oldTitle[256]{};
    GetConsoleTitleW(oldTitle, 256);
    wprint(L"  原标题: %s\n", oldTitle);

    wprint(L"  [OSC 2] 设置标题...\n");
    vt(L"\x1b]2;VT-010 标题测试\x1b\\");
    wprint(L"  >>> 查看 WT 标签页, 3 秒...\n");
    Sleep(3000);

    wprint(L"  [OSC 0] 设置图标名+标题...\n");
    vt(L"\x1b]0;VT-010 Icon 测试\x1b\\");
    wprint(L"  >>> 查看 WT 标签页, 3 秒...\n");
    Sleep(3000);

    wprint(L"\x1b]2;%s\x1b\\", oldTitle);
    wprint(L"  已恢复原标题: %s\n", oldTitle);

    wait3s(L"检查 OSC 标题是否变化并恢复");
    return 0;
}
