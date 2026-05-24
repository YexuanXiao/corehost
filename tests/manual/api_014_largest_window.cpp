// api_014_largest_window.cpp — 测试 GetLargestConsoleWindowSize
#include "manual_common.hpp"

int main()
{
    HANDLE hOut = init_console();
    title(L"api-014", L"GetLargestConsoleWindowSize — 最大窗口尺寸\n   测试获取当前控制台允许的最大窗口尺寸。\n   "
                      L"期望：返回一个 COORD，X 和 Y 均大于 0，\n   通常 X≥80, Y≥25。该值取决于显示驱动和字体大小。");

    wprint(L"\x1b[1;32m[期望结果]\x1b[0m\n");
    wprint(L"  COORD.X ≥ 80 (列)\n");
    wprint(L"  COORD.Y ≥ 25 (行)\n");
    wprint(L"  实际值取决于屏幕分辨率和字体大小\n");

    sep();
    wprint(L"\x1b[1;32m[实际输出]\x1b[0m\n\n");

    COORD sz = GetLargestConsoleWindowSize(hOut);
    wprint(L"  最大窗口尺寸: (%d, %d)\n", sz.X, sz.Y);
    if (sz.X >= 80 && sz.Y >= 25)
        wprint(L"  \x1b[1;32m✓ 尺寸合理 (≥80×25)\x1b[0m\n");
    else
        wprint(L"  \x1b[1;31m✗ 尺寸异常\x1b[0m\n");

    wait3s(L"检查最大窗口尺寸");
    return 0;
}
