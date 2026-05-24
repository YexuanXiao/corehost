// api_009_text_attr.cpp — 测试 SetConsoleTextAttribute
#include "manual_common.hpp"

int main()
{
    HANDLE hOut = init_console();
    title(L"api-009", L"SetConsoleTextAttribute — 设置文本属性\n   测试 SetConsoleTextAttribute 改变后续 WriteConsole "
                      L"的文本颜色。\n   期望：依次输出默认灰色、亮红、亮绿、亮蓝、红底亮绿、恢复默认。\n   "
                      L"每种颜色的文字分别为 'Hello!' 或说明文字。");

    wprint(L"\x1b[1;32m[期望结果]\x1b[0m\n");
    wprint(L"  行1: 默认色 'Hello!' (灰色)\n");
    wprint(L"  行2: 亮红色 'Hello!' (FOREGROUND_RED|INTENSITY)\n");
    wprint(L"  行3: 亮绿色 'Hello!' (FOREGROUND_GREEN|INTENSITY)\n");
    wprint(L"  行4: 亮蓝色 'Hello!' (FOREGROUND_BLUE|INTENSITY)\n");
    wprint(L"  行5: 红底亮绿 'Hello!' (BACKGROUND_RED|FG_GREEN|INTENSITY)\n");
    wprint(L"  行6: 恢复默认后 'Hello!' (灰)\n");

    sep();
    wprint(L"\x1b[1;32m[实际输出]\x1b[0m\n\n");

    wprint(L"  默认色: ");
    SetConsoleTextAttribute(hOut, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
    wprint(L"Hello!\n");

    wprint(L"  亮红色: ");
    SetConsoleTextAttribute(hOut, FOREGROUND_RED | FOREGROUND_INTENSITY);
    wprint(L"Hello!\n");

    wprint(L"  亮绿色: ");
    SetConsoleTextAttribute(hOut, FOREGROUND_GREEN | FOREGROUND_INTENSITY);
    wprint(L"Hello!\n");

    wprint(L"  亮蓝色: ");
    SetConsoleTextAttribute(hOut, FOREGROUND_BLUE | FOREGROUND_INTENSITY);
    wprint(L"Hello!\n");

    wprint(L"  红底亮绿: ");
    SetConsoleTextAttribute(hOut, BACKGROUND_RED | FOREGROUND_GREEN | FOREGROUND_INTENSITY);
    wprint(L"Hello!\n");

    SetConsoleTextAttribute(hOut, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
    wprint(L"  恢复默认: Hello!\n");

    wait3s(L"检查五色文本：默认/亮红/亮绿/亮蓝/红底亮绿/恢复默认");
    return 0;
}
