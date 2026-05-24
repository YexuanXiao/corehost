// api_002_screen_buffer_info.cpp — 测试 GetConsoleScreenBufferInfo
#include "manual_common.hpp"

int main()
{
    HANDLE hOut = init_console();
    title(L"api-002",
          L"GetConsoleScreenBufferInfo — 屏幕缓冲区信息\n   测试获取控制台屏幕缓冲区尺寸、光标位置、窗口范围。\n   "
          L"期望：显示 dwSize (缓冲大小)、dwCursorPosition (光标位置 0-based)、\n   wAttributes (文本属性)、srWindow "
          L"(可视窗口)、dwMaximumWindowSize。\n   这些值应与实际 ConPTY 配置一致。");

    wprint(L"\x1b[1;32m[期望结果]\x1b[0m\n");
    wprint(L"  dwSize              = 缓冲区宽×高 (创建时指定, 如 120×30)\n");
    wprint(L"  dwCursorPosition    = 当前光标位置, 0-based\n");
    wprint(L"  wAttributes         = 当前文本属性值, 如 0x0007\n");
    wprint(L"  srWindow            = 可视窗口范围\n");
    wprint(L"  dwMaximumWindowSize = 最大窗口尺寸\n");

    sep();
    wprint(L"\x1b[1;32m[实际输出]\x1b[0m\n\n");

    CONSOLE_SCREEN_BUFFER_INFO csbi{};
    if (GetConsoleScreenBufferInfo(hOut, &csbi))
    {
        wprint(L"  dwSize:              (%d, %d)  ← 缓冲区宽×高\n", csbi.dwSize.X, csbi.dwSize.Y);
        wprint(L"  dwCursorPosition:    (%d, %d)  ← 当前光标 (0-based)\n", csbi.dwCursorPosition.X,
               csbi.dwCursorPosition.Y);
        wprint(L"  wAttributes:         0x%04X\n", csbi.wAttributes);
        wprint(L"  srWindow:            L=%d T=%d R=%d B=%d  ← 可视窗口\n", csbi.srWindow.Left, csbi.srWindow.Top,
               csbi.srWindow.Right, csbi.srWindow.Bottom);
        wprint(L"  dwMaximumWindowSize: (%d, %d)  ← 最大窗口\n", csbi.dwMaximumWindowSize.X,
               csbi.dwMaximumWindowSize.Y);

        wprint(L"\n  \x1b[1;37m验证:\x1b[0m dwSize.X>0, dwSize.Y>0, srWindow 非零, cursor 在范围内\n");
    }
    else
        wprint(L"  \x1b[1;31mERROR:\x1b[0m GetConsoleScreenBufferInfo 失败! GLE=%lu\n", GetLastError());

    wait3s(L"检查屏幕缓冲区信息是否正确");
    return 0;
}
